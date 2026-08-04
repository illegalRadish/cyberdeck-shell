#pragma once

#include "ai/AiAssets.hpp"
#include "core/Time.hpp"
#include "input/Input.hpp"
#include "media/MediaLibrary.hpp"
#include "media/MpvPlayer.hpp"
#include "net/TorrentManager.hpp"
#include "platform/Window.hpp"
#include "render/Font.hpp"
#include "render/GLRenderer.hpp"
#include "system/SystemSampler.hpp"
#include "ui/ScreenManager.hpp"

namespace cyberdeck {

class Application {
public:
    Application() = default;
    ~Application() = default;

    int run();

private:
    bool init();
    void shutdown();
    void frame(float dt);

    Window window_;
    GLRenderer renderer_;
    Input input_;
    Time time_;
    Font titleFont_;
    Font bodyFont_;
    SystemSampler sampler_;
    MediaLibrary library_;
    AiAssets ai_;  // declared after library_: initialize() reads library_.root()
    TorrentManager torrents_;  // likewise, and it files into library_
    MpvPlayer player_;
    ScreenManager screens_;
    bool running_ = false;
    bool sdlImgReady_ = false;
    bool sdlTtfReady_ = false;
    float bgTime_ = 0.0f;

};

}  // namespace cyberdeck
