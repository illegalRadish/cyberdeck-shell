#pragma once

#include "media/IPlayer.hpp"
#include "media/MediaLibrary.hpp"
#include "render/Font.hpp"
#include "render/Texture.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"
#include "ui/Tween.hpp"

namespace cyberdeck {

class NowPlayingScreen final : public Screen {
public:
    NowPlayingScreen(Font* titleFont, Font* bodyFont, IPlayer* player, MediaLibrary* library,
                     ScreenManager* navigator);

    void onEnter() override;
    void onExit() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;
    // Suppress the CRT scanline overlay while fullscreen video is rendering so
    // the footage stays clean; audio (cover art / placeholder) keeps the effect.
    bool wantsScanlines() const override {
        return !(player_ && player_->status().hasVideo && player_->videoTexture() != 0);
    }

private:
    void persistProgress();
    void loadCoverArt(const std::string& path);

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    IPlayer* player_ = nullptr;
    MediaLibrary* library_ = nullptr;
    ScreenManager* navigator_ = nullptr;
    float saveTimer_ = 0.0f;
    Tween overlayFade_{0.0f, 0.0f, 0.35f, Ease::InOutSine};
    bool wasPaused_ = false;
    Texture coverArt_;
    std::string coverPath_;
};

}  // namespace cyberdeck
