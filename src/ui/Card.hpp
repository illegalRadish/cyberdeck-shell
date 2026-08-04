#pragma once

#include "render/Font.hpp"
#include "render/Texture.hpp"
#include "ui/Node.hpp"
#include "ui/Tween.hpp"

#include <string>

namespace cyberdeck {

class Card final : public Node {
public:
    Card(NodeId id, std::string title, Font* titleFont, Font* bodyFont = nullptr);

    void setTitle(std::string title);
    const std::string& title() const { return title_; }

    void setTitleFont(Font* font) { titleFont_ = font; }
    void setBodyFont(Font* font) { bodyFont_ = font; }
    void setIcon(Texture* icon) { icon_ = icon; }
    void setSubtitle(std::string subtitle) { subtitle_ = std::move(subtitle); }

    void setFocused(bool v) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    std::string title_;
    std::string subtitle_;
    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    Texture* icon_ = nullptr;
    Tween focusScale_{1.0f, 1.0f, 0.18f, Ease::OutBack};
    Tween focusGlow_{0.0f, 0.0f, 0.18f, Ease::InOutQuad};
};

}  // namespace cyberdeck
