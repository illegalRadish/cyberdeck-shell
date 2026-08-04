#include "screens/MediaScreen.hpp"

#include "core/Types.hpp"
#include "screens/LibraryBrowserScreen.hpp"
#include "screens/DownloadsScreen.hpp"
#include "screens/MoviesScreen.hpp"
#include "screens/MusicScreen.hpp"
#include "ui/AsciiSpinner.hpp"
#include "ui/Card.hpp"
#include "ui/Label.hpp"

#include <memory>
#include <sstream>
#include <vector>

namespace cyberdeck {

namespace {

constexpr NodeId kMediaId = 200;
constexpr NodeId kTitleId = 201;
constexpr NodeId kHintId = 202;
constexpr NodeId kStatusId = 203;
constexpr NodeId kSpinnerId = 204;

constexpr NodeId kPhotos = 210;
constexpr NodeId kVideos = 211;
constexpr NodeId kSongs = 212;
constexpr NodeId kMovies = 213;
constexpr NodeId kShows = 214;
constexpr NodeId kFiles = 215;
constexpr NodeId kDownloads = 216;

SpinnerMesh meshForCategory(NodeId id) {
    switch (id) {
        case kPhotos: return SpinnerMesh::Camera;
        case kVideos: return SpinnerMesh::Video;
        case kSongs:  return SpinnerMesh::Music;
        case kMovies: return SpinnerMesh::Film;
        case kShows:  return SpinnerMesh::Tv;
        case kDownloads: return SpinnerMesh::Logo;
        default:      return SpinnerMesh::Logo;
    }
}

std::string countLabel(MediaLibrary* library, MediaType type) {
    if (!library || !library->ready()) {
        return "Awaiting PI LIB";
    }
    const int count = library->db().countByType(type);
    std::ostringstream ss;
    ss << count << (count == 1 ? " item" : " items");
    return ss.str();
}

}  // namespace

MediaScreen::MediaScreen(Font* titleFont, Font* bodyFont, ScreenManager* navigator,
                         MediaLibrary* library, IPlayer* player, TorrentManager* torrents)
    : Screen(kMediaId, "MediaScreen"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      navigator_(navigator),
      library_(library),
      player_(player),
      torrents_(torrents) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);

    auto title = std::make_unique<Label>(kTitleId, "CYBERDECK / MEDIA", titleFont_);
    title->bounds() = Rect{64, 36, 800, 56};
    title->setColor(kTextBright);
    title->setAlignLeft(true);
    addChild(std::move(title));

    auto status = std::make_unique<Label>(
        kStatusId, library_ ? library_->statusLine() : "PI LIB not found", bodyFont_);
    status->bounds() = Rect{64, 92, 800, 28};
    status->setColor(kAccent);
    status->setAlignLeft(true);
    addChild(std::move(status));

    auto hint = std::make_unique<Label>(kHintId, "ENTER TO OPEN  ·  ESC TO GO BACK", bodyFont_);
    hint->bounds() = Rect{64, 668, 900, 28};
    hint->setColor(kTextDim);
    hint->setAlignLeft(true);
    addChild(std::move(hint));

    struct Item {
        NodeId id;
        const char* title;
        float y;
    };

    // Seven rows at a 68px pitch: the last card ends at 594, clear of the
    // hint line at 668. An eighth would not fit without a scrolling list.
    const Item items[] = {
        {kPhotos, "Photos", 148.0f},
        {kVideos, "Videos", 216.0f},
        {kSongs, "Songs", 284.0f},
        {kMovies, "Movies", 352.0f},
        {kShows, "Shows", 420.0f},
        {kFiles, "Files", 488.0f},
        {kDownloads, "Downloads", 556.0f},
    };

    std::vector<NodeId> order;
    for (const Item& item : items) {
        auto card = std::make_unique<Card>(item.id, item.title, bodyFont_, bodyFont_);
        card->bounds() = Rect{64.0f, item.y, 700.0f, 60.0f};
        card->setSubtitle("…");
        addChild(std::move(card));
        order.push_back(item.id);
    }
    focus_.setOrder(std::move(order));

    auto spinner = std::make_unique<AsciiSpinner>(kSpinnerId, bodyFont_);
    spinner->bounds() = Rect{876, 190, 308, 300};
    spinner_ = spinner.get();
    addChild(std::move(spinner));

    refreshSubtitles();
}

void MediaScreen::refreshSubtitles() {
    auto setSub = [this](NodeId id, const std::string& text) {
        if (Node* node = findById(id)) {
            if (auto* card = dynamic_cast<Card*>(node)) {
                card->setSubtitle(text);
            }
        }
    };

    setSub(kPhotos, countLabel(library_, MediaType::Photo));
    setSub(kVideos, countLabel(library_, MediaType::Video));
    setSub(kSongs, countLabel(library_, MediaType::Music));
    setSub(kMovies, countLabel(library_, MediaType::Movie));
    setSub(kShows, countLabel(library_, MediaType::TvShow));
    setSub(kFiles, library_ && library_->ready()
                       ? std::to_string(library_->db().countAll()) + " indexed files"
                       : "Browse media drive");

    // Subtitle is a live count, so it doubles as an at-a-glance indicator that
    // something is still transferring without opening the screen.
    if (torrents_) {
        const TorrentStatus status = torrents_->status();
        if (!status.engineInstalled) {
            setSub(kDownloads, "Torrent engine not installed");
        } else if (status.torrents.empty()) {
            setSub(kDownloads, "No active torrents");
        } else {
            setSub(kDownloads, std::to_string(status.torrents.size()) + " active");
        }
    } else {
        setSub(kDownloads, "Unavailable");
    }

    if (Node* status = findById(kStatusId)) {
        if (auto* label = dynamic_cast<Label*>(status)) {
            label->setText(library_ ? library_->statusLine() : "PI LIB not found");
        }
    }
}

void MediaScreen::syncSpinnerToFocus() {
    if (spinner_) {
        spinner_->setMesh(meshForCategory(focus_.focusedId()));
        spinner_->resetAngle();
    }
}

void MediaScreen::onEnter() {
    refreshSubtitles();
    if (focus_.focusedId() == 0) {
        focus_.setFocus(kPhotos, this);
    } else {
        focus_.refresh(this);
    }
    syncSpinnerToFocus();
    Screen::onEnter();
}

void MediaScreen::openFocused() {
    if (!navigator_) {
        return;
    }
    static NodeId nextId = 600;

    auto pushBrowser = [&](const char* title, MediaType type, bool showAll = false) {
        navigator_->push(std::make_unique<LibraryBrowserScreen>(
            nextId++, std::string("MEDIA / ") + title, type, titleFont_, bodyFont_, library_,
            showAll));
    };

    switch (focus_.focusedId()) {
        case kPhotos:
            pushBrowser("PHOTOS", MediaType::Photo);
            break;
        case kVideos:
            pushBrowser("VIDEOS", MediaType::Video);
            break;
        case kSongs:
            navigator_->push(std::make_unique<MusicScreen>(titleFont_, bodyFont_, library_,
                                                           player_, navigator_));
            break;
        case kMovies:
            navigator_->push(std::make_unique<MoviesScreen>(titleFont_, bodyFont_, library_,
                                                            player_, navigator_));
            break;
        case kShows:
            pushBrowser("SHOWS", MediaType::TvShow);
            break;
        case kFiles:
            pushBrowser("FILES", MediaType::Other, true);
            break;
        case kDownloads:
            navigator_->push(std::make_unique<DownloadsScreen>(titleFont_, bodyFont_,
                                                               torrents_, navigator_));
            break;
        default:
            break;
    }
}

void MediaScreen::handleInput(const Input& input) {
    for (Action action : input.actions()) {
        if (action == Action::Confirm) {
            openFocused();
            continue;
        }
        if (focus_.handleAction(action, this)) {
            syncSpinnerToFocus();
        }
    }
}

void MediaScreen::update(float dt) {
    subtitleTimer_ += dt;
    if (subtitleTimer_ >= 0.75f) {
        subtitleTimer_ = 0.0f;
        refreshSubtitles();
    }
    if (player_) {
        player_->update();
    }
    Screen::update(dt);
}

void MediaScreen::draw(IRenderer& renderer) {
    // Preview panel chrome, drawn under the spinner child.
    const Rect preview{860, 150, 340, 420};
    renderer.drawRect(preview, modulate(Color::fromBytes(8, 18, 10)));
    renderer.drawRect(Rect{preview.x, preview.y, preview.w, 4.0f}, modulate(kAccent));

    Screen::draw(renderer);

    if (bodyFont_) {
        bodyFont_->draw(renderer, "BROWSING", {preview.x + 20.0f, preview.y + 20.0f},
                        modulate(kTextDim));
    }
}

}  // namespace cyberdeck
