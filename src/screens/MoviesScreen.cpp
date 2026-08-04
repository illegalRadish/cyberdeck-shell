#include "screens/MoviesScreen.hpp"

#include "core/Types.hpp"
#include "screens/NowPlayingScreen.hpp"

#include <algorithm>
#include <memory>
#include <unordered_set>

namespace cyberdeck {

MoviesScreen::MoviesScreen(Font* titleFont, Font* bodyFont, MediaLibrary* library,
                           IPlayer* player, ScreenManager* navigator)
    : Screen(720, "Movies"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      library_(library),
      player_(player),
      navigator_(navigator) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
}

const MediaItem* MoviesScreen::itemAt(int index) const {
    if (index < 0) {
        return nullptr;
    }
    if (index < static_cast<int>(continueItems_.size())) {
        return &continueItems_[static_cast<std::size_t>(index)];
    }
    index -= static_cast<int>(continueItems_.size());
    if (index < static_cast<int>(movies_.size())) {
        return &movies_[static_cast<std::size_t>(index)];
    }
    return nullptr;
}

void MoviesScreen::rebuild() {
    continueItems_.clear();
    movies_.clear();
    if (!library_ || !library_->ready()) {
        return;
    }

    continueItems_ = library_->db().listContinueWatching(8);
    auto allMovies = library_->db().listByType(MediaType::Movie, 400);
    auto videos = library_->db().listByType(MediaType::Video, 200);

    std::unordered_set<std::string> contPaths;
    for (const auto& item : continueItems_) {
        contPaths.insert(item.path);
    }

    for (auto& item : allMovies) {
        if (!contPaths.count(item.path)) {
            movies_.push_back(std::move(item));
        }
    }
    for (auto& item : videos) {
        if (!contPaths.count(item.path)) {
            movies_.push_back(std::move(item));
        }
    }

    if (focusIndex_ >= totalCount()) {
        focusIndex_ = std::max(0, totalCount() - 1);
    }
}

void MoviesScreen::onEnter() {
    rebuild();
    Screen::onEnter();
}

void MoviesScreen::openFocused(bool resume) {
    const MediaItem* item = itemAt(focusIndex_);
    if (!item || !player_ || !navigator_) {
        return;
    }

    std::vector<MediaItem> queue = movies_;
    // Prefer playing from full movie list; if continue item, put it first.
    if (focusIndex_ < static_cast<int>(continueItems_.size())) {
        queue.insert(queue.begin(), *item);
        player_->setQueue(queue, 0);
    } else {
        const int movieIndex = focusIndex_ - static_cast<int>(continueItems_.size());
        player_->setQueue(movies_, movieIndex);
    }

    player_->playFromQueue(true);

    if (resume && library_ && library_->ready()) {
        const double pos = library_->db().loadProgress(item->path);
        if (pos > 5.0) {
            player_->seekAbsolute(pos);
        }
    }

    navigator_->push(std::make_unique<NowPlayingScreen>(titleFont_, bodyFont_, player_, library_,
                                                        navigator_));
}

void MoviesScreen::handleInput(const Input& input) {
    if (totalCount() <= 0) {
        return;
    }
    for (Action action : input.actions()) {
        if (action == Action::Left || action == Action::Up) {
            focusIndex_ = (focusIndex_ == 0) ? totalCount() - 1 : focusIndex_ - 1;
        } else if (action == Action::Right || action == Action::Down) {
            focusIndex_ = (focusIndex_ + 1) % totalCount();
        } else if (action == Action::Confirm) {
            const bool resume = focusIndex_ < static_cast<int>(continueItems_.size());
            openFocused(resume);
        }
    }
}

void MoviesScreen::update(float dt) {
    refreshTimer_ += dt;
    if (refreshTimer_ >= 1.5f) {
        refreshTimer_ = 0.0f;
        rebuild();
    }
    Screen::update(dt);
}

void MoviesScreen::draw(IRenderer& renderer) {
    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    if (titleFont_) {
        titleFont_->draw(renderer, "MOVIES", {64, 36}, kAccent);
    }

    float y = 100.0f;
    int index = 0;

    auto drawRow = [&](const MediaItem& item, bool isContinue) {
        const bool focused = index == focusIndex_;
        const Rect row{64, y, 1150, 56};
        renderer.drawRect(row, focused ? kCardFocused : kCard);
        if (focused) {
            renderer.drawRect(Rect{row.x, row.y, 6, row.h}, kAccent);
        }
        if (bodyFont_) {
            std::string label = item.name;
            if (isContinue && library_ && library_->ready()) {
                label += "  ·  CONTINUE";
            }
            bodyFont_->draw(renderer, label, {row.x + 24, row.y + 14},
                            focused ? kTextBright : kTextDim);
        }
        y += 64.0f;
        ++index;
    };

    if (bodyFont_ && !continueItems_.empty()) {
        bodyFont_->draw(renderer, "CONTINUE WATCHING", {64, y}, kAccent);
        y += 36.0f;
        for (const auto& item : continueItems_) {
            if (y > 620) {
                break;
            }
            drawRow(item, true);
        }
        y += 12.0f;
    }

    if (bodyFont_) {
        bodyFont_->draw(renderer, "LIBRARY", {64, y}, kAccent);
        y += 36.0f;
    }

    for (const auto& item : movies_) {
        if (y > 640) {
            break;
        }
        drawRow(item, false);
    }

    if (totalCount() == 0 && bodyFont_) {
        bodyFont_->draw(renderer, "NO MOVIES IN PI LIB/MOVIES YET", {64, 180}, kTextDim);
    }

    if (bodyFont_) {
        bodyFont_->draw(renderer, "ENTER PLAY/RESUME  ·  ESC BACK", {64, 670}, kTextDim);
    }
}

}  // namespace cyberdeck
