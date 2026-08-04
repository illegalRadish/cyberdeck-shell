#include "screens/LibraryBrowserScreen.hpp"

#include "core/Types.hpp"
#include "ui/AsciiSpinner.hpp"
#include "ui/Label.hpp"

#include <algorithm>
#include <memory>

namespace cyberdeck {

namespace {

constexpr NodeId kListBase = 5000;

SpinnerMesh meshForType(MediaType type) {
    switch (type) {
        case MediaType::Photo:  return SpinnerMesh::Camera;
        case MediaType::Video:  return SpinnerMesh::Video;
        case MediaType::Music:  return SpinnerMesh::Music;
        case MediaType::Movie:  return SpinnerMesh::Film;
        case MediaType::TvShow: return SpinnerMesh::Tv;
        default:                return SpinnerMesh::Logo;
    }
}

}  // namespace

LibraryBrowserScreen::LibraryBrowserScreen(NodeId id, std::string title, MediaType type,
                                           Font* titleFont, Font* bodyFont,
                                           MediaLibrary* library, bool showAll)
    : Screen(id, title),
      type_(type),
      showAll_(showAll),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      library_(library) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);

    auto heading = std::make_unique<Label>(id + 1, std::move(title), titleFont_);
    heading->bounds() = Rect{64, 36, 800, 52};
    heading->setColor(kTextBright);
    heading->setAlignLeft(true);
    addChild(std::move(heading));

    auto hint = std::make_unique<Label>(id + 2, "ESC TO GO BACK", bodyFont_);
    hint->bounds() = Rect{64, 668, 400, 28};
    hint->setColor(kTextDim);
    hint->setAlignLeft(true);
    addChild(std::move(hint));

    auto spinner = std::make_unique<AsciiSpinner>(id + 3, bodyFont_);
    spinner->bounds() = Rect{876, 190, 308, 240};
    spinner_ = spinner.get();
    addChild(std::move(spinner));
}

void LibraryBrowserScreen::rebuildList() {
    items_.clear();
    if (library_ && library_->ready()) {
        items_ = showAll_ ? library_->db().listAll(400) : library_->db().listByType(type_, 400);
    }
    if (focusIndex_ >= static_cast<int>(items_.size())) {
        focusIndex_ = std::max(0, static_cast<int>(items_.size()) - 1);
    }

    if (!library_ || !library_->rootFound()) {
        status_ = "PI LIB not found — connect the drive named PI LIB";
    } else if (items_.empty()) {
        const auto progress = library_->scanProgress();
        if (progress.running) {
            status_ = "Scanning library…";
        } else {
            status_ = "No items in this category yet";
        }
    } else {
        status_ = std::to_string(items_.size()) + " items";
    }
    loadFocusedThumb();
}

void LibraryBrowserScreen::loadFocusedThumb() {
    thumb_.destroy();
    thumbSource_.clear();

    // Preview state: spin the category emblem while browsing, swap to the focused
    // item's type emblem when it has no artwork, and yield entirely to a real thumbnail.
    auto showSpinner = [this](SpinnerMesh mesh) {
        if (spinner_) {
            spinner_->setMesh(mesh);
            spinner_->setVisible(true);
        }
    };

    if (items_.empty() || focusIndex_ < 0 ||
        focusIndex_ >= static_cast<int>(items_.size())) {
        showSpinner(meshForType(type_));
        return;
    }
    const auto& item = items_[static_cast<std::size_t>(focusIndex_)];
    if (!item.thumbnailPath.empty() && thumb_.loadFromFile(item.thumbnailPath)) {
        thumbSource_ = item.thumbnailPath;
        if (spinner_) {
            spinner_->setVisible(false);
        }
        return;
    }
    showSpinner(meshForType(item.type));
}

void LibraryBrowserScreen::onEnter() {
    rebuildList();
    Screen::onEnter();
}

void LibraryBrowserScreen::handleInput(const Input& input) {
    if (items_.empty()) {
        return;
    }
    for (Action action : input.actions()) {
        if (action == Action::Up || action == Action::Left) {
            focusIndex_ = (focusIndex_ == 0) ? static_cast<int>(items_.size()) - 1
                                             : focusIndex_ - 1;
            loadFocusedThumb();
        } else if (action == Action::Down || action == Action::Right) {
            focusIndex_ = (focusIndex_ + 1) % static_cast<int>(items_.size());
            loadFocusedThumb();
        }
    }
}

void LibraryBrowserScreen::update(float dt) {
    refreshTimer_ += dt;
    if (refreshTimer_ >= 1.0f) {
        refreshTimer_ = 0.0f;
        const int before = static_cast<int>(items_.size());
        rebuildList();
        (void)before;
    }
    Screen::update(dt);
}

void LibraryBrowserScreen::draw(IRenderer& renderer) {
    // Panel chrome first so the spinner child (drawn by Screen::draw) lands on top of it.
    const Rect preview{860, 150, 340, 420};
    renderer.drawRect(preview, modulate(Color::fromBytes(8, 18, 10)));
    renderer.drawRect(Rect{preview.x, preview.y, preview.w, 4.0f}, modulate(kAccent));

    Screen::draw(renderer);

    if (bodyFont_) {
        bodyFont_->draw(renderer, status_, {64, 100},
                        modulate(kTextDim));
    }

    const float listX = 64.0f;
    const float listY = 150.0f;
    const float rowH = 52.0f;
    const int visible = 9;
    const int start =
        std::max(0, std::min(focusIndex_ - visible / 2,
                             std::max(0, static_cast<int>(items_.size()) - visible)));

    for (int i = 0; i < visible; ++i) {
        const int index = start + i;
        if (index >= static_cast<int>(items_.size())) {
            break;
        }
        const bool focused = index == focusIndex_;
        const Rect row{listX, listY + i * (rowH + 8.0f), 720.0f, rowH};
        renderer.drawRect(row, modulate(focused ? kCardFocused : kCard));
        if (focused) {
            renderer.drawRect(Rect{row.x, row.y, 6.0f, row.h}, modulate(kAccent));
        }
        if (bodyFont_) {
            bodyFont_->draw(renderer, items_[static_cast<std::size_t>(index)].name,
                            {row.x + 24.0f, row.y + 14.0f},
                            modulate(focused ? kTextBright : kTextDim));
        }
    }

    if (thumb_.valid()) {
        const float maxW = preview.w - 40.0f;
        const float maxH = 240.0f;
        const float scale =
            std::min(maxW / static_cast<float>(thumb_.width()),
                     maxH / static_cast<float>(thumb_.height()));
        const float tw = thumb_.width() * scale;
        const float th = thumb_.height() * scale;
        renderer.drawTexture(
            Rect{preview.x + (preview.w - tw) * 0.5f, preview.y + 40.0f, tw, th}, thumb_,
            // Green phosphor tint to match the CRT palette.
            modulate(Color{0.62f, 1.0f, 0.60f, 1.0f}));
    }

    if (!items_.empty() && bodyFont_) {
        const auto& item = items_[static_cast<std::size_t>(focusIndex_)];
        bodyFont_->draw(renderer, item.name, {preview.x + 20.0f, preview.y + 300.0f},
                        modulate(kTextBright));
        bodyFont_->draw(renderer, mediaTypeToString(item.type),
                        {preview.x + 20.0f, preview.y + 340.0f},
                        modulate(kAccent));
    }
}

}  // namespace cyberdeck
