#pragma once

#include <optional>
#include <string>

namespace cyberdeck {

struct MediaRootInfo {
    std::string path;       // absolute path to PI LIB
    std::string cacheDir;   // PI LIB/.cyberdeck
    std::string dbPath;     // .../library.db
    std::string thumbsDir;  // .../thumbs
    bool found = false;
};

// Finds a folder named "PI LIB" on macOS and Linux.
// Override with CYBERDECK_MEDIA_ROOT if set.
MediaRootInfo discoverMediaRoot();

}  // namespace cyberdeck
