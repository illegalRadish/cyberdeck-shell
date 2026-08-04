#include "platform/Window.hpp"

#include <SDL2/SDL.h>

#include <iostream>

namespace cyberdeck {

Window::~Window() {
    destroy();
}

bool Window::create(const std::string& title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    window_ = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

    if (!window_) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return false;
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << '\n';
        destroy();
        return false;
    }

    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1);  // VSync

    width_ = width;
    height_ = height;
    refreshDrawableSize();
    return true;
}

void Window::refreshDrawableSize() {
    if (!window_) {
        drawableWidth_ = 0;
        drawableHeight_ = 0;
        return;
    }
    SDL_GL_GetDrawableSize(window_, &drawableWidth_, &drawableHeight_);
}

void Window::destroy() {
    if (glContext_) {
        SDL_GL_DeleteContext(glContext_);
        glContext_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
    width_ = 0;
    height_ = 0;
    drawableWidth_ = 0;
    drawableHeight_ = 0;
}

void Window::swap() const {
    if (window_) {
        SDL_GL_SwapWindow(window_);
    }
}

void Window::makeCurrent() const {
    if (window_ && glContext_) {
        SDL_GL_MakeCurrent(window_, glContext_);
    }
}

}  // namespace cyberdeck
