#pragma once

#include "media/MediaDatabase.hpp"
#include "media/MediaRoot.hpp"
#include "media/MediaScanner.hpp"
#include "media/ThumbnailCache.hpp"

#include <memory>
#include <string>

namespace cyberdeck {

class MediaLibrary {
public:
    MediaLibrary() = default;
    ~MediaLibrary() = default;

    bool initialize();
    void shutdown();

    bool ready() const { return ready_; }
    bool rootFound() const { return root_.found; }
    const MediaRootInfo& root() const { return root_; }

    MediaDatabase& db() { return db_; }
    const MediaDatabase& db() const { return db_; }
    MediaScanner& scanner() { return *scanner_; }
    const ScanProgress scanProgress() const {
        return scanner_ ? scanner_->progress() : ScanProgress{};
    }

    void startScan();
    std::string statusLine() const;

private:
    MediaRootInfo root_{};
    MediaDatabase db_;
    std::unique_ptr<ThumbnailCache> thumbs_;
    std::unique_ptr<MediaScanner> scanner_;
    bool ready_ = false;
};

}  // namespace cyberdeck
