#include "core/Assets.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace cyberdeck::assets {

std::string resolve(const std::string& relative) {
#ifdef CYBERDECK_ASSET_DIR
    const std::filesystem::path compiled =
        std::filesystem::path(CYBERDECK_ASSET_DIR) / relative;
    if (std::filesystem::exists(compiled)) {
        return compiled.string();
    }
#endif
    const std::filesystem::path local = std::filesystem::path("assets") / relative;
    if (std::filesystem::exists(local)) {
        return local.string();
    }
    return local.string();
}

std::string findFont(const std::string& preferredName) {
    const std::vector<std::string> candidates = {
        resolve("fonts/" + preferredName),
        resolve("fonts/VT323-Regular.ttf"),
        resolve("fonts/Inter-Regular.ttf"),
        resolve("fonts/DejaVuSans.ttf"),
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };

    for (const auto& path : candidates) {
        if (!path.empty() && std::filesystem::exists(path)) {
            return path;
        }
    }

    std::cerr << "assets::findFont: no font found\n";
    return {};
}

}  // namespace cyberdeck::assets
