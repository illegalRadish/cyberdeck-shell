#pragma once

#include "media/MediaTypes.hpp"

#include <string>

namespace cyberdeck {

class ThumbnailCache {
public:
    explicit ThumbnailCache(std::string thumbsDir);

    // Returns existing thumb path, or generates one for images. Empty if unavailable.
    std::string ensureThumbnail(const std::string& sourcePath, MediaType type);

    const std::string& directory() const { return thumbsDir_; }

private:
    std::string thumbPathFor(const std::string& sourcePath) const;
    bool generateImageThumb(const std::string& sourcePath, const std::string& destPath) const;

    std::string thumbsDir_;
    int maxSize_ = 256;
};

}  // namespace cyberdeck
