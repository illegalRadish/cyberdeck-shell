#pragma once

#include "core/Types.hpp"
#include "render/IRenderer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cyberdeck {

using NodeId = int;

class Node {
public:
    explicit Node(NodeId id = 0, std::string name = {});
    virtual ~Node() = default;

    NodeId id() const { return id_; }
    const std::string& name() const { return name_; }

    Rect& bounds() { return bounds_; }
    const Rect& bounds() const { return bounds_; }

    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

    bool focusable() const { return focusable_; }
    void setFocusable(bool v) { focusable_ = v; }

    bool focused() const { return focused_; }
    virtual void setFocused(bool v);

    Color fill() const { return fill_; }
    void setFill(const Color& c) { fill_ = c; }

    Color focusedFill() const { return focusedFill_; }
    void setFocusedFill(const Color& c) { focusedFill_ = c; }

    float opacity() const { return opacity_; }
    void setOpacity(float o) { opacity_ = o; }

    float scale() const { return scale_; }
    void setScale(float s) { scale_ = s; }

    void setInheritedOpacity(float o) { inheritedOpacity_ = o; }
    float drawOpacity() const { return opacity_ * inheritedOpacity_; }

    void addChild(std::unique_ptr<Node> child);
    const std::vector<std::unique_ptr<Node>>& children() const { return children_; }
    Node* findById(NodeId id);

    virtual void update(float dt);
    virtual void draw(IRenderer& renderer);

protected:
    Rect scaledBounds() const;
    Color modulate(const Color& c) const;
    void drawChildren(IRenderer& renderer);

    NodeId id_ = 0;
    std::string name_;
    Rect bounds_{};
    bool visible_ = true;
    bool focusable_ = false;
    bool focused_ = false;
    Color fill_ = kCard;
    Color focusedFill_ = kCardFocused;
    float opacity_ = 1.0f;
    float inheritedOpacity_ = 1.0f;
    float scale_ = 1.0f;
    std::vector<std::unique_ptr<Node>> children_;
};

}  // namespace cyberdeck
