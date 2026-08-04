#pragma once

#include <cstdint>

namespace cyberdeck {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr Color fromBytes(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                     std::uint8_t a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }
};

inline Color lerpColor(const Color& a, const Color& b, float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                 a.a + (b.a - a.a) * t};
}

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    Rect() = default;
    Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    Vec2 position() const { return {x, y}; }
    Vec2 size() const { return {w, h}; }
};

// Pip-Boy / CRT terminal palette
inline constexpr Color kBgDark = Color::fromBytes(5, 10, 7);
inline constexpr Color kBgPanel = Color::fromBytes(8, 18, 11);
inline constexpr Color kCard = Color::fromBytes(12, 26, 16);
inline constexpr Color kCardFocused = Color::fromBytes(19, 54, 29);
inline constexpr Color kBorder = Color::fromBytes(24, 60, 32);
inline constexpr Color kBorderFocused = Color::fromBytes(40, 130, 56);
inline constexpr Color kAccent = Color::fromBytes(57, 255, 20);
inline constexpr Color kGlow = Color::fromBytes(57, 255, 20);
inline constexpr Color kShadow = Color::fromBytes(0, 0, 0, 200);
inline constexpr Color kTextBright = Color::fromBytes(190, 255, 170);
inline constexpr Color kTextDim = Color::fromBytes(80, 155, 92);
inline constexpr Color kTextFaint = Color::fromBytes(48, 96, 58);

}  // namespace cyberdeck
