#include "screens/DownloadsScreen.hpp"

#include "core/Types.hpp"
#include "screens/TorrentSearchScreen.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace cyberdeck {

namespace {

constexpr float kRowTop = 132.0f;
constexpr float kRowPitch = 52.0f;
constexpr float kRowHeight = 46.0f;
constexpr int kVisibleRows = 7;  // the band between the header and the actions
constexpr float kActionTop = 508.0f;
constexpr float kActionPitch = 52.0f;
constexpr float kActionHeight = 44.0f;

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

// Whole MB/s, or whole KB/s below that. Kept coarse on purpose: every distinct
// string here becomes its own cached Font texture, and a rate rendered to one
// decimal would mint a new one nearly every frame.
std::string humanRate(std::int64_t bytesPerSecond) {
    if (bytesPerSecond <= 0) {
        return "";
    }
    char buf[32];
    const double mb = static_cast<double>(bytesPerSecond) / (1024.0 * 1024.0);
    if (mb >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.0f MB/S", mb);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f KB/S",
                      static_cast<double>(bytesPerSecond) / 1024.0);
    }
    return buf;
}

std::string routeLabel(MediaType route) {
    switch (route) {
        case MediaType::Music:  return "MUSIC";
        case MediaType::Movie:  return "MOVIES";
        case MediaType::TvShow: return "TV";
        case MediaType::Photo:  return "PHOTOS";
        case MediaType::Video:  return "VIDEOS";
        case MediaType::Book:   return "BOOKS";
        case MediaType::Rom:    return "ROMS";
        default:                return "DOWNLOADS";
    }
}

}  // namespace

DownloadsScreen::DownloadsScreen(Font* titleFont, Font* bodyFont, TorrentManager* torrents,
                                 ScreenManager* navigator)
    : Screen(920, "Downloads"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      torrents_(torrents),
      navigator_(navigator) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);
}

int DownloadsScreen::torrentCount() const {
    return static_cast<int>(status_.torrents.size());
}

int DownloadsScreen::actionCount() const {
    return status_.engineInstalled ? 2 : 3;
}

DownloadsScreen::ActionRow DownloadsScreen::actionAt(int index) const {
    if (!status_.engineInstalled) {
        return static_cast<ActionRow>(index);
    }
    // Install is not offered, so the remaining two shift up by one.
    return static_cast<ActionRow>(index + 1);
}

void DownloadsScreen::onEnter() {
    focusIndex_ = 0;
    scrollOffset_ = 0;
    mode_ = Mode::List;
    magnetInput_.clear();
    if (torrents_) {
        status_ = torrents_->status();
    }
    Screen::onEnter();
}

void DownloadsScreen::flash(const std::string& message) {
    flashMessage_ = message;
    flashTimer_ = 2.5f;
}

void DownloadsScreen::activateFocused() {
    if (!torrents_) {
        return;
    }

    if (focusIndex_ < torrentCount()) {
        const TorrentState& torrent = status_.torrents[static_cast<std::size_t>(focusIndex_)];
        if (rowAction_ == RowAction::Remove) {
            // delete-local-data is never passed: by the time a torrent is
            // removed its payload may already be filed into the library, and
            // deleting it there would destroy what was just downloaded.
            torrents_->remove(torrent.id);
            flash("Removed " + torrent.name + " (files kept)");
        } else if (torrent.status == "stopped") {
            torrents_->resume(torrent.id);
            flash("Resumed " + torrent.name);
        } else {
            torrents_->pause(torrent.id);
            flash("Paused " + torrent.name);
        }
        return;
    }

    switch (actionAt(focusIndex_ - torrentCount())) {
        case ActionRow::Install:
            if (torrents_->isInstalling()) {
                flash("Install already running");
            } else {
                torrents_->startInstall();
                flash("Installing torrent engine — this takes a few minutes");
            }
            break;
        case ActionRow::Search:
            if (navigator_) {
                navigator_->push(std::make_unique<TorrentSearchScreen>(titleFont_, bodyFont_,
                                                                      torrents_, navigator_));
            }
            break;
        case ActionRow::AddMagnet:
            mode_ = Mode::MagnetEntry;
            magnetInput_.clear();
            break;
    }
}

void DownloadsScreen::handleInput(const Input& input) {
    if (mode_ == Mode::MagnetEntry) {
        if (!input.textChars().empty()) {
            magnetInput_ += input.textChars();
        }
        if (input.textBackspace() && !magnetInput_.empty()) {
            magnetInput_.pop_back();
        }
        for (Action action : input.actions()) {
            if (action == Action::Back) {
                mode_ = Mode::List;
                magnetInput_.clear();
            } else if (action == Action::Confirm) {
                if (magnetInput_.rfind("magnet:", 0) == 0) {
                    // No route is forced: the filer infers one from the name and
                    // payload, which is all we know about a hand-pasted magnet.
                    torrents_->addMagnet(magnetInput_, MediaType::Download);
                    flash("Magnet queued");
                    mode_ = Mode::List;
                    magnetInput_.clear();
                } else {
                    flash("That is not a magnet link");
                }
            }
        }
        return;
    }

    const int rows = torrentCount() + actionCount();
    for (Action action : input.actions()) {
        if (action == Action::Up) {
            focusIndex_ = (focusIndex_ == 0) ? rows - 1 : focusIndex_ - 1;
        } else if (action == Action::Down) {
            focusIndex_ = (focusIndex_ + 1) % std::max(1, rows);
        } else if (action == Action::Left) {
            rowAction_ = RowAction::Toggle;
        } else if (action == Action::Right) {
            rowAction_ = RowAction::Remove;
        } else if (action == Action::Confirm) {
            activateFocused();
        }
    }

    // Keep the focused torrent inside the visible band.
    if (focusIndex_ < torrentCount()) {
        scrollOffset_ = std::clamp(scrollOffset_, focusIndex_ - kVisibleRows + 1, focusIndex_);
        scrollOffset_ = std::max(0, scrollOffset_);
    }
}

void DownloadsScreen::update(float dt) {
    if (flashTimer_ > 0.0f) {
        flashTimer_ -= dt;
        if (flashTimer_ <= 0.0f) {
            flashMessage_.clear();
        }
    }
    refreshTimer_ += dt;
    if (refreshTimer_ >= 0.5f) {
        refreshTimer_ = 0.0f;
        if (torrents_) {
            status_ = torrents_->status();
        }
        // The list shrinks when a torrent is removed, so the focus index has to
        // be re-clamped or Confirm would act on a stale row.
        const int rows = torrentCount() + actionCount();
        focusIndex_ = std::clamp(focusIndex_, 0, std::max(0, rows - 1));
    }
    Screen::update(dt);
}

void DownloadsScreen::draw(IRenderer& renderer) {
    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    if (titleFont_) {
        titleFont_->draw(renderer, "DOWNLOADS", {64, 40}, kAccent);
    }

    if (bodyFont_) {
        std::string header;
        if (!status_.helpersFound) {
            header = "HELPER SCRIPTS MISSING FROM assets/net/";
        } else if (!status_.engineInstalled) {
            header = "TORRENT ENGINE NOT INSTALLED";
        } else if (!status_.daemonRunning) {
            header = "ENGINE INSTALLED  ·  DAEMON NOT RUNNING";
        } else {
            header = std::to_string(torrentCount()) + " ACTIVE";
        }
        bodyFont_->draw(renderer, header, {64, 96}, kTextDim);
    }

    const int count = torrentCount();
    if (count == 0 && bodyFont_) {
        bodyFont_->draw(renderer, "Nothing downloading.", {64, kRowTop + 8}, kTextFaint);
        bodyFont_->draw(renderer, "Search, paste a magnet, or drop a .torrent into",
                        {64, kRowTop + 44}, kTextFaint);
        bodyFont_->draw(renderer, "PI LIB/Downloads/.watch/", {64, kRowTop + 76}, kTextFaint);
    }

    const int first = scrollOffset_;
    const int last = std::min(count, first + kVisibleRows);
    float y = kRowTop;

    for (int i = first; i < last; ++i) {
        const TorrentState& torrent = status_.torrents[static_cast<std::size_t>(i)];
        const bool focused = i == focusIndex_;
        const Rect row{64, y, 1152, kRowHeight};
        renderer.drawRect(row, focused ? kCardFocused : kCard);
        if (focused) {
            renderer.drawRect(Rect{row.x, row.y, 6, row.h}, kAccent);
        }

        const float textY = row.y + 9.0f;
        if (bodyFont_) {
            std::string name = torrent.name;
            if (name.size() > 44) {
                name = name.substr(0, 43) + "…";
            }
            bodyFont_->draw(renderer, name, {row.x + 20, textY},
                            focused ? kTextBright : kTextDim);

            // Low-cardinality by construction: whole percent, or a word.
            std::string state;
            Color stateColor = kTextDim;
            if (!torrent.error.empty()) {
                state = "ERROR";
                stateColor = kTextFaint;
            } else if (torrent.sizeBytes == 0 && torrent.percent == 0) {
                // A magnet carries only a hash; until peers supply the metadata
                // there is no file list and no size, so 0% here means "still
                // finding out what this is", not "downloading slowly".
                state = "METADATA";
                stateColor = kTextDim;
            } else if (torrent.filed) {
                state = "FILED";
                stateColor = kAccent;
            } else if (torrent.percent >= 100) {
                state = "DONE";
                stateColor = kAccent;
            } else {
                state = std::to_string(torrent.percent) + "%";
                stateColor = kTextBright;
            }
            bodyFont_->draw(renderer, state, {row.x + 600, textY}, stateColor);

            if (!torrent.error.empty()) {
                // Takes the rate/route slot: when a torrent is failing, why it
                // is failing is the only thing on this row worth reading.
                std::string reason = torrent.error;
                if (reason.size() > 28) {
                    reason = reason.substr(0, 27) + "…";
                }
                bodyFont_->draw(renderer, reason, {row.x + 690, textY}, kTextFaint);
            } else {
                const std::string rate = humanRate(torrent.rateBytes);
                if (!rate.empty()) {
                    bodyFont_->draw(renderer, rate, {row.x + 690, textY}, kTextDim);
                }
                bodyFont_->draw(renderer, routeLabel(torrent.route), {row.x + 810, textY},
                                kTextFaint);
            }

            if (focused) {
                bodyFont_->draw(renderer,
                                rowAction_ == RowAction::Remove
                                    ? "REMOVE"
                                    : (torrent.status == "stopped" ? "RESUME" : "PAUSE"),
                                {row.x + 470, textY}, kAccent);
            }
        }

        // Determinate bar drawn as rects — the established progress idiom.
        const Rect track{row.x + 950, row.y + (kRowHeight - 12.0f) * 0.5f, 180, 12};
        renderer.drawRect(track, kBgPanel);
        const float frac = std::clamp(static_cast<float>(torrent.percent) / 100.0f, 0.0f, 1.0f);
        renderer.drawRect(Rect{track.x, track.y, track.w * frac, track.h}, kAccent);

        y += kRowPitch;
    }

    if (count > last && bodyFont_) {
        bodyFont_->draw(renderer, "+" + std::to_string(count - last) + " MORE",
                        {64, kRowTop + kRowPitch * kVisibleRows}, kTextFaint);
    }

    for (int i = 0; i < actionCount(); ++i) {
        const Rect row{64.0f, kActionTop + kActionPitch * static_cast<float>(i), 1152.0f,
                       kActionHeight};
        const bool focused = (i + count) == focusIndex_;
        renderer.drawRect(row, focused ? kCardFocused : kCard);
        if (focused) {
            renderer.drawRect(Rect{row.x, row.y, 6, row.h}, kAccent);
        }
        if (!bodyFont_) {
            continue;
        }
        std::string label;
        switch (actionAt(i)) {
            case ActionRow::Install:
                label = status_.installing ? "INSTALLING…" : "INSTALL TORRENT ENGINE";
                break;
            case ActionRow::Search:
                label = "SEARCH TORRENTS";
                break;
            case ActionRow::AddMagnet:
                label = "ADD MAGNET LINK";
                break;
        }
        bodyFont_->draw(renderer, label, {row.x + 24, row.y + 8},
                        focused ? kTextBright : kTextDim);
    }

    if (!bodyFont_) {
        return;
    }

    if (mode_ == Mode::MagnetEntry) {
        // Full-width overlay so a long magnet is readable while it is typed.
        renderer.drawRect(Rect{0, 560, 1280, 160}, kBgPanel);
        renderer.drawRect(Rect{0, 560, 1280, 4}, kAccent);
        bodyFont_->draw(renderer, "PASTE MAGNET LINK", {64, 578}, kAccent);
        // Show the tail: a magnet's leading "magnet:?xt=urn:btih:" is identical
        // for every link, so the end is the part worth seeing while typing.
        std::string shown = magnetInput_;
        if (shown.size() > 68) {
            shown = "…" + shown.substr(shown.size() - 67);
        }
        bodyFont_->draw(renderer, shown + "_", {64, 616}, kTextBright);
        bodyFont_->draw(renderer, "ENTER ADD  ·  ESC CANCEL", {64, 664}, kTextDim);
        return;
    }

    if (!flashMessage_.empty()) {
        bodyFont_->draw(renderer, flashMessage_, {64, 626}, kAccent);
    } else if (!status_.message.empty()) {
        bodyFont_->draw(renderer, status_.message, {64, 626}, kTextDim);
    }
    bodyFont_->draw(renderer, "ENTER SELECT  ·  ←→ PAUSE/REMOVE  ·  ESC BACK", {64, 670},
                    kTextDim);
}

}  // namespace cyberdeck
