#include "render/Texture.hpp"
#include "render/GL.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <iostream>

namespace cyberdeck {

Texture::~Texture() {
    destroy();
}

Texture::Texture(Texture&& other) noexcept
    : id_(other.id_), width_(other.width_), height_(other.height_) {
    other.id_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = other.id_;
        width_ = other.width_;
        height_ = other.height_;
        other.id_ = 0;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void Texture::destroy() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

bool Texture::createFromRGBA(int width, int height, const void* pixels) {
    destroy();
    if (width <= 0 || height <= 0 || !pixels) {
        return false;
    }

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    width_ = width;
    height_ = height;
    return true;
}

bool Texture::createFromSurface(SDL_Surface* surface) {
    if (!surface) {
        return false;
    }

    SDL_Surface* converted =
        SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (!converted) {
        std::cerr << "Texture: SDL_ConvertSurfaceFormat failed: " << SDL_GetError() << '\n';
        return false;
    }

    const bool ok = createFromRGBA(converted->w, converted->h, converted->pixels);
    SDL_FreeSurface(converted);
    return ok;
}

bool Texture::loadFromFile(const std::string& path) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        std::cerr << "Texture: IMG_Load failed for " << path << ": " << IMG_GetError() << '\n';
        return false;
    }
    const bool ok = createFromSurface(surface);
    SDL_FreeSurface(surface);
    return ok;
}

void Texture::bind(unsigned int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

}  // namespace cyberdeck
