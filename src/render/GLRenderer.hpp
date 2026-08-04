#pragma once

#include "render/IRenderer.hpp"
#include "render/Shader.hpp"

#include <string>

namespace cyberdeck {

class GLRenderer final : public IRenderer {
public:
    GLRenderer() = default;
    ~GLRenderer() override;

    bool init(int width, int height) override;
    void shutdown() override;

    void beginFrame(const Color& clearColor) override;
    void endFrame() override;

    // Re-apply UI GL state after external renderers (e.g. libmpv) mutate it.
    void bindUiState() override;

    void setViewport(int width, int height) override;
    void drawRect(const Rect& rect, const Color& color) override;
    void drawRoundedRect(const Rect& rect, const Color& color, float radius) override;
    void drawShadow(const Rect& rect, const Color& color, float radius, float offsetY,
                    float softness) override;
    void drawTexture(const Rect& rect, const Texture& texture, const Color& tint,
                     const Vec2& uv0 = {0.0f, 0.0f},
                     const Vec2& uv1 = {1.0f, 1.0f}) override;
    void drawTextureId(const Rect& rect, unsigned int textureId, const Color& tint,
                       const Vec2& uv0 = {0.0f, 0.0f},
                       const Vec2& uv1 = {1.0f, 1.0f}) override;
    void drawText(Font& font, const std::string& text, const Vec2& pos,
                  const Color& color) override;

    // Cheap fullscreen CRT/scanline/vignette backdrop. Drawn right after
    // beginFrame so it sits behind all screens. Pure single-quad shader.
    void drawBackground(const Color& tint, float timeSeconds);

    // Fullscreen flowing-scanline overlay drawn ON TOP of the UI. Caller skips
    // this over fullscreen video so footage stays clean. Pure single-quad shader.
    void drawScanlines(const Color& tint, float timeSeconds);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    void drawRectEx(const Rect& rect, const Color& color, float radius, float soft);

    Shader rectShader_;
    Shader texShader_;
    Shader bgShader_;
    Shader scanShader_;

    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace cyberdeck
