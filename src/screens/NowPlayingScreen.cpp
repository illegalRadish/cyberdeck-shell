#include "screens/NowPlayingScreen.hpp"

#include "core/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cyberdeck {

namespace {

std::string formatTime(double seconds) {
    if (seconds < 0.0 || !std::isfinite(seconds)) {
        return "0:00";
    }
    const int total = static_cast<int>(seconds);
    const int m = total / 60;
    const int s = total % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

void drawVideoFullscreen(IRenderer& renderer, IPlayer& player) {
    renderer.drawTextureId(Rect{0, 0, 1280, 720}, player.videoTexture(), Color{1, 1, 1, 1},
                           Vec2{0.0f, 1.0f}, Vec2{1.0f, 0.0f});
}

void drawPausedOverlay(IRenderer& renderer, Font* titleFont, Font* bodyFont,
                       const PlayerStatus& st, float fade) {
    if (fade <= 0.01f) {
        return;
    }
    const Color plate = Color{10 / 255.0f, 18 / 255.0f, 12 / 255.0f, 0.92f * fade};
    renderer.drawRect(Rect{0, 520, 1280, 200}, plate);

    if (titleFont) {
        Color c = kTextBright;
        c.a = fade;
        titleFont->draw(renderer, st.title.empty() ? "PAUSED" : st.title, {64, 560}, c);
    }

    const float progress =
        (st.durationSec > 0.0) ? static_cast<float>(st.positionSec / st.durationSec) : 0.0f;
    renderer.drawRect(Rect{64, 630, 1152, 12}, Color{0.05f, 0.12f, 0.06f, fade});
    renderer.drawRect(Rect{64, 630, 1152.0f * std::clamp(progress, 0.0f, 1.0f), 12},
                      Color{kAccent.r, kAccent.g, kAccent.b, fade});

    if (bodyFont) {
        Color dim = kTextDim;
        dim.a = fade;
        const std::string times =
            formatTime(st.positionSec) + " / " + formatTime(st.durationSec);
        bodyFont->draw(renderer, times, {64, 655}, dim);
        bodyFont->draw(renderer, "PAUSED  ·  ENTER RESUME  ·  SEEK  ·  ESC BACK", {420, 655},
                       dim);
    }
}

}  // namespace

NowPlayingScreen::NowPlayingScreen(Font* titleFont, Font* bodyFont, IPlayer* player,
                                   MediaLibrary* library, ScreenManager* navigator)
    : Screen(700, "NowPlaying"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      player_(player),
      library_(library),
      navigator_(navigator) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    overlayFade_.reset(0.0f, 0.0f, 0.35f, Ease::InOutSine);
}

void NowPlayingScreen::onEnter() {
    Screen::onEnter();
}

void NowPlayingScreen::persistProgress() {
    if (!player_ || !library_ || !library_->ready()) {
        return;
    }
    const auto st = player_->status();
    if (st.path.empty()) {
        return;
    }
    library_->db().saveProgress(st.path, st.positionSec, st.durationSec);
}

void NowPlayingScreen::loadCoverArt(const std::string& path) {
    coverPath_ = path;
    coverArt_.destroy();
    if (!library_ || !library_->ready() || path.empty()) {
        return;
    }
    const auto item = library_->db().findByPath(path);
    if (item && !item->thumbnailPath.empty()) {
        coverArt_.loadFromFile(item->thumbnailPath);
    }
}

void NowPlayingScreen::onExit() {
    persistProgress();
    Screen::onExit();
}

void NowPlayingScreen::handleInput(const Input& input) {
    if (!player_) {
        return;
    }
    for (Action action : input.actions()) {
        if (action == Action::Confirm) {
            player_->togglePause();
        } else if (action == Action::Left) {
            player_->seekRelative(-10.0);
        } else if (action == Action::Right) {
            player_->seekRelative(10.0);
        } else if (action == Action::Up) {
            player_->playPrevious();
        } else if (action == Action::Down) {
            player_->playNext();
        }
    }
}

void NowPlayingScreen::update(float dt) {
    if (player_) {
        player_->update();
        if (player_->status().path != coverPath_) {
            loadCoverArt(player_->status().path);
        }
        if (player_->status().hasVideo) {
            player_->renderVideoFrame(1280, 720);
        }

        const bool paused = player_->status().paused;
        if (paused != wasPaused_) {
            overlayFade_.retarget(paused ? 1.0f : 0.0f, 0.38f, Ease::InOutSine);
            wasPaused_ = paused;
        }
    }
    overlayFade_.update(dt);

    saveTimer_ += dt;
    if (saveTimer_ >= 5.0f) {
        saveTimer_ = 0.0f;
        persistProgress();
    }
    Screen::update(dt);
}

void NowPlayingScreen::draw(IRenderer& renderer) {
    const auto st = player_ ? player_->status() : PlayerStatus{};
    const bool videoReady = st.hasVideo && player_ && player_->videoTexture() != 0;

    if (videoReady) {
        drawVideoFullscreen(renderer, *player_);
        drawPausedOverlay(renderer, titleFont_, bodyFont_, st, overlayFade_.value());
        return;
    }

    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    if (coverArt_.valid()) {
        renderer.drawTexture(Rect{440, 140, 400, 400}, coverArt_, Color{1, 1, 1, 1});
    } else {
        renderer.drawRect(Rect{440, 140, 400, 400}, kCard);
        if (bodyFont_) {
            bodyFont_->draw(renderer, "NOW PLAYING", {520, 300}, kAccent);
        }
    }

    if (titleFont_) {
        titleFont_->draw(renderer, st.title.empty() ? "NO MEDIA" : st.title, {64, 560},
                         kTextBright);
    }

    const float progress =
        (st.durationSec > 0.0) ? static_cast<float>(st.positionSec / st.durationSec) : 0.0f;
    renderer.drawRect(Rect{64, 630, 1152, 12}, Color::fromBytes(12, 28, 16));
    renderer.drawRect(Rect{64, 630, 1152.0f * std::clamp(progress, 0.0f, 1.0f), 12}, kAccent);

    if (bodyFont_) {
        const std::string times =
            formatTime(st.positionSec) + " / " + formatTime(st.durationSec);
        bodyFont_->draw(renderer, times, {64, 655}, kTextDim);
        bodyFont_->draw(renderer,
                        st.paused ? "PAUSED  ·  ENTER RESUME  ·  SEEK  ·  ESC"
                                  : "PLAYING  ·  ENTER PAUSE  ·  SEEK  ·  ESC",
                        {400, 655}, kTextDim);
    }
}

}  // namespace cyberdeck
