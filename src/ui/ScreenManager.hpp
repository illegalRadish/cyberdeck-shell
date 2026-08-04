#pragma once

#include "input/Input.hpp"
#include "render/IRenderer.hpp"
#include "ui/Screen.hpp"
#include "ui/Tween.hpp"

#include <memory>
#include <vector>

namespace cyberdeck {

class ScreenManager {
public:
    ScreenManager() = default;

    void setRoot(std::unique_ptr<Screen> screen);
    void push(std::unique_ptr<Screen> screen);
    void pop();

    Screen* current();
    const Screen* current() const;
    int depth() const { return static_cast<int>(stack_.size()); }
    bool transitioning() const { return state_ != State::Idle; }
    bool quitRequested() const { return quitRequested_; }
    void clearQuitRequest() { quitRequested_ = false; }
    void requestQuit() { quitRequested_ = true; }
    bool wantsTextInput() const;
    // Whether the top-most screen allows the fullscreen scanline overlay.
    bool wantsScanlines() const;

    void handleInput(const Input& input);
    void update(float dt);
    void draw(IRenderer& renderer);

private:
    enum class State { Idle, Pushing, Popping };

    void beginPush(std::unique_ptr<Screen> incoming);
    void beginPop();
    void finishTransition();
    static void offsetTree(Node* node, float dx);
    static void scaleTree(Node* node, float scale, float pivotX, float pivotY);

    std::vector<std::unique_ptr<Screen>> stack_;
    std::unique_ptr<Screen> incoming_;
    State state_ = State::Idle;
    Tween progress_{0.0f, 0.0f, 0.34f, Ease::InOutCubic};
    bool quitRequested_ = false;
    float slideDistance_ = 72.0f;
};

}  // namespace cyberdeck
