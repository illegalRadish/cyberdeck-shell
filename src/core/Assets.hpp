#pragma once

#include <string>

namespace cyberdeck::assets {

// Resolve a path under assets/ (compiled CYBERDECK_ASSET_DIR, then ./assets).
std::string resolve(const std::string& relative);

// Prefer assets/fonts/* then common system fonts.
std::string findFont(const std::string& preferredName = "VT323-Regular.ttf");

}  // namespace cyberdeck::assets
