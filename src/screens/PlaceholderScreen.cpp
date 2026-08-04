#include "screens/PlaceholderScreen.hpp"

#include "core/Types.hpp"
#include "ui/Label.hpp"

#include <memory>

namespace cyberdeck {

PlaceholderScreen::PlaceholderScreen(NodeId id, std::string title, std::string detail,
                                     Font* titleFont, Font* bodyFont)
    : Screen(id, title), titleFont_(titleFont), bodyFont_(bodyFont) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);

    auto heading = std::make_unique<Label>(id + 1, std::move(title), titleFont_);
    heading->bounds() = Rect{80, 72, 900, 56};
    heading->setColor(kTextBright);
    heading->setAlignLeft(true);
    addChild(std::move(heading));

    auto body = std::make_unique<Label>(id + 2, std::move(detail), bodyFont_);
    body->bounds() = Rect{80, 160, 1000, 40};
    body->setColor(kTextDim);
    body->setAlignLeft(true);
    addChild(std::move(body));

    auto hint = std::make_unique<Label>(id + 3, "ESC / BACKSPACE TO RETURN", bodyFont_);
    hint->bounds() = Rect{80, 640, 600, 32};
    hint->setColor(kTextDim);
    hint->setAlignLeft(true);
    addChild(std::move(hint));
}

void PlaceholderScreen::handleInput(const Input& input) {
    // Back is handled by ScreenManager. No focusables yet.
    (void)input;
}

}  // namespace cyberdeck
