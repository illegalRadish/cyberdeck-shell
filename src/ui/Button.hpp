#pragma once

#include "render/Font.hpp"
#include "ui/Node.hpp"
#include "ui/Tween.hpp"

#include <functional>
#include <string>

namespace cyberdeck {

class Button final : public Node {
public:
    using Callback = std::function<void()>;

    Button(NodeId id, std::string label, Font* font);

    void setLabel(std::string label);
    const std::string& label() const { return label_; }
    void setFont(Font* font) { font_ = font; }
    void setOnConfirm(Callback cb) { onConfirm_ = std::move(cb); }

    void activate();  // call on Confirm while focused

    void setFocused(bool v) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    std::string label_;
    Font* font_ = nullptr;
    Callback onConfirm_;
    Tween focusScale_{1.0f, 1.0f, 0.28f, Ease::OutCubic};
    Tween focusGlow_{0.0f, 0.0f, 0.28f, Ease::InOutSine};
};

}  // namespace cyberdeck
