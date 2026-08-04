#include "render/Font.hpp"
#include "render/IRenderer.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <iostream>

namespace cyberdeck {

Font::~Font() {
    destroy();
}

void Font::destroy() {
    cache_.clear();
    if (font_) {
        TTF_CloseFont(static_cast<TTF_Font*>(font_));
        font_ = nullptr;
    }
    pixelSize_ = 0;
}

bool Font::load(const std::string& path, int pixelSize) {
    destroy();
    if (path.empty() || pixelSize <= 0) {
        return false;
    }

    font_ = TTF_OpenFont(path.c_str(), pixelSize);
    if (!font_) {
        std::cerr << "Font: TTF_OpenFont failed for " << path << ": " << TTF_GetError()
                  << '\n';
        return false;
    }
    pixelSize_ = pixelSize;
    return true;
}

Vec2 Font::measure(const std::string& text) const {
    if (!font_ || text.empty()) {
        return {};
    }
    int w = 0;
    int h = 0;
    if (TTF_SizeUTF8(static_cast<TTF_Font*>(font_), text.c_str(), &w, &h) != 0) {
        return {};
    }
    return {static_cast<float>(w), static_cast<float>(h)};
}

const Texture* Font::textureFor(const std::string& text) {
    if (!font_ || text.empty()) {
        return nullptr;
    }

    if (auto it = cache_.find(text); it != cache_.end()) {
        return it->second.get();
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(static_cast<TTF_Font*>(font_), text.c_str(),
                                                  SDL_Color{255, 255, 255, 255});
    if (!surface) {
        std::cerr << "Font: TTF_RenderUTF8_Blended failed: " << TTF_GetError() << '\n';
        return nullptr;
    }

    auto texture = std::make_unique<Texture>();
    const bool ok = texture->createFromSurface(surface);
    SDL_FreeSurface(surface);
    if (!ok) {
        return nullptr;
    }

    const Texture* raw = texture.get();
    cache_.emplace(text, std::move(texture));
    return raw;
}

void Font::draw(IRenderer& renderer, const std::string& text, const Vec2& pos,
                const Color& color) {
    const Texture* tex = textureFor(text);
    if (!tex) {
        return;
    }
    renderer.drawTexture(Rect{pos.x, pos.y, static_cast<float>(tex->width()),
                              static_cast<float>(tex->height())},
                         *tex, color);
}

}  // namespace cyberdeck
