#pragma once

#include "media/IPlayer.hpp"
#include "net/TorrentManager.hpp"
#include "media/MediaLibrary.hpp"
#include "render/Font.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

namespace cyberdeck {

class AsciiSpinner;

// Second-level column: media categories, drilled into from HomeScreen's MEDIA entry.
class MediaScreen final : public Screen {
public:
    MediaScreen(Font* titleFont, Font* bodyFont, ScreenManager* navigator,
                MediaLibrary* library, IPlayer* player, TorrentManager* torrents);

    void onEnter() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    void refreshSubtitles();
    void syncSpinnerToFocus();
    void openFocused();

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    ScreenManager* navigator_ = nullptr;
    MediaLibrary* library_ = nullptr;
    IPlayer* player_ = nullptr;
    TorrentManager* torrents_ = nullptr;
    AsciiSpinner* spinner_ = nullptr;
    float subtitleTimer_ = 0.0f;
};

}  // namespace cyberdeck
