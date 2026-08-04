#pragma once

#include <string>

struct SDL_Window;

namespace cyberdeck {

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const std::string& title, int width, int height);
    void destroy();

    void swap() const;
    void makeCurrent() const;

    int width() const { return width_; }
    int height() const { return height_; }
    // Backing store size (important on HighDPI / Retina).
    int drawableWidth() const { return drawableWidth_; }
    int drawableHeight() const { return drawableHeight_; }
    void refreshDrawableSize();
    bool isOpen() const { return window_ != nullptr; }

    SDL_Window* handle() const { return window_; }
    void* glContext() const { return glContext_; }

private:
    SDL_Window* window_ = nullptr;
    void* glContext_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int drawableWidth_ = 0;
    int drawableHeight_ = 0;
};

}  // namespace cyberdeck
