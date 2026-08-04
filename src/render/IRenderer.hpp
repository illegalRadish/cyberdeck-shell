#pragma once

#include "core/Types.hpp"

#include <string>

namespace cyberdeck {

class Texture;
class Font;

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init(int width, int height) = 0;
    virtual void shutdown() = 0;

    virtual void beginFrame(const Color& clearColor) = 0;
    virtual void endFrame() = 0;
    virtual void bindUiState() {}

    virtual void setViewport(int width, int height) = 0;
    virtual void drawRect(const Rect& rect, const Color& color) = 0;

    // Rounded-rect + soft shadow helpers. Default implementations fall back to
    // plain rects so non-GL renderers keep working unchanged.
    virtual void drawRoundedRect(const Rect& rect, const Color& color, float radius) {
        (void)radius;
        drawRect(rect, color);
    }
    virtual void drawShadow(const Rect& rect, const Color& color, float radius,
                            float offsetY, float softness) {
        (void)radius;
        (void)offsetY;
        (void)softness;
        drawRect(rect, color);
    }

    // uv = (u0, v0, u1, v1); default full texture.
    virtual void drawTexture(const Rect& rect, const Texture& texture, const Color& tint,
                             const Vec2& uv0 = {0.0f, 0.0f},
                             const Vec2& uv1 = {1.0f, 1.0f}) = 0;

    virtual void drawTextureId(const Rect& rect, unsigned int textureId, const Color& tint,
                               const Vec2& uv0 = {0.0f, 0.0f},
                               const Vec2& uv1 = {1.0f, 1.0f}) = 0;

    virtual void drawText(Font& font, const std::string& text, const Vec2& pos,
                          const Color& color) = 0;
};

}  // namespace cyberdeck
