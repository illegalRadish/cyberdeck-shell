#pragma once

#include "input/Input.hpp"
#include "ui/FocusNav.hpp"
#include "ui/Node.hpp"

namespace cyberdeck {

class Screen : public Node {
public:
    explicit Screen(NodeId id = 0, std::string name = {});

    FocusNav& focus() { return focus_; }
    const FocusNav& focus() const { return focus_; }

    virtual void onEnter();
    virtual void onExit();
    virtual void handleInput(const Input& input);
    virtual bool wantsTextInput() const { return false; }

    // Whether Back should be delivered to this screen instead of popping it.
    // Screens with cancellable work in flight return true while it is running,
    // so Esc means "abort the task" and only pops once the screen is idle.
    virtual bool consumesBack() const { return false; }

    // Whether the fullscreen CRT scanline overlay may be drawn over this screen.
    // Screens showing fullscreen video return false so footage stays clean.
    virtual bool wantsScanlines() const { return true; }

    void update(float dt) override;
    void draw(IRenderer& renderer) override;

protected:
    FocusNav focus_;
};

}  // namespace cyberdeck
