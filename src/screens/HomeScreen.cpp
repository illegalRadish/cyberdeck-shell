#include "screens/HomeScreen.hpp"

#include "core/Types.hpp"
#include "screens/AskDeckScreen.hpp"
#include "screens/MediaScreen.hpp"
#include "screens/SettingsScreen.hpp"
#include "ui/AsciiSpinner.hpp"
#include "ui/Card.hpp"
#include "ui/Label.hpp"
#include "ui/SystemHud.hpp"

#include <memory>
#include <vector>

namespace cyberdeck {

namespace {

constexpr NodeId kHomeId = 100;
constexpr NodeId kTitleId = 101;
constexpr NodeId kHintId = 102;
constexpr NodeId kHudId = 103;
constexpr NodeId kLibStatusId = 104;
constexpr NodeId kSpinnerId = 105;

constexpr NodeId kMedia = 110;
constexpr NodeId kSettings = 111;
constexpr NodeId kAskDeck = 112;

SpinnerMesh meshForCategory(NodeId id) {
    switch (id) {
        case kMedia:    return SpinnerMesh::MediaM;
        case kSettings: return SpinnerMesh::Cog;
        case kAskDeck:  return SpinnerMesh::Logo;
        default:        return SpinnerMesh::Logo;
    }
}

}  // namespace

HomeScreen::HomeScreen(Font* titleFont, Font* bodyFont, ScreenManager* navigator,
                       SystemSampler* sampler, MediaLibrary* library, IPlayer* player,
                       AiAssets* ai, TorrentManager* torrents)
    : Screen(kHomeId, "HomeScreen"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      navigator_(navigator),
      sampler_(sampler),
      library_(library),
      player_(player),
      ai_(ai),
      torrents_(torrents) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);

    auto title = std::make_unique<Label>(kTitleId, "CYBERDECK", titleFont_);
    title->bounds() = Rect{64, 36, 640, 56};
    title->setColor(kTextBright);
    title->setAlignLeft(true);
    addChild(std::move(title));

    auto libStatus = std::make_unique<Label>(
        kLibStatusId, library_ ? library_->statusLine() : "PI LIB not found", bodyFont_);
    libStatus->bounds() = Rect{64, 92, 800, 28};
    libStatus->setColor(kAccent);
    libStatus->setAlignLeft(true);
    addChild(std::move(libStatus));

    auto hint = std::make_unique<Label>(
        kHintId, "ENTER TO OPEN  ·  ESC QUITS FROM HOME", bodyFont_);
    hint->bounds() = Rect{64, 668, 900, 28};
    hint->setColor(kTextDim);
    hint->setAlignLeft(true);
    addChild(std::move(hint));

    auto hud = std::make_unique<SystemHud>(kHudId, bodyFont_, sampler_);
    hud->bounds() = Rect{920, 380, 300, 260};
    addChild(std::move(hud));

    struct Item {
        NodeId id;
        const char* title;
        const char* subtitle;
        float y;
    };

    const Item items[] = {
        {kMedia, "Media", "Photos · Videos · Songs · Movies · Shows · Downloads", 180.0f},
        {kAskDeck, "Ask the Deck", "Offline voice assistant · Wikipedia", 270.0f},
        {kSettings, "Settings", "System · AI assets · Network", 360.0f},
    };

    std::vector<NodeId> order;
    for (const Item& item : items) {
        auto card = std::make_unique<Card>(item.id, item.title, titleFont_, bodyFont_);
        card->bounds() = Rect{64.0f, item.y, 700.0f, 72.0f};
        card->setSubtitle(item.subtitle);
        addChild(std::move(card));
        order.push_back(item.id);
    }
    focus_.setOrder(std::move(order));

    auto spinner = std::make_unique<AsciiSpinner>(kSpinnerId, bodyFont_);
    spinner->bounds() = Rect{936, 80, 268, 260};
    spinner_ = spinner.get();
    addChild(std::move(spinner));

    refreshStatus();
}

void HomeScreen::refreshStatus() {
    if (Node* status = findById(kLibStatusId)) {
        if (auto* label = dynamic_cast<Label*>(status)) {
            label->setText(library_ ? library_->statusLine() : "PI LIB not found");
        }
    }
}

void HomeScreen::onEnter() {
    refreshStatus();
    if (focus_.focusedId() == 0) {
        focus_.setFocus(kMedia, this);
    } else {
        focus_.refresh(this);
    }
    syncSpinnerToFocus();
    Screen::onEnter();
}

void HomeScreen::syncSpinnerToFocus() {
    if (spinner_) {
        spinner_->setMesh(meshForCategory(focus_.focusedId()));
        spinner_->resetAngle();
    }
}

void HomeScreen::handleInput(const Input& input) {
    for (Action action : input.actions()) {
        if (action == Action::Confirm) {
            if (!navigator_) {
                continue;
            }
            switch (focus_.focusedId()) {
                case kMedia:
                    navigator_->push(std::make_unique<MediaScreen>(titleFont_, bodyFont_,
                                                                   navigator_, library_,
                                                                   player_, torrents_));
                    break;
                case kAskDeck:
                    navigator_->push(std::make_unique<AskDeckScreen>(
                        titleFont_, bodyFont_, navigator_, ai_, library_, player_));
                    break;
                case kSettings:
                    navigator_->push(std::make_unique<SettingsScreen>(
                        titleFont_, bodyFont_, library_, sampler_, navigator_, ai_));
                    break;
                default:
                    break;
            }
            continue;
        }
        if (focus_.handleAction(action, this)) {
            syncSpinnerToFocus();
        }
    }
}

void HomeScreen::update(float dt) {
    statusTimer_ += dt;
    if (statusTimer_ >= 0.75f) {
        statusTimer_ = 0.0f;
        refreshStatus();
    }
    if (player_) {
        player_->update();
    }
    Screen::update(dt);
}

void HomeScreen::draw(IRenderer& renderer) {
    const Rect preview{920, 40, 300, 320};
    renderer.drawRect(preview, modulate(Color::fromBytes(8, 18, 10)));
    renderer.drawRect(Rect{preview.x, preview.y, preview.w, 4.0f}, modulate(kAccent));

    Screen::draw(renderer);
}

}  // namespace cyberdeck
