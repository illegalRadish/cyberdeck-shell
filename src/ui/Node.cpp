#include "ui/Node.hpp"

namespace cyberdeck {

Node::Node(NodeId id, std::string name) : id_(id), name_(std::move(name)) {}

void Node::setFocused(bool v) {
    focused_ = v;
}

void Node::addChild(std::unique_ptr<Node> child) {
    children_.push_back(std::move(child));
}

Node* Node::findById(NodeId id) {
    if (id_ == id) {
        return this;
    }
    for (auto& child : children_) {
        if (Node* found = child->findById(id)) {
            return found;
        }
    }
    return nullptr;
}

Rect Node::scaledBounds() const {
    if (scale_ == 1.0f) {
        return bounds_;
    }
    const float cx = bounds_.x + bounds_.w * 0.5f;
    const float cy = bounds_.y + bounds_.h * 0.5f;
    const float w = bounds_.w * scale_;
    const float h = bounds_.h * scale_;
    return Rect{cx - w * 0.5f, cy - h * 0.5f, w, h};
}

Color Node::modulate(const Color& c) const {
    return Color{c.r, c.g, c.b, c.a * drawOpacity()};
}

void Node::drawChildren(IRenderer& renderer) {
    const float parentOp = drawOpacity();
    for (auto& child : children_) {
        child->setInheritedOpacity(parentOp);
        child->draw(renderer);
        child->setInheritedOpacity(1.0f);
    }
}

void Node::update(float dt) {
    for (auto& child : children_) {
        child->update(dt);
    }
}

void Node::draw(IRenderer& renderer) {
    if (!visible_ || drawOpacity() <= 0.0f) {
        return;
    }

    const Color& base = focused_ ? focusedFill_ : fill_;
    const Color color = modulate(base);
    const Rect r = scaledBounds();
    if (r.w > 0.0f && r.h > 0.0f && color.a > 0.0f) {
        const float radius = 8.0f * scale_;
        renderer.drawRoundedRect(r, color, radius);
        if (focused_) {
            const float barW = 5.0f * scale_;
            renderer.drawRoundedRect(Rect{r.x, r.y + 2.0f, barW, r.h - 4.0f},
                                     modulate(kAccent), barW * 0.5f);
        }
    }


    drawChildren(renderer);
}

}  // namespace cyberdeck
