#pragma once

#include "net/TorrentManager.hpp"
#include "render/Font.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

#include <string>

namespace cyberdeck {

// Active torrents, their progress, and the actions that operate on them.
//
// The worker lives in TorrentManager (owned by Application), so leaving this
// screen does not interrupt a transfer — the same arrangement AiAssetsScreen
// has with AiAssets.
class DownloadsScreen final : public Screen {
public:
    DownloadsScreen(Font* titleFont, Font* bodyFont, TorrentManager* torrents,
                    ScreenManager* navigator);

    void onEnter() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

    // Esc cancels magnet entry rather than popping the screen.
    bool consumesBack() const override { return mode_ == Mode::MagnetEntry; }
    bool wantsTextInput() const override { return mode_ == Mode::MagnetEntry; }

private:
    enum class Mode { List, MagnetEntry };

    // What Confirm does on a focused torrent row. Selected with Left/Right,
    // which are otherwise unused here.
    enum class RowAction { Toggle, Remove };

    // Action rows below the torrent list. Install is only offered when the
    // engine is actually missing, so the index is computed, not fixed.
    enum class ActionRow { Install, Search, AddMagnet };

    int actionCount() const;
    ActionRow actionAt(int index) const;
    int torrentCount() const;
    void activateFocused();
    void flash(const std::string& message);

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    TorrentManager* torrents_ = nullptr;
    ScreenManager* navigator_ = nullptr;

    TorrentStatus status_{};
    Mode mode_ = Mode::List;
    RowAction rowAction_ = RowAction::Toggle;
    int focusIndex_ = 0;
    int scrollOffset_ = 0;
    std::string magnetInput_;
    float refreshTimer_ = 0.0f;
    std::string flashMessage_;
    float flashTimer_ = 0.0f;
};

}  // namespace cyberdeck
