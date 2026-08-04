#pragma once

#include "media/IPlayer.hpp"

#include <memory>

struct mpv_handle;
struct mpv_render_context;

namespace cyberdeck {

class MpvPlayer final : public IPlayer {
public:
    MpvPlayer() = default;
    ~MpvPlayer() override;

    bool init() override;
    void shutdown() override;
    void update() override;

    bool play(const MediaItem& item, bool videoEnabled) override;
    bool playFromQueue(bool videoEnabled) override;
    void pause(bool paused) override;
    void togglePause() override;
    void stop() override;
    void seekRelative(double seconds) override;
    void seekAbsolute(double seconds) override;

    void setQueue(std::vector<MediaItem> items, int startIndex) override;
    void playNext() override;
    void playPrevious() override;

    PlayerStatus status() const override { return status_; }
    const std::vector<MediaItem>& queue() const override { return queue_; }
    int queueIndex() const override { return queueIndex_; }

    unsigned int videoTexture() const override { return videoTex_; }
    int videoWidth() const override { return videoW_; }
    int videoHeight() const override { return videoH_; }
    void renderVideoFrame(int viewportW, int viewportH) override;

private:
    bool ensureRenderContext();
    void destroyRenderContext();
    void loadCurrent(bool videoEnabled);
    double getDouble(const char* name) const;
    bool getFlag(const char* name) const;
    void setProperty(const char* name, const char* value);
    void command(const char** args);

    mpv_handle* mpv_ = nullptr;
    mpv_render_context* render_ = nullptr;
    PlayerStatus status_{};
    std::vector<MediaItem> queue_;
    int queueIndex_ = 0;
    bool videoEnabled_ = false;
    bool renderReady_ = false;

    unsigned int videoTex_ = 0;
    unsigned int fbo_ = 0;
    int videoW_ = 0;
    int videoH_ = 0;
};

}  // namespace cyberdeck
