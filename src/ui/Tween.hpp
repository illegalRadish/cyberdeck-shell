#pragma once

#include <algorithm>
#include <cmath>

namespace cyberdeck {

enum class Ease {
    Linear,
    InOutQuad,
    InOutCubic,
    InOutSine,
    OutCubic,
    OutBack,
};

inline float easeLinear(float t) { return t; }

inline float easeInOutQuad(float t) {
    return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

inline float easeInOutCubic(float t) {
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

inline float easeInOutSine(float t) {
    return -(std::cos(3.14159265f * t) - 1.0f) / 2.0f;
}

inline float easeOutCubic(float t) {
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

inline float easeOutBack(float t) {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

inline float applyEase(Ease ease, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (ease) {
        case Ease::InOutQuad:
            return easeInOutQuad(t);
        case Ease::InOutCubic:
            return easeInOutCubic(t);
        case Ease::InOutSine:
            return easeInOutSine(t);
        case Ease::OutCubic:
            return easeOutCubic(t);
        case Ease::OutBack:
            return easeOutBack(t);
        case Ease::Linear:
        default:
            return easeLinear(t);
    }
}

class Tween {
public:
    Tween() = default;

    Tween(float from, float to, float duration, Ease ease = Ease::InOutCubic)
        : from_(from), to_(to), duration_(std::max(duration, 0.0001f)), ease_(ease) {
        value_ = from_;
    }

    void reset(float from, float to, float duration, Ease ease = Ease::InOutCubic) {
        from_ = from;
        to_ = to;
        duration_ = std::max(duration, 0.0001f);
        ease_ = ease;
        elapsed_ = 0.0f;
        finished_ = false;
        value_ = from_;
    }

    void retarget(float to, float duration, Ease ease = Ease::InOutCubic) {
        reset(value_, to, duration, ease);
    }

    // Smooth critically-damped style chase toward a target (no hard retarget snap).
    void chase(float target, float speed, float dt) {
        finished_ = false;
        to_ = target;
        const float t = 1.0f - std::exp(-speed * dt);
        value_ += (target - value_) * t;
        from_ = value_;
        if (std::fabs(target - value_) < 0.0005f) {
            value_ = target;
            finished_ = true;
        }
    }

    float update(float dt) {
        if (finished_) {
            return value_;
        }
        elapsed_ += dt;
        const float t = std::clamp(elapsed_ / duration_, 0.0f, 1.0f);
        value_ = from_ + (to_ - from_) * applyEase(ease_, t);
        if (t >= 1.0f) {
            finished_ = true;
            value_ = to_;
        }
        return value_;
    }

    float value() const { return value_; }
    bool finished() const { return finished_; }
    float target() const { return to_; }

private:
    float from_ = 0.0f;
    float to_ = 0.0f;
    float duration_ = 0.28f;
    float elapsed_ = 0.0f;
    float value_ = 0.0f;
    Ease ease_ = Ease::InOutCubic;
    bool finished_ = true;
};

}  // namespace cyberdeck
