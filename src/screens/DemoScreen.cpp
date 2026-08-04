#include "screens/DemoScreen.hpp"

#include "core/Assets.hpp"
#include "core/Types.hpp"
#include "ui/Button.hpp"
#include "ui/Card.hpp"
#include "ui/Label.hpp"

#include <iostream>
#include <memory>

namespace cyberdeck {

namespace {

constexpr NodeId kScreenId = 1;
constexpr NodeId kTitleId = 2;
constexpr NodeId kStatusId = 3;
constexpr NodeId kCardMusic = 10;
constexpr NodeId kCardMovies = 11;
constexpr NodeId kCardPhotos = 12;
constexpr NodeId kBtnPlay = 20;

}  // namespace

DemoScreen::DemoScreen(Font* titleFont, Font* bodyFont)
    : Screen(kScreenId, "DemoScreen"), titleFont_(titleFont), bodyFont_(bodyFont) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);

    const std::string iconPath = assets::resolve("images/demo_icon.png");
    if (!demoIcon_.loadFromFile(iconPath)) {
        std::cerr << "DemoScreen: optional icon failed to load: " << iconPath << '\n';
    }

    auto title = std::make_unique<Label>(kTitleId, "CYBERDECK", titleFont_);
    title->bounds() = Rect{80, 48, 600, 56};
    title->setColor(Color::fromBytes(230, 240, 255));
    title->setAlignLeft(true);
    addChild(std::move(title));

    auto makeCard = [this](NodeId id, const char* name, const char* subtitle, float x,
                           float y, bool withIcon) {
        auto card = std::make_unique<Card>(id, name, titleFont_, bodyFont_);
        card->bounds() = Rect{x, y, 340.0f, 180.0f};
        card->setSubtitle(subtitle);
        if (withIcon && demoIcon_.valid()) {
            card->setIcon(&demoIcon_);
        }
        return card;
    };

    addChild(makeCard(kCardMusic, "Music", "Albums · Artists · Playlists", 80.0f, 160.0f, true));
    addChild(makeCard(kCardMovies, "Movies", "Posters · Continue watching", 470.0f, 160.0f, false));
    addChild(makeCard(kCardPhotos, "Photos", "Library · Timeline", 860.0f, 160.0f, false));

    auto play = std::make_unique<Button>(kBtnPlay, "Play Demo", bodyFont_);
    play->bounds() = Rect{80, 400, 220, 56};
    play->setOnConfirm([this]() {
        statusText_ = "Play Demo confirmed";
        std::cout << "[cyberdeck] Play Demo activated\n";
        if (Node* status = findById(kStatusId)) {
            if (auto* label = dynamic_cast<Label*>(status)) {
                label->setText(statusText_);
            }
        }
    });
    addChild(std::move(play));

    auto status = std::make_unique<Label>(kStatusId, statusText_, bodyFont_);
    status->bounds() = Rect{80, 640, 1100, 32};
    status->setColor(Color::fromBytes(140, 155, 175));
    status->setAlignLeft(true);
    addChild(std::move(status));

    focus_.setOrder({kCardMusic, kCardMovies, kCardPhotos, kBtnPlay});
}

void DemoScreen::onEnter() {
    focus_.setFocus(kCardMusic, this);
    Screen::onEnter();
}

void DemoScreen::handleInput(const Input& input) {
    for (Action action : input.actions()) {
        if (action == Action::Confirm) {
            if (Node* node = findById(focus_.focusedId())) {
                if (auto* button = dynamic_cast<Button*>(node)) {
                    button->activate();
                } else if (auto* card = dynamic_cast<Card*>(node)) {
                    statusText_ = std::string("Selected: ") + card->title();
                    std::cout << "[cyberdeck] " << statusText_ << '\n';
                    if (Node* status = findById(kStatusId)) {
                        if (auto* label = dynamic_cast<Label*>(status)) {
                            label->setText(statusText_);
                        }
                    }
                }
            }
            continue;
        }
        focus_.handleAction(action, this);
    }
}

}  // namespace cyberdeck
