#include "media/MediaRoot.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace cyberdeck {

namespace fs = std::filesystem;

namespace {

constexpr const char* kLibName = "PI LIB";

bool isPiLibDir(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_directory(path, ec)) {
        return false;
    }
    return path.filename() == kLibName;
}

MediaRootInfo makeRootInfo(const fs::path& root) {
    MediaRootInfo info;
    info.path = fs::weakly_canonical(root).string();
    info.cacheDir = (fs::path(info.path) / ".cyberdeck").string();
    info.dbPath = (fs::path(info.cacheDir) / "library.db").string();
    info.thumbsDir = (fs::path(info.cacheDir) / "thumbs").string();
    info.found = true;

    std::error_code ec;
    fs::create_directories(info.thumbsDir, ec);
    return info;
}

std::optional<fs::path> findInDirectory(const fs::path& parent) {
    std::error_code ec;
    if (!fs::is_directory(parent, ec)) {
        return std::nullopt;
    }
    const fs::path candidate = parent / kLibName;
    if (isPiLibDir(candidate)) {
        return candidate;
    }
    return std::nullopt;
}

std::optional<fs::path> scanMountRoots(const std::vector<fs::path>& mounts) {
    for (const auto& mountRoot : mounts) {
        std::error_code ec;
        if (!fs::is_directory(mountRoot, ec)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(mountRoot, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_directory()) {
                continue;
            }
            if (auto found = findInDirectory(entry.path())) {
                return found;
            }
            // Also allow the volume itself to be named PI LIB.
            if (isPiLibDir(entry.path())) {
                return entry.path();
            }
        }
    }
    return std::nullopt;
}

}  // namespace

MediaRootInfo discoverMediaRoot() {
    if (const char* env = std::getenv("CYBERDECK_MEDIA_ROOT")) {
        const fs::path envPath(env);
        if (fs::is_directory(envPath)) {
            std::cout << "Media root from CYBERDECK_MEDIA_ROOT: " << envPath << '\n';
            return makeRootInfo(envPath);
        }
        std::cerr << "CYBERDECK_MEDIA_ROOT is not a directory: " << env << '\n';
    }

    std::vector<fs::path> candidates;

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        candidates.push_back(cwd / kLibName);
        // Walk up a few parents (useful when launched from build/).
        fs::path walk = cwd;
        for (int i = 0; i < 5; ++i) {
            candidates.push_back(walk / kLibName);
            if (!walk.has_parent_path() || walk == walk.parent_path()) {
                break;
            }
            walk = walk.parent_path();
        }
    }

    if (const char* home = std::getenv("HOME")) {
        candidates.push_back(fs::path(home) / kLibName);
        candidates.push_back(fs::path(home) / "Desktop" / kLibName);
        candidates.push_back(fs::path(home) / "Documents" / kLibName);
    }

    for (const auto& candidate : candidates) {
        if (isPiLibDir(candidate)) {
            std::cout << "Found media root: " << candidate << '\n';
            return makeRootInfo(candidate);
        }
    }

    // External volumes / mounts (macOS + Pi / Linux).
    if (auto found = scanMountRoots({
            "/Volumes",
            "/media",
            "/mnt",
            "/run/media",
        })) {
        std::cout << "Found media root on volume: " << *found << '\n';
        return makeRootInfo(*found);
    }

    std::cerr << "PI LIB not found. Connect the media drive or set CYBERDECK_MEDIA_ROOT.\n";
    MediaRootInfo missing;
    missing.found = false;
    return missing;
}

}  // namespace cyberdeck
