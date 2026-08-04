#pragma once

#include "media/MediaDatabase.hpp"
#include "media/MediaRoot.hpp"
#include "media/ThumbnailCache.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cyberdeck {

struct ScanProgress {
    bool running = false;
    bool finished = false;
    int filesSeen = 0;
    int filesUpdated = 0;
    int filesSkipped = 0;
    std::string currentPath;
    std::string message;
};

class MediaScanner {
public:
    MediaScanner(MediaDatabase& db, ThumbnailCache& thumbs);
    ~MediaScanner();

    void start(const MediaRootInfo& root);
    void requestStop();
    bool isRunning() const { return running_.load(); }

    ScanProgress progress() const;

private:
    void run(MediaRootInfo root);
    static std::string displayNameFromPath(const std::string& path);

    MediaDatabase& db_;
    ThumbnailCache& thumbs_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    mutable std::mutex progressMutex_;
    ScanProgress progress_;
};

}  // namespace cyberdeck
