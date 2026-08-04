#include "media/MediaTypes.hpp"

#include <algorithm>
#include <cctype>

namespace cyberdeck {

namespace {

std::string lowerExt(std::string ext) {
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Files that travel alongside media without being media: subtitles, release
// notes, checksums. Named explicitly rather than lumped in with unknown
// extensions, because an unknown extension inside a media folder is treated as
// media on purpose — that is what keeps containers missing from the table
// (.mpg, .flv, .ts) from disappearing out of their category.
bool isSidecarExtension(const std::string& extension) {
    const std::string ext = lowerExt(extension);
    return ext == "srt" || ext == "sub" || ext == "idx" || ext == "ass" || ext == "ssa" ||
           ext == "vtt" || ext == "nfo" || ext == "txt" || ext == "url" || ext == "sfv" ||
           ext == "md5" || ext == "log" || ext == "diz" || ext == "torrent" ||
           ext == "part" || ext == "db" || ext == "ds_store";
}

}  // namespace

const char* mediaTypeToString(MediaType type) {
    switch (type) {
        case MediaType::Music:
            return "music";
        case MediaType::Movie:
            return "movie";
        case MediaType::TvShow:
            return "tv";
        case MediaType::Photo:
            return "photo";
        case MediaType::Video:
            return "video";
        case MediaType::Book:
            return "book";
        case MediaType::Rom:
            return "rom";
        case MediaType::Download:
            return "download";
        case MediaType::Other:
        default:
            return "other";
    }
}

MediaType mediaTypeFromString(const std::string& s) {
    const std::string v = lowerCopy(s);
    if (v == "music") return MediaType::Music;
    if (v == "movie") return MediaType::Movie;
    if (v == "tv") return MediaType::TvShow;
    if (v == "photo") return MediaType::Photo;
    if (v == "video") return MediaType::Video;
    if (v == "book") return MediaType::Book;
    if (v == "rom") return MediaType::Rom;
    if (v == "download") return MediaType::Download;
    return MediaType::Other;
}

MediaType mediaTypeFromExtension(const std::string& extension) {
    const std::string ext = lowerExt(extension);
    if (ext == "mp3" || ext == "flac" || ext == "wav" || ext == "aac" || ext == "m4a" ||
        ext == "ogg" || ext == "wma" || ext == "aiff") {
        return MediaType::Music;
    }
    if (ext == "mp4" || ext == "mkv" || ext == "avi" || ext == "mov" || ext == "wmv" ||
        ext == "m4v" || ext == "webm") {
        return MediaType::Video;
    }
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "webp" ||
        ext == "heic" || ext == "bmp" || ext == "tif" || ext == "tiff") {
        return MediaType::Photo;
    }
    if (ext == "pdf" || ext == "epub" || ext == "mobi" || ext == "cbz" || ext == "cbr") {
        return MediaType::Book;
    }
    if (ext == "nes" || ext == "smc" || ext == "gba" || ext == "gb" || ext == "gbc" ||
        ext == "n64" || ext == "z64" || ext == "iso" || ext == "cue" || ext == "chd") {
        return MediaType::Rom;
    }
    return MediaType::Other;
}

MediaType mediaTypeFromRelativePath(const std::string& relativePath) {
    std::string path = relativePath;
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    const std::string lower = lowerCopy(path);
    auto startsWithFolder = [&](const char* folder) {
        const std::string prefix = std::string(folder) + "/";
        return lower.rfind(prefix, 0) == 0 || lower == folder;
    };

    MediaType byFolder = MediaType::Other;
    if (startsWithFolder("music")) {
        byFolder = MediaType::Music;
    } else if (startsWithFolder("movies")) {
        byFolder = MediaType::Movie;
    } else if (startsWithFolder("tv shows") || startsWithFolder("tvshows") ||
               startsWithFolder("television")) {
        byFolder = MediaType::TvShow;
    } else if (startsWithFolder("photos")) {
        byFolder = MediaType::Photo;
    } else if (startsWithFolder("videos")) {
        byFolder = MediaType::Video;
    } else if (startsWithFolder("books")) {
        byFolder = MediaType::Book;
    } else if (startsWithFolder("roms")) {
        byFolder = MediaType::Rom;
    } else if (startsWithFolder("downloads")) {
        // Staging area: everything in it is a download regardless of extension.
        return MediaType::Download;
    }

    if (byFolder != MediaType::Other) {
        // The folder decides the category, but only for files that could
        // plausibly BE that category. Scene releases ship artwork and sidecars
        // next to the media — a YTS rip arrives as the .mp4 plus a
        // "www.YTS.AM.jpg" promo image — and folder-only classification listed
        // that JPG as a movie, so opening it showed a poster instead of a film.
        //
        // An unknown extension still trusts the folder, so container formats
        // missing from the extension table (.mpg, .flv, .ts) keep working
        // rather than silently vanishing from their category.
        const auto dotPos = path.find_last_of('.');
        const std::string ext =
            dotPos == std::string::npos ? std::string{} : path.substr(dotPos + 1);
        if (isSidecarExtension(ext)) {
            return MediaType::Other;
        }
        const MediaType byExt =
            ext.empty() ? MediaType::Other : mediaTypeFromExtension(ext);
        if (byExt == MediaType::Other) {
            return byFolder;
        }
        const bool compatible =
            (byFolder == MediaType::Movie && byExt == MediaType::Video) ||
            (byFolder == MediaType::TvShow && byExt == MediaType::Video) ||
            (byFolder == MediaType::Video && byExt == MediaType::Video) ||
            (byFolder == MediaType::Music && byExt == MediaType::Music) ||
            (byFolder == MediaType::Photo && byExt == MediaType::Photo) ||
            (byFolder == MediaType::Book && byExt == MediaType::Book) ||
            (byFolder == MediaType::Rom && byExt == MediaType::Rom);
        // Artwork, subtitles and extras land in Other: still indexed and
        // visible under Files, but never offered as something to play.
        return compatible ? byFolder : MediaType::Other;
    }

    const auto dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        MediaType byExt = mediaTypeFromExtension(path.substr(dot + 1));
        if (byExt == MediaType::Video) {
            // Loose videos outside Movies/Videos stay classified as video.
            return MediaType::Video;
        }
        return byExt;
    }
    return MediaType::Other;
}

bool isSupportedMediaExtension(const std::string& extension) {
    return mediaTypeFromExtension(extension) != MediaType::Other;
}

}  // namespace cyberdeck
