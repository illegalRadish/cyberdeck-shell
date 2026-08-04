#include "media/ThumbnailCache.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>

namespace cyberdeck {

namespace fs = std::filesystem;

ThumbnailCache::ThumbnailCache(std::string thumbsDir) : thumbsDir_(std::move(thumbsDir)) {
    std::error_code ec;
    fs::create_directories(thumbsDir_, ec);
}

std::string ThumbnailCache::thumbPathFor(const std::string& sourcePath) const {
    std::hash<std::string> hash;
    const auto value = hash(sourcePath);
    char name[64];
    std::snprintf(name, sizeof(name), "%016zx.png", static_cast<std::size_t>(value));
    return (fs::path(thumbsDir_) / name).string();
}

bool ThumbnailCache::generateImageThumb(const std::string& sourcePath,
                                        const std::string& destPath) const {
    SDL_Surface* loaded = IMG_Load(sourcePath.c_str());
    if (!loaded) {
        return false;
    }

    SDL_Surface* src = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!src) {
        return false;
    }

    const float scale =
        std::min(1.0f, static_cast<float>(maxSize_) /
                           static_cast<float>(std::max(src->w, src->h)));
    const int tw = std::max(1, static_cast<int>(src->w * scale));
    const int th = std::max(1, static_cast<int>(src->h * scale));

    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_RGBA32);
    if (!dst) {
        SDL_FreeSurface(src);
        return false;
    }

    SDL_BlitScaled(src, nullptr, dst, nullptr);
    SDL_FreeSurface(src);

    const bool ok = IMG_SavePNG(dst, destPath.c_str()) == 0;
    SDL_FreeSurface(dst);
    if (!ok) {
        std::cerr << "Thumbnail save failed: " << destPath << " — " << IMG_GetError() << '\n';
    }
    return ok;
}

std::string ThumbnailCache::ensureThumbnail(const std::string& sourcePath, MediaType type) {
    if (type != MediaType::Photo) {
        // Video/movie posters come later with mpv/ffmpeg in Phase 5.
        return {};
    }

    const std::string dest = thumbPathFor(sourcePath);
    std::error_code ec;
    if (fs::exists(dest, ec)) {
        return dest;
    }
    if (generateImageThumb(sourcePath, dest)) {
        return dest;
    }
    return {};
}

}  // namespace cyberdeck
