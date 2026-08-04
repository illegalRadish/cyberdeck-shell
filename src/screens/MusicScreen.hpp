#pragma once

#include "media/IPlayer.hpp"
#include "media/MediaLibrary.hpp"
#include "render/Font.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"
#include "ui/Tween.hpp"

#include <vector>

namespace cyberdeck {

class MusicScreen final : public Screen {
public:
    MusicScreen(Font* titleFont, Font* bodyFont, MediaLibrary* library, IPlayer* player,
                ScreenManager* navigator);

    void onEnter() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    void rebuild();
    void playFocused();

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    MediaLibrary* library_ = nullptr;
    IPlayer* player_ = nullptr;
    ScreenManager* navigator_ = nullptr;
    std::vector<MediaItem> items_;
    int focusIndex_ = 0;
    float refreshTimer_ = 0.0f;
    Tween focusSlide_{0.0f, 0.0f, 0.28f, Ease::OutCubic};
};

}  // namespace cyberdeck
