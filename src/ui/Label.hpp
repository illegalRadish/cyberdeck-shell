#pragma once

#include "render/Font.hpp"
#include "ui/Node.hpp"

#include <string>

namespace cyberdeck {

class Label final : public Node {
public:
    Label(NodeId id, std::string text, Font* font);

    void setText(std::string text);
    const std::string& text() const { return text_; }

    void setFont(Font* font) { font_ = font; }
    void setColor(const Color& c) { textColor_ = c; }
    void setFocusedColor(const Color& c) { focusedTextColor_ = c; }
    void setAlignLeft(bool left) { alignLeft_ = left; }

    void draw(IRenderer& renderer) override;

private:
    std::string text_;
    Font* font_ = nullptr;
    Color textColor_ = Color::fromBytes(220, 230, 240);
    Color focusedTextColor_ = Color::fromBytes(255, 255, 255);
    bool alignLeft_ = false;
};

}  // namespace cyberdeck
