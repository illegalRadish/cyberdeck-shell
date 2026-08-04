#include "render/GLRenderer.hpp"
#include "render/Font.hpp"
#include "render/GL.hpp"
#include "render/Texture.hpp"
#include "core/Assets.hpp"

#include <iostream>

namespace cyberdeck {

namespace {

const float kUnitQuad[] = {
    0.0f, 0.0f,
    1.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f,
};

}  // namespace

GLRenderer::~GLRenderer() {
    shutdown();
}

bool GLRenderer::init(int width, int height) {
    width_ = width;
    height_ = height;

    const std::string rectVert = assets::resolve("shaders/rect.vert");
    const std::string rectFrag = assets::resolve("shaders/rect.frag");
    if (!rectShader_.loadFromFiles(rectVert, rectFrag)) {
        std::cerr << "GLRenderer: failed to load rect shaders\n";
        return false;
    }

    const std::string texVert = assets::resolve("shaders/tex.vert");
    const std::string texFrag = assets::resolve("shaders/tex.frag");
    if (!texShader_.loadFromFiles(texVert, texFrag)) {
        std::cerr << "GLRenderer: failed to load tex shaders\n";
        return false;
    }

    const std::string bgVert = assets::resolve("shaders/bg.vert");
    const std::string bgFrag = assets::resolve("shaders/bg.frag");
    if (!bgShader_.loadFromFiles(bgVert, bgFrag)) {
        std::cerr << "GLRenderer: failed to load bg shaders\n";
        return false;
    }

    const std::string scanFrag = assets::resolve("shaders/scanlines.frag");
    if (!scanShader_.loadFromFiles(bgVert, scanFrag)) {
        std::cerr << "GLRenderer: failed to load scanline shaders\n";
        return false;
    }


    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kUnitQuad), kUnitQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
    glBindVertexArray(0);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    setViewport(width_, height_);
    return true;
}

void GLRenderer::shutdown() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    rectShader_.destroy();
    texShader_.destroy();
    bgShader_.destroy();
    scanShader_.destroy();
}


void GLRenderer::setViewport(int width, int height) {
    width_ = width;
    height_ = height;
    glViewport(0, 0, width_, height_);
}

void GLRenderer::bindUiState() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void GLRenderer::beginFrame(const Color& clearColor) {
    bindUiState();
    glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GLRenderer::endFrame() {}

void GLRenderer::drawRectEx(const Rect& rect, const Color& color, float radius, float soft) {
    bindUiState();
    rectShader_.use();
    rectShader_.setVec2("uViewport", static_cast<float>(width_), static_cast<float>(height_));
    rectShader_.setVec4("uRect", rect.x, rect.y, rect.w, rect.h);
    rectShader_.setVec4("uColor", color.r, color.g, color.b, color.a);
    rectShader_.setFloat("uRadius", radius);
    rectShader_.setFloat("uSoft", soft);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void GLRenderer::drawRect(const Rect& rect, const Color& color) {
    drawRectEx(rect, color, 0.0f, 1.0f);
}

void GLRenderer::drawRoundedRect(const Rect& rect, const Color& color, float radius) {
    drawRectEx(rect, color, radius, 1.0f);
}

void GLRenderer::drawShadow(const Rect& rect, const Color& color, float radius, float offsetY,
                            float softness) {
    // Expand the quad outward by the softness so the blurred falloff isn't clipped,
    // then evaluate the round-box SDF against the *original* box but with a large
    // negative soft value so alpha fades smoothly outside the shape.
    const float grow = softness;
    Rect expanded{rect.x - grow, rect.y - grow + offsetY, rect.w + grow * 2.0f,
                  rect.h + grow * 2.0f};
    drawRectEx(expanded, color, radius + grow, softness);
}

void GLRenderer::drawTexture(const Rect& rect, const Texture& texture, const Color& tint,
                             const Vec2& uv0, const Vec2& uv1) {
    if (!texture.valid()) {
        return;
    }
    drawTextureId(rect, texture.id(), tint, uv0, uv1);
}

void GLRenderer::drawTextureId(const Rect& rect, unsigned int textureId, const Color& tint,
                               const Vec2& uv0, const Vec2& uv1) {
    if (textureId == 0) {
        return;
    }

    bindUiState();
    texShader_.use();
    texShader_.setVec2("uViewport", static_cast<float>(width_), static_cast<float>(height_));
    texShader_.setVec4("uRect", rect.x, rect.y, rect.w, rect.h);
    texShader_.setVec4("uUV", uv0.x, uv0.y, uv1.x, uv1.y);
    texShader_.setVec4("uColor", tint.r, tint.g, tint.b, tint.a);
    texShader_.setInt("uTex", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void GLRenderer::drawBackground(const Color& tint, float timeSeconds) {
    bindUiState();
    bgShader_.use();
    bgShader_.setVec2("uViewport", static_cast<float>(width_), static_cast<float>(height_));
    bgShader_.setVec4("uRect", 0.0f, 0.0f, static_cast<float>(width_),
                      static_cast<float>(height_));
    bgShader_.setVec4("uColor", tint.r, tint.g, tint.b, tint.a);
    bgShader_.setFloat("uTime", timeSeconds);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void GLRenderer::drawScanlines(const Color& tint, float timeSeconds) {
    bindUiState();
    scanShader_.use();
    scanShader_.setVec2("uViewport", static_cast<float>(width_), static_cast<float>(height_));
    scanShader_.setVec4("uRect", 0.0f, 0.0f, static_cast<float>(width_),
                        static_cast<float>(height_));
    scanShader_.setVec4("uColor", tint.r, tint.g, tint.b, tint.a);
    scanShader_.setFloat("uTime", timeSeconds);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void GLRenderer::drawText(Font& font, const std::string& text, const Vec2& pos,
                          const Color& color) {
    font.draw(*this, text, pos, color);
}


}  // namespace cyberdeck
