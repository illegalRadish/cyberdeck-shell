#pragma once

#include "input/Actions.hpp"

#include <string>
#include <vector>

namespace cyberdeck {

class Input {
public:
    // Poll SDL events. Returns true while the app should keep running.
    bool poll();

    const std::vector<Action>& actions() const { return actions_; }
    void clearActions() { actions_.clear(); }

    bool wasPressed(Action action) const;

    // When enabled: letters type text, Backspace deletes, Esc still backs out.
    void setTextMode(bool enabled);
    bool textMode() const { return textMode_; }
    const std::string& textChars() const { return textChars_; }
    bool textBackspace() const { return textBackspace_; }

private:
    std::vector<Action> actions_;
    std::string textChars_;
    bool textBackspace_ = false;
    bool textMode_ = false;
    bool quitRequested_ = false;
};

}  // namespace cyberdeck
