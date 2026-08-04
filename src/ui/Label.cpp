#include "ui/Label.hpp"

namespace cyberdeck {

Label::Label(NodeId id, std::string text, Font* font)
    : Node(id, "Label"), text_(std::move(text)), font_(font) {
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);
}

void Label::setText(std::string text) {
    text_ = std::move(text);
}

void Label::draw(IRenderer& renderer) {
    if (!visible_ || drawOpacity() <= 0.0f || !font_ || text_.empty()) {
        return;
    }

    const Color color = modulate(focused_ ? focusedTextColor_ : textColor_);
    const Rect r = scaledBounds();

    Vec2 pos{r.x, r.y};
    if (r.w > 0.0f && r.h > 0.0f) {
        const Vec2 size = font_->measure(text_);
        if (alignLeft_) {
            pos.x = r.x;
            pos.y = r.y + (r.h - size.y) * 0.5f;
        } else {
            pos.x = r.x + (r.w - size.x) * 0.5f;
            pos.y = r.y + (r.h - size.y) * 0.5f;
        }
    }

    font_->draw(renderer, text_, pos, color);
    drawChildren(renderer);
}

}  // namespace cyberdeck
