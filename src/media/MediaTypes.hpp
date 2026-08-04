#pragma once

#include <string>

namespace cyberdeck {

enum class MediaType {
    Music,
    Movie,
    TvShow,
    Photo,
    Video,
    Book,
    Rom,
    Download,
    Other,
};

const char* mediaTypeToString(MediaType type);
MediaType mediaTypeFromString(const std::string& s);
MediaType mediaTypeFromRelativePath(const std::string& relativePath);
MediaType mediaTypeFromExtension(const std::string& extension);

bool isSupportedMediaExtension(const std::string& extension);

}  // namespace cyberdeck
