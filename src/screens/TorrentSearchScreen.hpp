#pragma once

#include "net/TorrentManager.hpp"
#include "platform/Process.hpp"
#include "render/Font.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cyberdeck {

// Torrent search, and adding a result to the download queue.
//
// This screen owns its Process directly rather than going through
// TorrentManager's worker. A search is short, cancellable, and lives entirely
// within a screen that is on-screen for its whole duration — the same
// arrangement AskDeckScreen has, and it keeps the shared worker free to keep
// polling downloads while a search runs.
class TorrentSearchScreen final : public Screen {
public:
    TorrentSearchScreen(Font* titleFont, Font* bodyFont, TorrentManager* torrents,
                        ScreenManager* navigator);

    void onEnter() override;
    void onExit() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

    // Esc steps back through the screen's own modes before popping it.
    bool consumesBack() const override { return mode_ != Mode::Query; }
    bool wantsTextInput() const override { return mode_ == Mode::Query; }

private:
    enum class Mode { Query, Searching, Results };

    struct Result {
        std::string name;
        std::string magnet;
        MediaType route = MediaType::Download;
        int seeders = 0;
        std::int64_t sizeBytes = 0;
    };

    void startSearch();
    void cancelSearch();
    void consumeOutput();
    void flash(const std::string& message);

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    TorrentManager* torrents_ = nullptr;
    ScreenManager* navigator_ = nullptr;

    Mode mode_ = Mode::Query;
    std::string query_;
    int categoryIndex_ = 0;
    std::vector<Result> results_;
    int focusIndex_ = 0;
    int scrollOffset_ = 0;
    std::string message_;
    std::string flashMessage_;
    float flashTimer_ = 0.0f;
    float spinnerTimer_ = 0.0f;

    Process search_;
    std::vector<std::string> lines_;
    std::size_t consumed_ = 0;
};

}  // namespace cyberdeck
