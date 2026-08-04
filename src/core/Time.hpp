#pragma once

#include <cstdint>

#include <SDL2/SDL.h>

namespace cyberdeck {

class Time {
public:
    Time() : lastTicks_(SDL_GetTicks64()) {}

    // Returns delta seconds since last tick(), clamped for stability.
    float tick() {
        const std::uint64_t now = SDL_GetTicks64();
        float dt = static_cast<float>(now - lastTicks_) / 1000.0f;
        lastTicks_ = now;
        if (dt < 0.0f) {
            dt = 0.0f;
        }
        if (dt > 0.1f) {
            dt = 0.1f;
        }
        return dt;
    }

    float elapsed() const {
        return static_cast<float>(SDL_GetTicks64() - startTicks_) / 1000.0f;
    }

private:
    std::uint64_t startTicks_ = SDL_GetTicks64();
    std::uint64_t lastTicks_ = 0;
};

}  // namespace cyberdeck
