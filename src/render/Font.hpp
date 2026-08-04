#pragma once

#include "core/Types.hpp"
#include "render/Texture.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace cyberdeck {

class IRenderer;

class Font {
public:
    Font() = default;
    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    bool load(const std::string& path, int pixelSize);
    void destroy();

    bool valid() const { return font_ != nullptr; }
    int pixelSize() const { return pixelSize_; }

    Vec2 measure(const std::string& text) const;

    // Cached RGBA texture for UTF-8 text (tint applied at draw time).
    const Texture* textureFor(const std::string& text);

    // Drop every cached glyph texture, keeping the font open. The cache never
    // evicts on its own, so screens that render open-ended strings (an LLM
    // answer, a transcript) must own a private Font and clear it — otherwise
    // each distinct line leaks a texture for the life of the process.
    void clearCache() { cache_.clear(); }

    void draw(IRenderer& renderer, const std::string& text, const Vec2& pos,
              const Color& color);

private:
    void* font_ = nullptr;  // TTF_Font*
    int pixelSize_ = 0;
    std::unordered_map<std::string, std::unique_ptr<Texture>> cache_;
};

}  // namespace cyberdeck
