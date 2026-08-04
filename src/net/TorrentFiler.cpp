#include "net/TorrentFiler.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace cyberdeck::torrentfiler {

namespace fs = std::filesystem;

namespace {

// Release tags that mark the end of a title. Everything from the first one
// onward is noise: "Movie Name 1080p BluRay x264" is a title of two words.
constexpr const char* kQualityTags[] = {
    "2160p", "1080p", "720p", "480p", "4k", "uhd", "bluray", "blu-ray", "brrip",
    "bdrip", "webrip", "web-dl", "webdl", "hdtv", "dvdrip", "dvdscr", "hdrip",
    "x264", "x265", "h264", "h265", "hevc", "xvid", "divx", "aac", "ac3", "dts",
    "remux", "proper", "repack", "extended", "unrated", "internal", "limited",
    "multi", "dubbed", "subbed", "flac", "mp3", "320kbps",
};

// Never counted when deciding what a payload contains.
constexpr const char* kJunkExtensions[] = {
    "nfo", "txt", "url", "sfv", "md5", "srt", "sub", "idx", "exe", "db", "ds_store",
    "torrent", "jpg_bak", "part", "log", "diz",
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// A token boundary for tag and year matching. Scene names separate with dots,
// spaces, underscores and brackets more or less interchangeably.
bool isSeparator(char c) {
    return c == '.' || c == ' ' || c == '_' || c == '-' || c == '[' || c == ']' ||
           c == '(' || c == ')' || c == '{' || c == '}';
}

bool tokenAt(const std::string& lowerName, std::size_t pos, const std::string& token) {
    if (lowerName.compare(pos, token.size(), token) != 0) {
        return false;
    }
    const bool leftOk = pos == 0 || isSeparator(lowerName[pos - 1]);
    const std::size_t after = pos + token.size();
    const bool rightOk = after >= lowerName.size() || isSeparator(lowerName[after]);
    return leftOk && rightOk;
}

// Offset of the first release tag, or npos. Marks where a title stops.
std::size_t firstTagOffset(const std::string& lowerName) {
    std::size_t best = std::string::npos;
    for (const char* tag : kQualityTags) {
        const std::string token(tag);
        for (std::size_t pos = lowerName.find(token); pos != std::string::npos;
             pos = lowerName.find(token, pos + 1)) {
            if (tokenAt(lowerName, pos, token)) {
                best = std::min(best, pos);
                break;
            }
        }
    }
    return best;
}

// Offset and value of a delimited 4-digit year, searching from the right so
// that "2 Fast 2 Furious 2003" keeps the release year rather than the title's.
bool findYear(const std::string& name, std::size_t& offsetOut, int& yearOut) {
    if (name.size() < 4) {
        return false;
    }
    for (std::size_t i = name.size() - 4 + 1; i-- > 0;) {
        if (!isDigit(name[i]) || !isDigit(name[i + 1]) || !isDigit(name[i + 2]) ||
            !isDigit(name[i + 3])) {
            continue;
        }
        const bool leftOk = i == 0 || isSeparator(name[i - 1]);
        const std::size_t after = i + 4;
        const bool rightOk = after >= name.size() || isSeparator(name[after]);
        if (!leftOk || !rightOk) {
            continue;
        }
        const int year = (name[i] - '0') * 1000 + (name[i + 1] - '0') * 100 +
                         (name[i + 2] - '0') * 10 + (name[i + 3] - '0');
        if (year >= 1900 && year <= 2099) {
            offsetOut = i;
            yearOut = year;
            return true;
        }
    }
    return false;
}

std::string extensionOf(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    const std::size_t slash = name.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    return name.substr(dot + 1);
}

bool isJunkFile(const std::string& filename) {
    const std::string lowerName = lower(filename);
    if (lowerName.find("sample") != std::string::npos) {
        return true;
    }
    const std::string ext = lower(extensionOf(lowerName));
    for (const char* junk : kJunkExtensions) {
        if (ext == junk) {
            return true;
        }
    }
    return false;
}

// Reads "S01E03" / "s1e3" style markers at `pos`. Returns the season.
bool matchSeasonEpisode(const std::string& lowerName, std::size_t pos, int& seasonOut) {
    if (lowerName[pos] != 's') {
        return false;
    }
    // The 's' must start a token, so an ordinary word ending in "...s01e..."
    // cannot match. No boundary check on the tail: multi-episode names like
    // "S01E03E04" are common and must still parse.
    if (pos > 0 && !isSeparator(lowerName[pos - 1])) {
        return false;
    }
    std::size_t i = pos + 1;
    std::size_t digits = 0;
    int season = 0;
    while (i < lowerName.size() && isDigit(lowerName[i]) && digits < 2) {
        season = season * 10 + (lowerName[i] - '0');
        ++i;
        ++digits;
    }
    if (digits == 0 || i >= lowerName.size()) {
        return false;
    }
    if (lowerName[i] != 'e') {
        return false;
    }
    ++i;
    std::size_t epDigits = 0;
    while (i < lowerName.size() && isDigit(lowerName[i]) && epDigits < 3) {
        ++i;
        ++epDigits;
    }
    if (epDigits == 0) {
        return false;
    }
    seasonOut = season;
    return true;
}

// Reads "1x03" style markers ending at `pos`, where `pos` is the 'x'.
//
// Both ends must sit on a token boundary. Without that, a resolution such as
// "1920x1080" matches as season 20 episode 108 and every film tagged with its
// dimensions is filed as television.
bool matchCrossEpisode(const std::string& lowerName, std::size_t pos, int& seasonOut,
                       std::size_t& startOut) {
    if (lowerName[pos] != 'x' || pos == 0) {
        return false;
    }
    std::size_t start = pos;
    std::size_t digits = 0;
    while (start > 0 && isDigit(lowerName[start - 1]) && digits < 2) {
        --start;
        ++digits;
    }
    if (digits == 0 || (start > 0 && !isSeparator(lowerName[start - 1]))) {
        return false;
    }
    std::size_t i = pos + 1;
    std::size_t epDigits = 0;
    while (i < lowerName.size() && isDigit(lowerName[i]) && epDigits < 3) {
        ++i;
        ++epDigits;
    }
    if (epDigits < 2 || (i < lowerName.size() && !isSeparator(lowerName[i]))) {
        return false;
    }
    seasonOut = std::stoi(lowerName.substr(start, digits));
    startOut = start;
    return true;
}

}  // namespace

std::string cleanTitle(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        out.push_back(c == '.' || c == '_' ? ' ' : c);
    }

    // Collapse whitespace runs.
    std::string collapsed;
    collapsed.reserve(out.size());
    bool lastSpace = false;
    for (char c : out) {
        const bool space = c == ' ' || c == '\t';
        if (space && lastSpace) {
            continue;
        }
        collapsed.push_back(space ? ' ' : c);
        lastSpace = space;
    }

    // Trim, including trailing separator debris left by cutting at a tag.
    std::size_t begin = collapsed.find_first_not_of(" -");
    if (begin == std::string::npos) {
        return {};
    }
    std::size_t end = collapsed.find_last_not_of(" -([{");
    return collapsed.substr(begin, end - begin + 1);
}

EpisodeInfo parseEpisode(const std::string& name) {
    const std::string lowerName = lower(name);
    EpisodeInfo info;

    for (std::size_t i = 0; i < lowerName.size(); ++i) {
        int season = 0;
        if (matchSeasonEpisode(lowerName, i, season)) {
            info.season = season;
            info.show = cleanTitle(name.substr(0, i));
            info.valid = true;
            return info;
        }
        std::size_t start = 0;
        if (matchCrossEpisode(lowerName, i, season, start)) {
            info.season = season;
            info.show = cleanTitle(name.substr(0, start));
            info.valid = true;
            return info;
        }
    }

    // Season packs carry no episode number: "Some Show Season 2 COMPLETE".
    const std::string marker = "season";
    for (std::size_t pos = lowerName.find(marker); pos != std::string::npos;
         pos = lowerName.find(marker, pos + 1)) {
        std::size_t i = pos + marker.size();
        while (i < lowerName.size() && isSeparator(lowerName[i])) {
            ++i;
        }
        int season = 0;
        std::size_t digits = 0;
        while (i < lowerName.size() && isDigit(lowerName[i]) && digits < 2) {
            season = season * 10 + (lowerName[i] - '0');
            ++i;
            ++digits;
        }
        if (digits > 0) {
            info.season = season;
            info.show = cleanTitle(name.substr(0, pos));
            info.valid = true;
            return info;
        }
    }

    return info;
}

std::string movieFolderName(const std::string& name) {
    const std::string lowerName = lower(name);
    const std::size_t tagOffset = firstTagOffset(lowerName);

    std::size_t yearOffset = 0;
    int year = 0;
    if (findYear(name, yearOffset, year)) {
        // A year after the first release tag belongs to the tag soup
        // ("...x264-GROUP.2019.REPACK"), not to the title.
        if (tagOffset == std::string::npos || yearOffset < tagOffset) {
            const std::string title = cleanTitle(name.substr(0, yearOffset));
            if (!title.empty()) {
                return title + " (" + std::to_string(year) + ")";
            }
        }
    }

    if (tagOffset != std::string::npos) {
        const std::string title = cleanTitle(name.substr(0, tagOffset));
        if (!title.empty()) {
            return title;
        }
    }

    // Strip a trailing extension so single-file torrents do not become a
    // folder called "Movie mkv".
    const std::string ext = extensionOf(name);
    if (!ext.empty() && mediaTypeFromExtension(ext) != MediaType::Other) {
        return cleanTitle(name.substr(0, name.size() - ext.size() - 1));
    }
    return cleanTitle(name);
}

MediaType routeFromName(const std::string& name) {
    if (name.empty()) {
        return MediaType::Download;
    }
    if (parseEpisode(name).valid) {
        return MediaType::TvShow;
    }

    // A torrent named after a single file is the clearest signal there is.
    const std::string ext = extensionOf(name);
    if (!ext.empty()) {
        const MediaType byExt = mediaTypeFromExtension(ext);
        switch (byExt) {
            case MediaType::Video:
                return MediaType::Movie;  // no episode marker, so not a show
            case MediaType::Music:
            case MediaType::Photo:
            case MediaType::Book:
            case MediaType::Rom:
                return byExt;
            default:
                break;
        }
    }

    const std::string lowerName = lower(name);
    if (lowerName.find("discography") != std::string::npos ||
        lowerName.find("flac") != std::string::npos ||
        lowerName.find("[mp3") != std::string::npos ||
        lowerName.find("320kbps") != std::string::npos) {
        return MediaType::Music;
    }

    // Video release tags, or a bare year, mean a film.
    if (firstTagOffset(lowerName) != std::string::npos) {
        return MediaType::Movie;
    }
    std::size_t yearOffset = 0;
    int year = 0;
    if (findYear(name, yearOffset, year) && yearOffset > 0) {
        return MediaType::Movie;
    }

    return MediaType::Download;
}

MediaType routeFromPayload(const std::string& payloadPath, MediaType nameRoute) {
    // An episode marker in the name is stronger evidence than file extensions,
    // which only ever say "video" and cannot tell a film from a show.
    if (nameRoute == MediaType::TvShow) {
        return nameRoute;
    }

    std::error_code ec;
    if (payloadPath.empty() || !fs::exists(payloadPath, ec)) {
        return nameRoute;
    }

    std::map<MediaType, std::int64_t> bytesByType;
    std::int64_t largest = 0;
    std::vector<std::pair<std::string, std::int64_t>> files;

    auto consider = [&](const fs::path& path) {
        std::error_code sizeEc;
        const auto size = fs::file_size(path, sizeEc);
        if (sizeEc) {
            return;
        }
        const std::string filename = path.filename().string();
        if (isJunkFile(filename)) {
            return;
        }
        const auto bytes = static_cast<std::int64_t>(size);
        largest = std::max(largest, bytes);
        files.emplace_back(filename, bytes);
    };

    if (fs::is_directory(payloadPath, ec)) {
        for (fs::recursive_directory_iterator it(payloadPath, ec), end; it != end;
             it.increment(ec)) {
            if (ec) {
                break;
            }
            if (it->is_regular_file(ec)) {
                consider(it->path());
            }
        }
    } else {
        consider(payloadPath);
    }

    if (files.empty()) {
        return nameRoute;
    }

    // Cover art and liner notes next to a 20GB remux are not evidence about
    // what the torrent is. Anything under 1% of the largest file is ignored —
    // which leaves a photo pack, where every file is comparable, untouched.
    const std::int64_t floor = largest / 100;
    for (const auto& [filename, bytes] : files) {
        if (bytes < floor) {
            continue;
        }
        bytesByType[mediaTypeFromExtension(extensionOf(filename))] += bytes;
    }

    MediaType dominant = MediaType::Other;
    std::int64_t best = 0;
    for (const auto& [type, bytes] : bytesByType) {
        if (type != MediaType::Other && bytes > best) {
            best = bytes;
            dominant = type;
        }
    }

    switch (dominant) {
        case MediaType::Video:
            // Videos with no episode marker anywhere are films.
            return MediaType::Movie;
        case MediaType::Music:
        case MediaType::Photo:
        case MediaType::Book:
        case MediaType::Rom:
            return dominant;
        default:
            return nameRoute;
    }
}

const char* folderForRoute(MediaType route) {
    switch (route) {
        case MediaType::Music:
            return "Music";
        case MediaType::Movie:
            return "Movies";
        case MediaType::TvShow:
            return "TV Shows";
        case MediaType::Photo:
            return "Photos";
        case MediaType::Video:
            return "Videos";
        case MediaType::Book:
            return "Books";
        case MediaType::Rom:
            return "ROMs";
        default:
            return nullptr;  // Download / Other: leave it where it landed
    }
}

std::string destinationDir(const std::string& mediaRoot, MediaType route,
                           const std::string& name) {
    const char* folder = folderForRoute(route);
    if (!folder || mediaRoot.empty()) {
        return {};
    }
    fs::path dest = fs::path(mediaRoot) / folder;

    switch (route) {
        case MediaType::TvShow: {
            const EpisodeInfo episode = parseEpisode(name);
            if (episode.valid && !episode.show.empty()) {
                char season[16];
                std::snprintf(season, sizeof(season), "Season %02d", episode.season);
                dest /= episode.show;
                dest /= season;
            }
            break;
        }
        case MediaType::Movie: {
            const std::string title = movieFolderName(name);
            if (!title.empty()) {
                dest /= title;
            }
            break;
        }
        case MediaType::Music:
        case MediaType::Photo:
        case MediaType::Video: {
            const std::string title = cleanTitle(name);
            if (!title.empty()) {
                dest /= title;
            }
            break;
        }
        default:
            // Books and ROMs are flat collections; a folder per torrent there
            // just buries single files one level deeper.
            break;
    }

    return dest.string();
}

}  // namespace cyberdeck::torrentfiler
