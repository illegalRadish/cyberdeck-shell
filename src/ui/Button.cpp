#include "ui/Button.hpp"

namespace cyberdeck {

Button::Button(NodeId id, std::string label, Font* font)
    : Node(id, label), label_(std::move(label)), font_(font) {
    setFocusable(true);
    setFill(kCard);
    setFocusedFill(kCardFocused);
    focusScale_.reset(1.0f, 1.0f, 0.28f, Ease::OutCubic);
    focusGlow_.reset(0.0f, 0.0f, 0.28f, Ease::InOutSine);
}

void Button::setLabel(std::string label) {
    label_ = std::move(label);
    name_ = label_;
}

void Button::activate() {
    if (onConfirm_) {
        onConfirm_();
    }
}

void Button::setFocused(bool v) {
    if (focused_ == v) {
        return;
    }
    focused_ = v;
    focusScale_.retarget(v ? 1.04f : 1.0f, 0.30f, Ease::OutCubic);
    focusGlow_.retarget(v ? 1.0f : 0.0f, 0.30f, Ease::InOutSine);
}

void Button::update(float dt) {
    scale_ = focusScale_.update(dt);
    focusGlow_.update(dt);
    Node::update(dt);
}

void Button::draw(IRenderer& renderer) {
    if (!visible_ || drawOpacity() <= 0.0f) {
        return;
    }

    const Rect r = scaledBounds();
    const float glow = focusGlow_.value();
    const Color base = lerpColor(fill_, focusedFill_, glow);
    const float radius = 8.0f;

    if (glow > 0.01f) {
        renderer.drawRoundedRect(r, modulate(Color{kGlow.r, kGlow.g, kGlow.b, 0.08f * glow}),
                                 radius + 6.0f);
    }

    renderer.drawRoundedRect(r, modulate(base), radius);

    renderer.drawRoundedRect(Rect{r.x, r.y, r.w, (2.0f + 2.0f * glow) * scale_},
                             modulate(Color{kAccent.r, kAccent.g, kAccent.b, 0.35f + 0.65f * glow}),
                             radius);

    if (font_ && !label_.empty()) {
        const Vec2 size = font_->measure(label_);
        const Vec2 pos{r.x + (r.w - size.x) * 0.5f, r.y + (r.h - size.y) * 0.5f};
        font_->draw(renderer, label_, pos,
                    modulate(lerpColor(kTextDim, kTextBright, 0.4f + 0.6f * glow)));
    }

    drawChildren(renderer);
}

}  // namespace cyberdeck
