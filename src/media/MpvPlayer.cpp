#include "media/MpvPlayer.hpp"

#include "render/GL.hpp"

#include <SDL2/SDL.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>

#include <algorithm>
#include <cstdio>
#include <iostream>

namespace cyberdeck {

namespace {

void* getProcAddress(void* /*ctx*/, const char* name) {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

}  // namespace

MpvPlayer::~MpvPlayer() {
    shutdown();
}

bool MpvPlayer::init() {
    if (mpv_) {
        return true;
    }

    mpv_ = mpv_create();
    if (!mpv_) {
        status_.error = "mpv_create failed";
        return false;
    }

    setProperty("terminal", "no");
    setProperty("input-default-bindings", "no");
    setProperty("input-vo-keyboard", "no");
    setProperty("osc", "no");
    setProperty("osd-level", "0");
    setProperty("keep-open", "yes");
    setProperty("idle", "yes");

    if (mpv_initialize(mpv_) < 0) {
        status_.error = "mpv_initialize failed";
        shutdown();
        return false;
    }

    status_.ready = true;
    status_.error.clear();
    std::cout << "libmpv ready\n";
    return true;
}

void MpvPlayer::shutdown() {
    stop();
    destroyRenderContext();
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    status_ = PlayerStatus{};
    queue_.clear();
    queueIndex_ = 0;
}

void MpvPlayer::setProperty(const char* name, const char* value) {
    if (!mpv_) {
        return;
    }
    mpv_set_property_string(mpv_, name, value);
}

void MpvPlayer::command(const char** args) {
    if (!mpv_) {
        return;
    }
    mpv_command_async(mpv_, 0, args);
}

double MpvPlayer::getDouble(const char* name) const {
    if (!mpv_) {
        return 0.0;
    }
    double value = 0.0;
    mpv_get_property(mpv_, name, MPV_FORMAT_DOUBLE, &value);
    return value;
}

bool MpvPlayer::getFlag(const char* name) const {
    if (!mpv_) {
        return false;
    }
    int flag = 0;
    mpv_get_property(mpv_, name, MPV_FORMAT_FLAG, &flag);
    return flag != 0;
}

bool MpvPlayer::ensureRenderContext() {
    if (renderReady_) {
        return true;
    }
    if (!mpv_) {
        return false;
    }

    mpv_opengl_init_params glInit {};
    glInit.get_proc_address = getProcAddress;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    if (mpv_render_context_create(&render_, mpv_, params) < 0) {
        std::cerr << "mpv_render_context_create failed — video surface disabled\n";
        render_ = nullptr;
        renderReady_ = false;
        return false;
    }

    glGenTextures(1, &videoTex_);
    glBindTexture(GL_TEXTURE_2D, videoTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    renderReady_ = true;
    return true;
}

void MpvPlayer::destroyRenderContext() {
    if (render_) {
        mpv_render_context_free(render_);
        render_ = nullptr;
    }
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (videoTex_) {
        glDeleteTextures(1, &videoTex_);
        videoTex_ = 0;
    }
    renderReady_ = false;
    videoW_ = 0;
    videoH_ = 0;
}

void MpvPlayer::loadCurrent(bool videoEnabled) {
    if (!mpv_ || queue_.empty() || queueIndex_ < 0 ||
        queueIndex_ >= static_cast<int>(queue_.size())) {
        return;
    }

    videoEnabled_ = videoEnabled;
    const MediaItem& item = queue_[static_cast<std::size_t>(queueIndex_)];

    if (videoEnabled) {
        setProperty("vid", "auto");
        setProperty("vo", "libmpv");
        ensureRenderContext();
    } else {
        setProperty("vid", "no");
        setProperty("vo", "null");
    }

    const char* cmd[] = {"loadfile", item.path.c_str(), "replace", nullptr};
    command(cmd);

    status_.path = item.path;
    status_.title = item.name;
    status_.playing = true;
    status_.paused = false;
    status_.ended = false;
    status_.hasVideo = videoEnabled && renderReady_;
    status_.error.clear();
}

bool MpvPlayer::play(const MediaItem& item, bool videoEnabled) {
    if (!mpv_ && !init()) {
        return false;
    }
    queue_ = {item};
    queueIndex_ = 0;
    loadCurrent(videoEnabled);
    return true;
}

bool MpvPlayer::playFromQueue(bool videoEnabled) {
    if (!mpv_ && !init()) {
        return false;
    }
    if (queue_.empty()) {
        return false;
    }
    loadCurrent(videoEnabled);
    return true;
}

void MpvPlayer::setQueue(std::vector<MediaItem> items, int startIndex) {
    queue_ = std::move(items);
    queueIndex_ = 0;
    if (!queue_.empty()) {
        queueIndex_ = std::clamp(startIndex, 0, static_cast<int>(queue_.size()) - 1);
    }
}

void MpvPlayer::playNext() {
    if (queue_.empty()) {
        return;
    }
    queueIndex_ = (queueIndex_ + 1) % static_cast<int>(queue_.size());
    loadCurrent(videoEnabled_);
}

void MpvPlayer::playPrevious() {
    if (queue_.empty()) {
        return;
    }
    queueIndex_ = (queueIndex_ == 0) ? static_cast<int>(queue_.size()) - 1 : queueIndex_ - 1;
    loadCurrent(videoEnabled_);
}

void MpvPlayer::pause(bool paused) {
    if (!mpv_) {
        return;
    }
    int flag = paused ? 1 : 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &flag);
    status_.paused = paused;
    status_.playing = !paused;
}

void MpvPlayer::togglePause() {
    pause(!status_.paused);
}

void MpvPlayer::stop() {
    if (!mpv_) {
        return;
    }
    const char* cmd[] = {"stop", nullptr};
    command(cmd);
    status_.playing = false;
    status_.paused = false;
    status_.positionSec = 0.0;
}

void MpvPlayer::seekRelative(double seconds) {
    if (!mpv_) {
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%f", seconds);
    const char* cmd[] = {"seek", buf, "relative", nullptr};
    command(cmd);
}

void MpvPlayer::seekAbsolute(double seconds) {
    if (!mpv_) {
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%f", seconds);
    const char* cmd[] = {"seek", buf, "absolute", nullptr};
    command(cmd);
}

void MpvPlayer::update() {
    if (!mpv_) {
        return;
    }

    while (true) {
        mpv_event* event = mpv_wait_event(mpv_, 0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }
        if (event->event_id == MPV_EVENT_END_FILE) {
            auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end && end->reason == MPV_END_FILE_REASON_EOF) {
                status_.ended = true;
                if (queue_.size() > 1) {
                    playNext();
                } else {
                    status_.playing = false;
                }
            }
        } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
            status_.ended = false;
        }
    }

    status_.positionSec = getDouble("time-pos");
    status_.durationSec = getDouble("duration");
    status_.paused = getFlag("pause");
    status_.playing = !status_.paused && !status_.path.empty() && !status_.ended;
    if (status_.title.empty() && !status_.path.empty()) {
        if (char* mediaTitle = mpv_get_property_string(mpv_, "media-title")) {
            status_.title = mediaTitle;
            mpv_free(mediaTitle);
        }
    }
}

void MpvPlayer::renderVideoFrame(int viewportW, int viewportH) {
    if (!render_ || !videoTex_ || !fbo_ || viewportW <= 0 || viewportH <= 0) {
        return;
    }

    if (videoW_ != viewportW || videoH_ != viewportH) {
        videoW_ = viewportW;
        videoH_ = viewportH;
        glBindTexture(GL_TEXTURE_2D, videoTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, videoW_, videoH_, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, videoTex_, 0);

    mpv_opengl_fbo fboInfo {};
    fboInfo.fbo = static_cast<int>(fbo_);
    fboInfo.w = videoW_;
    fboInfo.h = videoH_;
    fboInfo.internal_format = GL_RGBA8;

    int flipY = 1;  // OpenGL FBO convention (mpv recommendation)
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fboInfo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(render_, params);

    // mpv mutates global GL state; leave a clean default framebuffer bound.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glUseProgram(0);
}

}  // namespace cyberdeck
