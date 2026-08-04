#pragma once

#include "ai/AiAssets.hpp"
#include "net/TorrentManager.hpp"
#include "media/IPlayer.hpp"
#include "media/MediaLibrary.hpp"
#include "render/Font.hpp"
#include "system/SystemSampler.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

namespace cyberdeck {

class AsciiSpinner;

class HomeScreen final : public Screen {
public:
    HomeScreen(Font* titleFont, Font* bodyFont, ScreenManager* navigator,
               SystemSampler* sampler, MediaLibrary* library, IPlayer* player,
               AiAssets* ai, TorrentManager* torrents);

    void onEnter() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    void refreshStatus();
    void syncSpinnerToFocus();

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    ScreenManager* navigator_ = nullptr;
    SystemSampler* sampler_ = nullptr;
    MediaLibrary* library_ = nullptr;
    IPlayer* player_ = nullptr;
    AiAssets* ai_ = nullptr;
    TorrentManager* torrents_ = nullptr;
    AsciiSpinner* spinner_ = nullptr;
    float statusTimer_ = 0.0f;
};

}  // namespace cyberdeck
