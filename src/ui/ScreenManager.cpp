#include "ui/ScreenManager.hpp"

namespace cyberdeck {

void ScreenManager::offsetTree(Node* node, float dx) {
    if (!node) {
        return;
    }
    node->bounds().x += dx;
    for (auto& child : node->children()) {
        offsetTree(child.get(), dx);
    }
}

void ScreenManager::scaleTree(Node* node, float scale, float pivotX, float pivotY) {
    (void)pivotX;
    (void)pivotY;
    if (!node) {
        return;
    }
    node->setScale(scale);
    for (auto& child : node->children()) {
        scaleTree(child.get(), scale, pivotX, pivotY);
    }
}

void ScreenManager::setRoot(std::unique_ptr<Screen> screen) {
    stack_.clear();
    incoming_.reset();
    state_ = State::Idle;
    quitRequested_ = false;
    if (!screen) {
        return;
    }
    screen->onEnter();
    stack_.push_back(std::move(screen));
}

void ScreenManager::push(std::unique_ptr<Screen> screen) {
    if (!screen || transitioning()) {
        return;
    }
    beginPush(std::move(screen));
}

void ScreenManager::pop() {
    if (transitioning()) {
        return;
    }
    if (stack_.size() <= 1) {
        quitRequested_ = true;
        return;
    }
    beginPop();
}

Screen* ScreenManager::current() {
    return stack_.empty() ? nullptr : stack_.back().get();
}

const Screen* ScreenManager::current() const {
    return stack_.empty() ? nullptr : stack_.back().get();
}

bool ScreenManager::wantsScanlines() const {
    if (state_ == State::Pushing && incoming_) {
        return incoming_->wantsScanlines();
    }
    if (const Screen* screen = current()) {
        return screen->wantsScanlines();
    }
    return true;
}

bool ScreenManager::wantsTextInput() const {
    if (state_ == State::Pushing && incoming_) {
        return incoming_->wantsTextInput();
    }
    if (const Screen* screen = current()) {
        return screen->wantsTextInput();
    }
    return false;
}

void ScreenManager::beginPush(std::unique_ptr<Screen> incoming) {
    incoming_ = std::move(incoming);
    incoming_->setOpacity(0.0f);
    incoming_->onEnter();
    state_ = State::Pushing;
    progress_.reset(0.0f, 1.0f, 0.34f, Ease::InOutCubic);
}

void ScreenManager::beginPop() {
    state_ = State::Popping;
    progress_.reset(0.0f, 1.0f, 0.34f, Ease::InOutCubic);
}

void ScreenManager::finishTransition() {
    if (state_ == State::Pushing && incoming_) {
        if (!stack_.empty()) {
            stack_.back()->setOpacity(1.0f);
        }
        incoming_->setOpacity(1.0f);
        stack_.push_back(std::move(incoming_));
    } else if (state_ == State::Popping && stack_.size() >= 2) {
        stack_.back()->onExit();
        stack_.pop_back();
        stack_.back()->setOpacity(1.0f);
        stack_.back()->onEnter();
    }

    incoming_.reset();
    state_ = State::Idle;
}

void ScreenManager::handleInput(const Input& input) {
    if (transitioning()) {
        return;
    }

    for (Action action : input.actions()) {
        if (action == Action::Back) {
            // A screen with cancellable work in flight handles Back itself, so
            // Esc aborts the task rather than tearing the screen down mid-run.
            if (Screen* screen = current(); screen && screen->consumesBack()) {
                screen->handleInput(input);
                return;
            }
            pop();
            return;
        }
        if (action == Action::Quit) {
            quitRequested_ = true;
            return;
        }
    }

    if (Screen* screen = current()) {
        screen->handleInput(input);
    }
}

void ScreenManager::update(float dt) {
    if (state_ != State::Idle) {
        progress_.update(dt);
        if (progress_.finished()) {
            finishTransition();
        }
    }

    if (!stack_.empty()) {
        // Keep underlying screen updating lightly during transitions.
        if (state_ == State::Idle || state_ == State::Pushing) {
            stack_.back()->update(dt);
        }
        if (state_ == State::Popping && stack_.size() >= 2) {
            stack_[stack_.size() - 2]->update(dt);
        }
    }
    if (incoming_) {
        incoming_->update(dt);
    }
}

void ScreenManager::draw(IRenderer& renderer) {
    const float t = progress_.value();

    if (state_ == State::Idle) {
        if (Screen* screen = current()) {
            screen->setOpacity(1.0f);
            screen->draw(renderer);
        }
        return;
    }

    if (state_ == State::Pushing && incoming_ && !stack_.empty()) {
        Screen* from = stack_.back().get();
        Screen* to = incoming_.get();

        // Outgoing screen slides left and shrinks slightly (recedes).
        from->setOpacity(1.0f - 0.45f * t);
        const float fromScale = 1.0f - 0.03f * t;
        scaleTree(from, fromScale, 0.0f, 0.0f);
        offsetTree(from, -slideDistance_ * t);
        from->draw(renderer);
        offsetTree(from, slideDistance_ * t);
        scaleTree(from, 1.0f, 0.0f, 0.0f);

        // Incoming screen slides in from the right and grows into place.
        to->setOpacity(t);
        const float toScale = 0.97f + 0.03f * t;
        scaleTree(to, toScale, 0.0f, 0.0f);
        offsetTree(to, slideDistance_ * (1.0f - t));
        to->draw(renderer);
        offsetTree(to, -slideDistance_ * (1.0f - t));
        scaleTree(to, 1.0f, 0.0f, 0.0f);
        return;
    }

    if (state_ == State::Popping && stack_.size() >= 2) {
        Screen* from = stack_.back().get();
        Screen* to = stack_[stack_.size() - 2].get();

        // Screen being revealed slides in from the left and grows into place.
        to->setOpacity(0.55f + 0.45f * t);
        const float toScale = 0.97f + 0.03f * t;
        scaleTree(to, toScale, 0.0f, 0.0f);
        offsetTree(to, -slideDistance_ * (1.0f - t));
        to->draw(renderer);
        offsetTree(to, slideDistance_ * (1.0f - t));
        scaleTree(to, 1.0f, 0.0f, 0.0f);

        // Screen being popped slides right, fades, and shrinks slightly.
        from->setOpacity(1.0f - t);
        const float fromScale = 1.0f - 0.03f * t;
        scaleTree(from, fromScale, 0.0f, 0.0f);
        offsetTree(from, slideDistance_ * t);
        from->draw(renderer);
        offsetTree(from, -slideDistance_ * t);
        scaleTree(from, 1.0f, 0.0f, 0.0f);
    }
}

}  // namespace cyberdeck
