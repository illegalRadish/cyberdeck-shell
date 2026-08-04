#include "screens/SettingsScreen.hpp"

#include "core/Types.hpp"
#include "screens/AiAssetsScreen.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace cyberdeck {

namespace {

std::string humanBytes(std::int64_t bytes) {
    char buf[48];
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0) {
        std::snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f MB", mb);
    }
    return buf;
}

}  // namespace

SettingsScreen::SettingsScreen(Font* titleFont, Font* bodyFont, MediaLibrary* library,
                               SystemSampler* sampler, ScreenManager* navigator,
                               AiAssets* ai)
    : Screen(810, "Settings"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      library_(library),
      sampler_(sampler),
      navigator_(navigator),
      ai_(ai) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
}

void SettingsScreen::onEnter() {
    focusIndex_ = 0;
    Screen::onEnter();
}

std::string SettingsScreen::labelFor(Item item) const {
    switch (item) {
        case Item::MediaRoot:
            return "MEDIA ROOT";
        case Item::Rescan:
            return "RESCAN PI LIB";
        case Item::OpenLibrary:
            return "OPEN PI LIB FOLDER";
        case Item::AiAssetsItem:
            return "AI ASSETS";
        case Item::Network:
            return "NETWORK STATUS";
        case Item::About:
            return "ABOUT CYBERDECK";
        case Item::QuitApp:
            return "QUIT TO OS";
        default:
            return "";
    }
}

std::string SettingsScreen::detailFor(Item item) const {
    switch (item) {
        case Item::MediaRoot:
            if (library_ && library_->rootFound()) {
                return library_->root().path;
            }
            return "PI LIB not found";
        case Item::Rescan:
            if (library_ && library_->ready()) {
                const auto p = library_->scanProgress();
                if (p.running) {
                    return "Indexing… " + std::to_string(p.filesSeen) + " files";
                }
                return std::to_string(library_->db().countAll()) + " items indexed";
            }
            return "Library unavailable";
        case Item::OpenLibrary:
            return "Reveal media folder in Finder / file manager";
        case Item::AiAssetsItem: {
            if (!ai_) {
                return "Unavailable";
            }
            // Called once per row every frame, and Font caches a texture per
            // distinct string — so whole percent only, never a byte counter.
            const AiAssetsStatus s = ai_->status();
            if (s.running) {
                return s.verifying ? "Verifying…"
                                   : "Downloading " + std::to_string(s.percent) + "%";
            }
            if (s.ready) {
                return "Ready  ·  " + std::to_string(s.presentCount) + " of " +
                       std::to_string(s.totalCount) + " installed";
            }
            std::string detail = std::to_string(s.presentCount) + " of " +
                                 std::to_string(s.totalCount) + " present";
            if (s.totalMissingBytes > 0) {
                detail += "  ·  " + humanBytes(s.totalMissingBytes) + " to download";
            }
            return detail;
        }
        case Item::Network:
            if (sampler_) {
                return sampler_->wifiString() + "  ·  " + sampler_->clockString();
            }
            return "Unknown";
        case Item::About:
            return "Custom PI Shell  ·  SDL2 / OpenGL / libmpv";
        case Item::QuitApp:
            return "Close Cyberdeck";
        default:
            return "";
    }
}

void SettingsScreen::openPath(const std::string& path) {
    if (path.empty()) {
        flashMessage_ = "No media root to open";
        flashTimer_ = 2.5f;
        return;
    }
#if defined(__APPLE__)
    const std::string cmd = "open \"" + path + "\"";
#else
    const std::string cmd = "xdg-open \"" + path + "\" >/dev/null 2>&1 &";
#endif
    std::system(cmd.c_str());
    flashMessage_ = "Opened media folder";
    flashTimer_ = 2.0f;
}

void SettingsScreen::activate(Item item) {
    switch (item) {
        case Item::MediaRoot:
            flashMessage_ = detailFor(Item::MediaRoot);
            flashTimer_ = 3.0f;
            break;
        case Item::Rescan:
            if (library_ && library_->ready()) {
                library_->startScan();
                flashMessage_ = "Library scan started";
            } else {
                flashMessage_ = "Cannot scan — PI LIB missing";
            }
            flashTimer_ = 2.5f;
            break;
        case Item::OpenLibrary:
            if (library_ && library_->rootFound()) {
                openPath(library_->root().path);
            } else {
                flashMessage_ = "PI LIB not found";
                flashTimer_ = 2.5f;
            }
            break;
        case Item::AiAssetsItem:
            if (ai_ && navigator_) {
                navigator_->push(std::make_unique<AiAssetsScreen>(titleFont_, bodyFont_, ai_,
                                                                 navigator_));
            } else {
                flashMessage_ = "AI assets unavailable";
                flashTimer_ = 2.5f;
            }
            break;
        case Item::Network:
            flashMessage_ = detailFor(Item::Network);
            flashTimer_ = 3.0f;
            break;
        case Item::About:
            flashMessage_ = "Cyberdeck OS shell  ·  Phase 5";
            flashTimer_ = 3.0f;
            break;
        case Item::QuitApp:
            flashMessage_ = "Quitting…";
            flashTimer_ = 1.0f;
            break;
        default:
            break;
    }
}

void SettingsScreen::handleInput(const Input& input) {
    const int count = static_cast<int>(Item::Count);
    for (Action action : input.actions()) {
        if (action == Action::Up || action == Action::Left) {
            focusIndex_ = (focusIndex_ == 0) ? count - 1 : focusIndex_ - 1;
        } else if (action == Action::Down || action == Action::Right) {
            focusIndex_ = (focusIndex_ + 1) % count;
        } else if (action == Action::Confirm) {
            activate(static_cast<Item>(focusIndex_));
            if (static_cast<Item>(focusIndex_) == Item::QuitApp && navigator_) {
                navigator_->requestQuit();
            }
        }
    }
}

void SettingsScreen::update(float dt) {
    if (flashTimer_ > 0.0f) {
        flashTimer_ -= dt;
        if (flashTimer_ <= 0.0f) {
            flashMessage_.clear();
        }
    }
    Screen::update(dt);
}

void SettingsScreen::draw(IRenderer& renderer) {
    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    if (titleFont_) {
        titleFont_->draw(renderer, "SETTINGS", {64, 40}, kAccent);
    }

    // Seven rows at the old 82px pitch would run to y=682, through the flash
    // line and the hint. Compressed pitch puts the last row's bottom at 590.
    // An eighth row will not fit — that one needs a scrolling list.
    const int count = static_cast<int>(Item::Count);
    float y = 108.0f;
    for (int i = 0; i < count; ++i) {
        const Item item = static_cast<Item>(i);
        const bool focused = i == focusIndex_;
        const Rect row{64, y, 1150, 62};
        renderer.drawRect(row, focused ? kCardFocused : kCard);
        if (focused) {
            renderer.drawRect(Rect{row.x, row.y, 6, row.h}, kAccent);
        }
        if (bodyFont_) {
            bodyFont_->draw(renderer, labelFor(item), {row.x + 24, row.y + 6},
                            focused ? kTextBright : kTextDim);
            bodyFont_->draw(renderer, detailFor(item), {row.x + 24, row.y + 32},
                            focused ? kAccent : kTextDim);
        }
        y += 70.0f;
    }

    if (bodyFont_) {
        if (!flashMessage_.empty()) {
            bodyFont_->draw(renderer, flashMessage_, {64, 622}, kAccent);
        }
        bodyFont_->draw(renderer, "ENTER SELECT  ·  ESC BACK", {64, 664}, kTextDim);
    }
}

}  // namespace cyberdeck
