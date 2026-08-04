#include "ui/Card.hpp"

namespace cyberdeck {

Card::Card(NodeId id, std::string title, Font* titleFont, Font* bodyFont)
    : Node(id, std::move(title)),
      title_(name_),
      titleFont_(titleFont),
      bodyFont_(bodyFont ? bodyFont : titleFont) {
    setFocusable(true);
    setFill(kCard);
    setFocusedFill(kCardFocused);
    focusScale_.reset(1.0f, 1.0f, 0.32f, Ease::OutCubic);
    focusGlow_.reset(0.0f, 0.0f, 0.32f, Ease::InOutSine);
}

void Card::setTitle(std::string title) {
    title_ = std::move(title);
    name_ = title_;
}

void Card::setFocused(bool v) {
    if (focused_ == v) {
        return;
    }
    focused_ = v;
    focusScale_.retarget(v ? 1.045f : 1.0f, 0.34f, Ease::OutCubic);
    focusGlow_.retarget(v ? 1.0f : 0.0f, 0.36f, Ease::InOutSine);
}

void Card::update(float dt) {
    scale_ = focusScale_.update(dt);
    focusGlow_.update(dt);
    Node::update(dt);
}

void Card::draw(IRenderer& renderer) {
    if (!visible_ || drawOpacity() <= 0.0f) {
        return;
    }

    const Rect r = scaledBounds();
    const float glow = focusGlow_.value();
    const Color base = lerpColor(fill_, focusedFill_, glow);
    const float radius = 10.0f;

    // Outer accent halo that breathes with focus.
    if (glow > 0.01f) {
        renderer.drawRoundedRect(r, modulate(Color{kGlow.r, kGlow.g, kGlow.b, 0.10f * glow}),
                                 radius + 8.0f);
    }

    // Body.
    renderer.drawRoundedRect(r, modulate(base), radius);

    const float barW = (4.0f + 4.0f * glow) * scale_;
    renderer.drawRoundedRect(Rect{r.x, r.y, barW, r.h},
                             modulate(Color{kAccent.r, kAccent.g, kAccent.b, 0.25f + 0.75f * glow}),
                             radius);

    if (icon_ && icon_->valid()) {
        const float iconSize = 48.0f * scale_;
        const Rect iconRect{r.x + 24.0f * scale_, r.y + (r.h - iconSize) * 0.5f, iconSize,
                            iconSize};
        renderer.drawTexture(iconRect, *icon_,
                             modulate(lerpColor(Color{0.5f, 0.7f, 0.5f, 1}, Color{1, 1, 1, 1}, glow)));
    }

    if (titleFont_ && !title_.empty()) {
        const Vec2 titleSize = titleFont_->measure(title_);
        const float textX = r.x + (icon_ ? 88.0f : 28.0f) * scale_;
        const float textY = subtitle_.empty()
                                ? r.y + (r.h - titleSize.y) * 0.5f
                                : r.y + r.h * 0.28f - titleSize.y * 0.5f;
        titleFont_->draw(renderer, title_, {textX, textY},
                         modulate(lerpColor(kTextDim, kTextBright, 0.35f + 0.65f * glow)));

        if (!subtitle_.empty() && bodyFont_) {
            bodyFont_->draw(renderer, subtitle_, {textX, textY + titleSize.y + 4.0f * scale_},
                            modulate(lerpColor(Color::fromBytes(40, 80, 50), kTextDim,
                                               0.4f + 0.6f * glow)));
        }
    }

    drawChildren(renderer);
}

}  // namespace cyberdeck
