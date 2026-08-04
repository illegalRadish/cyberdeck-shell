#pragma once

#include "media/MediaDatabase.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cyberdeck {

struct PlayerStatus {
    bool ready = false;
    bool playing = false;
    bool paused = false;
    bool hasVideo = false;
    bool ended = false;
    double positionSec = 0.0;
    double durationSec = 0.0;
    std::string title;
    std::string path;
    std::string error;
};

class IPlayer {
public:
    virtual ~IPlayer() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void update() = 0;

    virtual bool play(const MediaItem& item, bool videoEnabled) = 0;
    virtual bool playFromQueue(bool videoEnabled) = 0;
    virtual void pause(bool paused) = 0;
    virtual void togglePause() = 0;
    virtual void stop() = 0;
    virtual void seekRelative(double seconds) = 0;
    virtual void seekAbsolute(double seconds) = 0;

    virtual void setQueue(std::vector<MediaItem> items, int startIndex) = 0;
    virtual void playNext() = 0;
    virtual void playPrevious() = 0;

    virtual PlayerStatus status() const = 0;
    virtual const std::vector<MediaItem>& queue() const = 0;
    virtual int queueIndex() const = 0;

    // Optional GL texture id for video frames (0 if unavailable).
    virtual unsigned int videoTexture() const { return 0; }
    virtual int videoWidth() const { return 0; }
    virtual int videoHeight() const { return 0; }
    virtual void renderVideoFrame(int viewportW, int viewportH) { (void)viewportW; (void)viewportH; }
};

}  // namespace cyberdeck
