#include "screens/MusicScreen.hpp"

#include "core/Types.hpp"
#include "screens/NowPlayingScreen.hpp"

#include <algorithm>
#include <memory>

namespace cyberdeck {

MusicScreen::MusicScreen(Font* titleFont, Font* bodyFont, MediaLibrary* library, IPlayer* player,
                         ScreenManager* navigator)
    : Screen(710, "Music"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      library_(library),
      player_(player),
      navigator_(navigator) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    focusSlide_.reset(0.0f, 0.0f, 0.28f, Ease::OutCubic);
}

void MusicScreen::rebuild() {
    items_.clear();
    if (library_ && library_->ready()) {
        items_ = library_->db().listByType(MediaType::Music, 500);
    }
    if (focusIndex_ >= static_cast<int>(items_.size())) {
        focusIndex_ = std::max(0, static_cast<int>(items_.size()) - 1);
    }
}

void MusicScreen::onEnter() {
    rebuild();
    focusSlide_.reset(static_cast<float>(focusIndex_), static_cast<float>(focusIndex_), 0.01f,
                      Ease::Linear);
    Screen::onEnter();
}

void MusicScreen::playFocused() {
    if (!player_ || items_.empty() || !navigator_) {
        return;
    }
    player_->setQueue(items_, focusIndex_);
    player_->playFromQueue(false);
    navigator_->push(std::make_unique<NowPlayingScreen>(titleFont_, bodyFont_, player_, library_,
                                                        navigator_));
}

void MusicScreen::handleInput(const Input& input) {
    if (items_.empty()) {
        return;
    }
    for (Action action : input.actions()) {
        if (action == Action::Up || action == Action::Left) {
            focusIndex_ = (focusIndex_ == 0) ? static_cast<int>(items_.size()) - 1
                                             : focusIndex_ - 1;
            focusSlide_.retarget(static_cast<float>(focusIndex_), 0.28f, Ease::OutCubic);
        } else if (action == Action::Down || action == Action::Right) {
            focusIndex_ = (focusIndex_ + 1) % static_cast<int>(items_.size());
            focusSlide_.retarget(static_cast<float>(focusIndex_), 0.28f, Ease::OutCubic);
        } else if (action == Action::Confirm) {
            playFocused();
        }
    }
}

void MusicScreen::update(float dt) {
    focusSlide_.update(dt);
    refreshTimer_ += dt;
    if (refreshTimer_ >= 1.5f) {
        refreshTimer_ = 0.0f;
        rebuild();
    }
    Screen::update(dt);
}

void MusicScreen::draw(IRenderer& renderer) {
    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    if (titleFont_) {
        titleFont_->draw(renderer, "MUSIC", {64, 40}, kAccent);
    }
    if (bodyFont_) {
        bodyFont_->draw(renderer,
                        items_.empty() ? "NO MUSIC IN PI LIB/MUSIC YET"
                                       : (std::to_string(items_.size()) + " TRACKS  ·  ENTER TO PLAY"),
                        {64, 100}, kTextDim);
    }

    const float rowPitch = 48.0f;
    const float listY = 150.0f;
    const int visible = 10;
    const float focusVisual = focusSlide_.value();
    const int start = std::max(
        0, std::min(static_cast<int>(focusVisual) - visible / 2,
                    std::max(0, static_cast<int>(items_.size()) - visible)));

    for (int i = 0; i < visible; ++i) {
        const int index = start + i;
        if (index >= static_cast<int>(items_.size())) {
            break;
        }
        const float dist = std::fabs(static_cast<float>(index) - focusVisual);
        const float glow = std::clamp(1.0f - dist, 0.0f, 1.0f);
        const Rect row{64, listY + i * rowPitch, 900, 42};
        renderer.drawRect(row, lerpColor(kCard, kCardFocused, glow));
        if (glow > 0.05f) {
            renderer.drawRect(Rect{row.x, row.y, 4.0f + 4.0f * glow, row.h},
                              Color{kAccent.r, kAccent.g, kAccent.b, 0.35f + 0.65f * glow});
        }
        if (bodyFont_) {
            bodyFont_->draw(renderer, items_[static_cast<std::size_t>(index)].name,
                            {row.x + 24, row.y + 8},
                            lerpColor(kTextDim, kTextBright, 0.35f + 0.65f * glow));
        }
    }

    if (player_ && bodyFont_) {
        const auto st = player_->status();
        if (!st.path.empty()) {
            renderer.drawRect(Rect{64, 650, 1152, 40}, kCard);
            bodyFont_->draw(renderer,
                            std::string(st.paused ? "PAUSED: " : "PLAYING: ") + st.title,
                            {80, 658}, kAccent);
        }
    }
}

}  // namespace cyberdeck
