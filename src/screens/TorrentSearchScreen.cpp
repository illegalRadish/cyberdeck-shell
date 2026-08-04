#include "screens/TorrentSearchScreen.hpp"

#include "core/JsonLine.hpp"
#include "core/Types.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace cyberdeck {

namespace {

constexpr float kRowTop = 176.0f;
constexpr float kRowPitch = 48.0f;
constexpr float kRowHeight = 42.0f;
constexpr int kVisibleRows = 9;

// Search categories, passed straight through to the helper. Filtering here is
// worth more than it looks: the category also decides the route the result is
// filed under, so searching in TV files the download as television without the
// name heuristics having to guess.
struct CategoryOption {
    const char* label;
    const char* code;
};

constexpr CategoryOption kCategories[] = {
    {"ALL", "0"},
    {"MOVIES", "201"},
    {"TV", "205"},
    {"MUSIC", "100"},
    {"GAMES", "400"},
};
constexpr int kCategoryCount = static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));

constexpr const char* kSpinnerFrames[] = {"|", "/", "-", "\\"};

std::string humanBytes(std::int64_t bytes) {
    char buf[48];
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0) {
        std::snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    } else if (mb >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.0f MB", mb);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    }
    return buf;
}

std::int64_t toInt64(const std::optional<std::string>& value) {
    if (!value || value->empty()) {
        return 0;
    }
    return std::strtoll(value->c_str(), nullptr, 10);
}

}  // namespace

TorrentSearchScreen::TorrentSearchScreen(Font* titleFont, Font* bodyFont,
                                         TorrentManager* torrents, ScreenManager* navigator)
    : Screen(930, "Torrent Search"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      torrents_(torrents),
      navigator_(navigator) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);
}

void TorrentSearchScreen::onEnter() {
    mode_ = Mode::Query;
    query_.clear();
    results_.clear();
    focusIndex_ = 0;
    scrollOffset_ = 0;
    message_.clear();
    Screen::onEnter();
}

void TorrentSearchScreen::onExit() {
    cancelSearch();
    Screen::onExit();
}

void TorrentSearchScreen::flash(const std::string& message) {
    flashMessage_ = message;
    flashTimer_ = 2.5f;
}

void TorrentSearchScreen::cancelSearch() {
    if (search_.running()) {
        search_.requestStop();
        // Not waited on: this runs on the UI thread, and the process is in its
        // own group with a SIGKILL escalation already armed.
    }
}

void TorrentSearchScreen::startSearch() {
    if (!torrents_) {
        return;
    }
    const std::string script = torrents_->searchScriptPath();
    if (script.empty()) {
        message_ = "tpb_search.py missing from assets/net/";
        return;
    }
    if (query_.empty()) {
        return;
    }

    cancelSearch();
    lines_.clear();
    consumed_ = 0;
    results_.clear();
    focusIndex_ = 0;
    scrollOffset_ = 0;
    message_.clear();

    const std::vector<std::string> argv = {
        "python3", "-u", script, "--query", query_, "--cat", kCategories[categoryIndex_].code};
    if (!search_.start(argv, torrents_->helperEnv())) {
        message_ = "could not start python3";
        return;
    }
    mode_ = Mode::Searching;
}

void TorrentSearchScreen::consumeOutput() {
    for (; consumed_ < lines_.size(); ++consumed_) {
        const std::string& line = lines_[consumed_];
        if (!jsonline::isObject(line)) {
            continue;  // a traceback or a stray print, not protocol
        }
        const auto kind = jsonline::field(line, "kind");
        if (!kind) {
            continue;
        }
        if (*kind == "error") {
            message_ = jsonline::field(line, "message").value_or("search failed");
            continue;
        }
        if (*kind == "empty") {
            message_ = "No results";
            continue;
        }
        if (*kind != "result") {
            continue;
        }

        Result result;
        result.name = jsonline::field(line, "name").value_or("");
        result.magnet = jsonline::field(line, "magnet").value_or("");
        result.seeders = jsonline::toInt(jsonline::field(line, "seeders"));
        result.sizeBytes = toInt64(jsonline::field(line, "sizeBytes"));
        // The helper emits exactly the strings mediaTypeFromString accepts, so
        // there is no second mapping table here to drift out of sync with it.
        result.route = mediaTypeFromString(jsonline::field(line, "route").value_or("download"));
        if (!result.magnet.empty()) {
            results_.push_back(std::move(result));
        }
    }
}

void TorrentSearchScreen::handleInput(const Input& input) {
    if (mode_ == Mode::Query) {
        if (!input.textChars().empty()) {
            query_ += input.textChars();
        }
        if (input.textBackspace() && !query_.empty()) {
            query_.pop_back();
        }
        for (Action action : input.actions()) {
            if (action == Action::Confirm) {
                startSearch();
            } else if (action == Action::Left) {
                categoryIndex_ = (categoryIndex_ == 0) ? kCategoryCount - 1 : categoryIndex_ - 1;
            } else if (action == Action::Right) {
                categoryIndex_ = (categoryIndex_ + 1) % kCategoryCount;
            }
        }
        return;
    }

    if (mode_ == Mode::Searching) {
        for (Action action : input.actions()) {
            if (action == Action::Back) {
                cancelSearch();
                mode_ = Mode::Query;
                message_ = "Search cancelled";
            }
        }
        return;
    }

    const int count = static_cast<int>(results_.size());
    for (Action action : input.actions()) {
        if (action == Action::Back) {
            mode_ = Mode::Query;  // back to the query field, not out of the screen
            return;
        }
        if (count == 0) {
            continue;
        }
        if (action == Action::Up) {
            focusIndex_ = (focusIndex_ == 0) ? count - 1 : focusIndex_ - 1;
        } else if (action == Action::Down) {
            focusIndex_ = (focusIndex_ + 1) % count;
        } else if (action == Action::Confirm && torrents_) {
            const Result& result = results_[static_cast<std::size_t>(focusIndex_)];
            torrents_->addMagnet(result.magnet, result.route);
            // "Queued", not "Added": addMagnet only hands the magnet to the
            // worker, and the RPC that actually adds it can still fail. The
            // Downloads screen reports the real outcome.
            flash("Queued " + result.name);
        }
    }

    scrollOffset_ = std::clamp(scrollOffset_, focusIndex_ - kVisibleRows + 1, focusIndex_);
    scrollOffset_ = std::max(0, scrollOffset_);
}

void TorrentSearchScreen::update(float dt) {
    if (flashTimer_ > 0.0f) {
        flashTimer_ -= dt;
        if (flashTimer_ <= 0.0f) {
            flashMessage_.clear();
        }
    }

    if (mode_ == Mode::Searching) {
        spinnerTimer_ += dt;
        // Non-blocking drain on the UI thread: poll() never waits.
        search_.poll(lines_);
        consumeOutput();
        if (search_.finished()) {
            consumeOutput();  // pick up anything buffered past the last poll
            mode_ = Mode::Results;
            if (results_.empty() && message_.empty()) {
                message_ = "No results";
            }
        }
    }

    Screen::update(dt);
}

void TorrentSearchScreen::draw(IRenderer& renderer) {
    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    if (titleFont_) {
        titleFont_->draw(renderer, "SEARCH TORRENTS", {64, 40}, kAccent);
    }
    if (!bodyFont_) {
        return;
    }

    // Query field and category filter, visible in every mode so the current
    // search is always identifiable.
    renderer.drawRect(Rect{64, 96, 1152, 52}, mode_ == Mode::Query ? kCardFocused : kCard);
    if (mode_ == Mode::Query) {
        renderer.drawRect(Rect{64, 96, 6, 52}, kAccent);
    }
    const std::string shown = query_ + (mode_ == Mode::Query ? "_" : "");
    bodyFont_->draw(renderer, shown.empty() ? "TYPE A SEARCH" : shown, {88, 108},
                    query_.empty() ? kTextFaint : kTextBright);
    bodyFont_->draw(renderer, std::string("[ ") + kCategories[categoryIndex_].label + " ]",
                    {1010, 108}, kAccent);

    if (mode_ == Mode::Searching) {
        const int frame = static_cast<int>(spinnerTimer_ * 8.0f) % 4;
        bodyFont_->draw(renderer, std::string(kSpinnerFrames[frame]) + " SEARCHING",
                        {64, kRowTop}, kAccent);
        bodyFont_->draw(renderer, "ESC CANCEL", {64, 670}, kTextDim);
        return;
    }

    if (mode_ == Mode::Query) {
        bodyFont_->draw(renderer, "ENTER SEARCH  ·  ←→ CATEGORY  ·  ESC BACK", {64, 670},
                        kTextDim);
        if (!message_.empty()) {
            bodyFont_->draw(renderer, message_, {64, kRowTop}, kTextDim);
        }
        return;
    }

    const int count = static_cast<int>(results_.size());
    if (count == 0) {
        bodyFont_->draw(renderer, message_.empty() ? "No results" : message_, {64, kRowTop},
                        kTextDim);
        bodyFont_->draw(renderer, "ESC EDIT SEARCH", {64, 670}, kTextDim);
        return;
    }

    bodyFont_->draw(renderer, std::to_string(count) + " RESULTS  ·  BY SEEDERS", {64, 152},
                    kTextDim);

    const int first = scrollOffset_;
    const int last = std::min(count, first + kVisibleRows);
    float y = kRowTop;

    for (int i = first; i < last; ++i) {
        const Result& result = results_[static_cast<std::size_t>(i)];
        const bool focused = i == focusIndex_;
        const Rect row{64, y, 1152, kRowHeight};
        renderer.drawRect(row, focused ? kCardFocused : kCard);
        if (focused) {
            renderer.drawRect(Rect{row.x, row.y, 6, row.h}, kAccent);
        }

        std::string name = result.name;
        if (name.size() > 56) {
            name = name.substr(0, 55) + "…";
        }
        bodyFont_->draw(renderer, name, {row.x + 20, row.y + 8},
                        focused ? kTextBright : kTextDim);
        bodyFont_->draw(renderer, humanBytes(result.sizeBytes), {row.x + 800, row.y + 8},
                        kTextDim);
        // Seeder count is the one number worth reading here: it decides whether
        // the download will move at all.
        bodyFont_->draw(renderer, "S " + std::to_string(result.seeders), {row.x + 950, row.y + 8},
                        result.seeders > 0 ? kAccent : kTextFaint);

        y += kRowPitch;
    }

    if (count > last) {
        bodyFont_->draw(renderer, "+" + std::to_string(count - last) + " MORE",
                        {64, kRowTop + kRowPitch * kVisibleRows}, kTextFaint);
    }

    if (!flashMessage_.empty()) {
        bodyFont_->draw(renderer, flashMessage_, {64, 626}, kAccent);
    }
    bodyFont_->draw(renderer, "ENTER DOWNLOAD  ·  ESC EDIT SEARCH", {64, 670}, kTextDim);
}

}  // namespace cyberdeck
