#include "ui/Screen.hpp"

namespace cyberdeck {

Screen::Screen(NodeId id, std::string name) : Node(id, std::move(name)) {}

void Screen::onEnter() {
    focus_.refresh(this);
}

void Screen::onExit() {}

void Screen::handleInput(const Input& input) {
    for (Action action : input.actions()) {
        focus_.handleAction(action, this);
    }
}

void Screen::update(float dt) {
    Node::update(dt);
}

void Screen::draw(IRenderer& renderer) {
    Node::draw(renderer);
}

}  // namespace cyberdeck
