#include "input/Input.hpp"

#include <SDL2/SDL.h>

#include <algorithm>

namespace cyberdeck {

namespace {

Action mapNavKey(SDL_Keycode key, bool textMode) {
    switch (key) {
        case SDLK_UP:
            return Action::Up;
        case SDLK_DOWN:
            return Action::Down;
        case SDLK_LEFT:
            return textMode ? Action::None : Action::Left;
        case SDLK_RIGHT:
            return textMode ? Action::None : Action::Right;
        case SDLK_RETURN:
            return Action::Confirm;
        case SDLK_ESCAPE:
            return Action::Back;
        case SDLK_BACKSPACE:
            // In menus, Backspace = back. In terminal text mode it deletes.
            return textMode ? Action::None : Action::Back;
        case SDLK_SPACE:
            return textMode ? Action::None : Action::Confirm;
        case SDLK_w:
        case SDLK_a:
        case SDLK_s:
        case SDLK_d:
            if (textMode) {
                return Action::None;
            }
            if (key == SDLK_w) return Action::Up;
            if (key == SDLK_s) return Action::Down;
            if (key == SDLK_a) return Action::Left;
            return Action::Right;
        default:
            return Action::None;
    }
}

}  // namespace

void Input::setTextMode(bool enabled) {
    if (textMode_ == enabled) {
        return;
    }
    textMode_ = enabled;
    if (enabled) {
        SDL_StartTextInput();
    } else {
        SDL_StopTextInput();
    }
}

bool Input::poll() {
    actions_.clear();
    textChars_.clear();
    textBackspace_ = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            quitRequested_ = true;
            actions_.push_back(Action::Quit);
            continue;
        }

        if (textMode_ && event.type == SDL_TEXTINPUT) {
            textChars_ += event.text.text;
            continue;
        }

        if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
            if (textMode_ && event.key.keysym.sym == SDLK_BACKSPACE) {
                textBackspace_ = true;
                continue;
            }

            const Action action = mapNavKey(event.key.keysym.sym, textMode_);
            if (action != Action::None) {
                actions_.push_back(action);
            }
        }
    }

    return !quitRequested_;
}

bool Input::wasPressed(Action action) const {
    return std::find(actions_.begin(), actions_.end(), action) != actions_.end();
}

}  // namespace cyberdeck
