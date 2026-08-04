#pragma once

#include "input/Actions.hpp"
#include "ui/Node.hpp"

#include <unordered_map>
#include <vector>

namespace cyberdeck {

// Directional focus graph. Phase 1 also supports simple ordered-list navigation.
class FocusNav {
public:
    void clear();
    void setOrder(std::vector<NodeId> order);
    void setFocus(NodeId id, Node* root);
    void refresh(Node* root);
    NodeId focusedId() const { return focusedId_; }

    // Returns true if focus changed.
    bool handleAction(Action action, Node* root);

private:
    void applyFocus(Node* root);

    NodeId focusedId_ = 0;
    std::vector<NodeId> order_;
    std::unordered_map<NodeId, std::unordered_map<Action, NodeId>> edges_;
};

}  // namespace cyberdeck
