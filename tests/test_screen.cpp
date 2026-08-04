// Headless harness for the AskDeckScreen state machine.
//
// Drives the real screen with real SDL key events and the real mock helper.
// draw() is never called (that needs a GL context); the assertions use the
// public observables consumesBack() and wantsTextInput(), which are derived
// directly from the private stage_, so the transitions are what is being tested.
#include "ai/AiAssets.hpp"
#include "input/Input.hpp"
#include "render/Font.hpp"
#include "render/IRenderer.hpp"
#include "screens/AiAssetsScreen.hpp"
#include "screens/AskDeckScreen.hpp"
#include "ui/ScreenManager.hpp"

#include <algorithm>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace cyberdeck;

static int failures = 0;
static void check(bool cond, const std::string& what) {
    std::printf("%s  %s\n", cond ? "  ok  " : " FAIL ", what.c_str());
    if (!cond) ++failures;
}

// Push a key event the way SDL would, so Input::poll() maps it normally.
static void pressKey(SDL_Keycode key) {
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.repeat = 0;
    ev.key.keysym.sym = key;
    SDL_PushEvent(&ev);
}

// NOTE: synthesising SDL_TEXTINPUT is not possible here. This machine's SDL2 is
// sdl2-compat (a shim over SDL3), where SDL_TextInputEvent::text is a const char*
// rather than SDL2's char[32], so writing the struct by hand and pushing it
// segfaults inside SDL_PushEvent. Reading such events is fine — the shim
// translates them on the way out — so the app itself is unaffected, but this
// harness cannot inject characters. Key events (SDL_KEYDOWN) push correctly.

// One frame of the real Application loop, minus rendering.
static void frame(Input& input, AskDeckScreen& screen, float dt = 0.016f) {
    input.setTextMode(screen.wantsTextInput());
    input.poll();
    screen.handleInput(input);
    screen.update(dt);
}

// Spin frames until `pred` holds or the timeout expires.
template <typename Pred>
static bool pumpUntil(Input& input, AskDeckScreen& screen, Pred pred, int timeoutMs,
                      const char* label) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        frame(input, screen);
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    std::printf("      (timed out waiting for %s)\n", label);
    return false;
}

// Records geometry only. Screens guard every text call behind `if (bodyFont_)`,
// so passing null fonts means draw() issues nothing but rects — which lets the
// layout be checked with no GL context.
class RectRecorder final : public IRenderer {
public:
    std::vector<Rect> rects;

    bool init(int, int) override { return true; }
    void shutdown() override {}
    void beginFrame(const Color&) override {}
    void endFrame() override {}
    void setViewport(int, int) override {}
    void drawRect(const Rect& r, const Color&) override { rects.push_back(r); }
    void drawTexture(const Rect&, const Texture&, const Color&, const Vec2&,
                     const Vec2&) override {}
    void drawTextureId(const Rect&, unsigned int, const Color&, const Vec2&,
                       const Vec2&) override {}
    void drawText(Font&, const std::string&, const Vec2&, const Color&) override {}
};

// Counts live helper processes. Matches both mock_deck.py and the real
// ask_deck.py so this harness can be pointed at either.
static int countMockProcesses() {
    FILE* p = popen("pgrep -f '[a-z]*_deck\\.py' | wc -l", "r");
    if (!p) return -1;
    char buf[32] = {0};
    if (!fgets(buf, sizeof(buf), p)) { pclose(p); return -1; }
    pclose(p);
    return std::atoi(buf);
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);  // survive a crash with output intact
    const std::string mockPath = argc > 1 ? argv[1] : "assets/ai/mock_deck.py";

    if (SDL_Init(SDL_INIT_EVENTS) != 0) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        std::printf("TTF_Init failed: %s\n", TTF_GetError());
        return 1;
    }

    // Everything owning a TTF_Font must be destroyed before TTF_Quit(), which is
    // why this is a scope rather than plain locals in main. Application does the
    // same thing by calling screens_.setRoot(nullptr) before TTF_Quit().
    const int rc = [&]() {
    Font titleFont;
    Font bodyFont;
    titleFont.load("assets/fonts/VT323-Regular.ttf", 42);
    bodyFont.load("assets/fonts/VT323-Regular.ttf", 26);

    AiAssets ai;
    ai.initialize(nullptr);  // no PI LIB: the ZIM slot is unavailable but optional

    ScreenManager screens;
    AskDeckScreen screen(&titleFont, &bodyFont, &screens, &ai, nullptr, nullptr);
    Input input;

    const bool ready = ai.status().ready;
    std::printf("\n-- asset gate --\n");
    check(ready, std::string("fake asset tree satisfies the required set") +
                     (ready ? "" : " (set up by the wrapper script)"));

    screen.onEnter();

    std::printf("\n-- idle --\n");
    check(!screen.consumesBack(), "idle does not swallow Back (Esc pops normally)");
    // The prompt is always live in the conversational layout, so idle IS text
    // mode — that is what lets you just start typing without picking a mode.
    check(screen.wantsTextInput(), "idle keeps the prompt live (text mode on)");

    std::printf("\n-- voice run --\n");
    pressKey(SDLK_RETURN);  // empty prompt + Enter == "talk to me"
    frame(input, screen);
    const bool started =
        pumpUntil(input, screen, [&]() { return screen.consumesBack(); }, 4000, "recording");
    check(started, "Enter on 'speak' starts a run (Back now cancellable)");
    check(countMockProcesses() >= 1, "helper process is alive");

    // Enter again stops the recorder; the mock then walks the whole pipeline.
    pressKey(SDLK_RETURN);
    frame(input, screen);
    const bool finished =
        pumpUntil(input, screen, [&]() { return !screen.consumesBack(); }, 20000, "done");
    check(finished, "Enter stops recording and the run completes");

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    check(countMockProcesses() == 0, "no helper process left after the run");

    std::printf("\n-- Esc cancels instead of popping --\n");
    pressKey(SDLK_RETURN);  // Done -> Idle
    frame(input, screen);
    pressKey(SDLK_RETURN);  // start a voice run
    frame(input, screen);
    check(pumpUntil(input, screen, [&]() { return screen.consumesBack(); }, 4000, "recording"),
          "second run started");

    // ScreenManager would route Back to us because consumesBack() is true.
    SDL_Event esc{};
    esc.type = SDL_KEYDOWN;
    esc.key.repeat = 0;
    esc.key.keysym.sym = SDLK_ESCAPE;
    SDL_PushEvent(&esc);
    frame(input, screen);
    check(!screen.consumesBack(), "Esc during a run cancels it and releases Back");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    for (int i = 0; i < 10; ++i) frame(input, screen);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    check(countMockProcesses() == 0, "cancelled run left no orphan process");

    std::printf("\n-- teardown --\n");
    pressKey(SDLK_RETURN);  // Error -> Idle
    frame(input, screen);
    pressKey(SDLK_RETURN);  // start one more run
    frame(input, screen);
    pumpUntil(input, screen, [&]() { return screen.consumesBack(); }, 4000, "recording");
    screen.onExit();  // what ScreenManager does at the end of a pop
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    check(countMockProcesses() == 0, "onExit + destructor kill a run still in flight");

    // The prompt is modeless now: idle is always text mode, and Enter with an
    // empty buffer starts a voice turn. Characters cannot be injected here (see
    // the note on synthesising SDL_TEXTINPUT above), so the send-text path is
    // exercised by the helper tests rather than through the screen.
    std::printf("\n-- modeless prompt --\n");
    {
        AskDeckScreen fresh(&titleFont, &bodyFont, &screens, &ai, nullptr, nullptr);
        fresh.onEnter();
        check(fresh.wantsTextInput(), "fresh screen is ready to type immediately");
        check(!fresh.consumesBack(), "idle does not swallow Back");

        pressKey(SDLK_RETURN);      // empty prompt -> voice
        frame(input, fresh);
        const bool talking =
            pumpUntil(input, fresh, [&]() { return fresh.consumesBack(); }, 4000, "recording");
        check(talking, "Enter on an empty prompt starts a voice turn");
        check(!fresh.wantsTextInput(), "text mode is off while a turn is running");

        SDL_Event esc{};
        esc.type = SDL_KEYDOWN;
        esc.key.repeat = 0;
        esc.key.keysym.sym = SDLK_ESCAPE;
        SDL_PushEvent(&esc);
        frame(input, fresh);
        check(!fresh.consumesBack(), "Esc cancels and returns to the prompt");
        check(fresh.wantsTextInput(), "prompt is live again after cancelling");
        fresh.onExit();
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        check(countMockProcesses() == 0, "no helper left after cancel + exit");
    }

    // All nine asset rows must be visible. At the original fixed 46px pitch the
    // ninth row landed at y=498 and was painted over by the action row at 500,
    // so PIPELINE HELPER silently vanished from the list.
    std::printf("\n-- AiAssetsScreen row geometry --\n");
    {
        AiAssetsScreen assets(nullptr, nullptr, &ai, &screens);
        assets.onEnter();
        RectRecorder rec;
        assets.draw(rec);

        const int assetCount = static_cast<int>(ai.status().assets.size());
        std::vector<Rect> wide;
        for (const Rect& r : rec.rects) {
            if (r.w > 1000.0f && r.h > 20.0f && r.h < 100.0f) wide.push_back(r);
        }
        std::sort(wide.begin(), wide.end(),
                  [](const Rect& a, const Rect& b) { return a.y < b.y; });
        check(static_cast<int>(wide.size()) == assetCount + 2,
              "one rect per asset plus two actions: expected " +
                  std::to_string(assetCount + 2) + ", got " + std::to_string(wide.size()));

        bool overlap = false;
        for (std::size_t i = 1; i < wide.size(); ++i) {
            if (wide[i].y < wide[i - 1].y + wide[i - 1].h) overlap = true;
        }
        check(!overlap, "no row overlaps the next");
        check(!wide.empty() && wide.back().y + wide.back().h <= 620.0f,
              "last row clears the status line at y=626");
        assets.onExit();
    }

    titleFont.destroy();
    bodyFont.destroy();
    return 0;
    }();
    (void)rc;

    TTF_Quit();
    SDL_Quit();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
