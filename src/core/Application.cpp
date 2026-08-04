#include "core/Application.hpp"

#include "core/Assets.hpp"
#include "core/Types.hpp"
#include "screens/HomeScreen.hpp"

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <iostream>
#include <memory>

namespace cyberdeck {

bool Application::init() {
    if (!window_.create("Cyberdeck", 1280, 720)) {
        return false;
    }
    window_.makeCurrent();

    if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & (IMG_INIT_PNG | IMG_INIT_JPG))) {
        std::cerr << "IMG_Init warning: " << IMG_GetError() << '\n';
    } else {
        sdlImgReady_ = true;
    }

    if (TTF_Init() != 0) {
        std::cerr << "TTF_Init failed: " << TTF_GetError() << '\n';
        return false;
    }
    sdlTtfReady_ = true;

    if (!renderer_.init(window_.width(), window_.height())) {
        return false;
    }

    const std::string fontPath = assets::findFont();
    if (fontPath.empty()) {
        std::cerr << "No UI font available\n";
        return false;
    }
    std::cout << "Using font: " << fontPath << '\n';

    if (!titleFont_.load(fontPath, 42)) {
        return false;
    }
    if (!bodyFont_.load(fontPath, 26)) {
        return false;
    }

    if (!player_.init()) {
        std::cerr << "Warning: libmpv init failed — playback disabled\n";
    }

    if (library_.initialize()) {
        library_.startScan();
    } else {
        std::cerr << "Continuing without media library (PI LIB missing)\n";
    }

    ai_.initialize(library_.rootFound() ? &library_.root() : nullptr);
    torrents_.initialize(library_.rootFound() ? &library_.root() : nullptr, &library_);

    sampler_.update(0.5f);
    screens_.setRoot(std::make_unique<HomeScreen>(&titleFont_, &bodyFont_, &screens_,
                                                  &sampler_, &library_, &player_, &ai_,
                                                  &torrents_));
    running_ = true;
    return true;
}

void Application::shutdown() {
    screens_.setRoot(nullptr);
    player_.shutdown();
    ai_.requestStop();  // destructor joins the worker
    library_.shutdown();
    titleFont_.destroy();
    bodyFont_.destroy();
    renderer_.shutdown();
    if (sdlTtfReady_) {
        TTF_Quit();
        sdlTtfReady_ = false;
    }
    if (sdlImgReady_) {
        IMG_Quit();
        sdlImgReady_ = false;
    }
    window_.destroy();
    running_ = false;
}

void Application::frame(float dt) {
    input_.setTextMode(screens_.wantsTextInput());

    if (!input_.poll()) {
        running_ = false;
        return;
    }

    sampler_.update(dt);
    screens_.handleInput(input_);
    if (screens_.quitRequested()) {
        running_ = false;
        return;
    }

    screens_.update(dt);

    // Advance the slow CRT backdrop clock and draw it behind the screens.
    bgTime_ += dt;
    renderer_.beginFrame(kBgDark);
    // A dim green tint just above kBgDark so the vignette/scanlines read.
    renderer_.drawBackground(Color{0.05f, 0.11f, 0.07f, 1.0f}, bgTime_);
    screens_.draw(renderer_);
    // Flowing CRT scanline overlay on top of the UI — skipped over fullscreen
    // video so footage stays clean. Subtle dark tint, alpha drives strength.
    if (screens_.wantsScanlines()) {
        renderer_.drawScanlines(Color{0.0f, 0.0f, 0.0f, 0.22f}, bgTime_);
    }
    renderer_.endFrame();
    window_.swap();
}

int Application::run() {
    if (!init()) {
        std::cerr << "Cyberdeck failed to initialize\n";
        shutdown();
        return 1;
    }

    time_.tick();
    while (running_) {
        const float dt = time_.tick();
        frame(dt);
    }

    shutdown();
    return 0;
}

}  // namespace cyberdeck
