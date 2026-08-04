#include "ui/FocusNav.hpp"

#include <algorithm>

namespace cyberdeck {

void FocusNav::clear() {
    focusedId_ = 0;
    order_.clear();
    edges_.clear();
}

void FocusNav::setOrder(std::vector<NodeId> order) {
    order_ = std::move(order);
    if (!order_.empty() && focusedId_ == 0) {
        focusedId_ = order_.front();
    }
}

void FocusNav::setFocus(NodeId id, Node* root) {
    focusedId_ = id;
    applyFocus(root);
}

void FocusNav::refresh(Node* root) {
    applyFocus(root);
}

void FocusNav::applyFocus(Node* root) {
    if (!root) {
        return;
    }

    std::vector<Node*> stack{root};
    while (!stack.empty()) {
        Node* node = stack.back();
        stack.pop_back();
        node->setFocused(node->id() == focusedId_ && node->focusable());
        for (auto& child : node->children()) {
            stack.push_back(child.get());
        }
    }
}

bool FocusNav::handleAction(Action action, Node* root) {
    if (order_.empty()) {
        return false;
    }

    if (auto it = edges_.find(focusedId_); it != edges_.end()) {
        if (auto edge = it->second.find(action); edge != it->second.end()) {
            focusedId_ = edge->second;
            applyFocus(root);
            return true;
        }
    }

    auto it = std::find(order_.begin(), order_.end(), focusedId_);
    if (it == order_.end()) {
        focusedId_ = order_.front();
        applyFocus(root);
        return true;
    }

    const std::size_t index = static_cast<std::size_t>(std::distance(order_.begin(), it));
    std::size_t next = index;

    switch (action) {
        case Action::Up:
        case Action::Left:
            next = (index == 0) ? order_.size() - 1 : index - 1;
            break;
        case Action::Down:
        case Action::Right:
            next = (index + 1) % order_.size();
            break;
        default:
            return false;
    }

    if (next == index) {
        return false;
    }

    focusedId_ = order_[next];
    applyFocus(root);
    return true;
}

}  // namespace cyberdeck
