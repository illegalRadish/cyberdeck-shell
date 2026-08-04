#pragma once

#include "media/IPlayer.hpp"
#include "media/MediaLibrary.hpp"
#include "render/Font.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

#include <vector>

namespace cyberdeck {

class MoviesScreen final : public Screen {
public:
    MoviesScreen(Font* titleFont, Font* bodyFont, MediaLibrary* library, IPlayer* player,
                 ScreenManager* navigator);

    void onEnter() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    void rebuild();
    void openFocused(bool resume);

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    MediaLibrary* library_ = nullptr;
    IPlayer* player_ = nullptr;
    ScreenManager* navigator_ = nullptr;
    std::vector<MediaItem> continueItems_;
    std::vector<MediaItem> movies_;
    int focusIndex_ = 0;  // 0..continue+movies
    float refreshTimer_ = 0.0f;

    int totalCount() const {
        return static_cast<int>(continueItems_.size() + movies_.size());
    }
    const MediaItem* itemAt(int index) const;
};

}  // namespace cyberdeck
