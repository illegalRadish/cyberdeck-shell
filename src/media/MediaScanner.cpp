#include "media/MediaScanner.hpp"

#include "media/MediaTypes.hpp"

#include <sys/stat.h>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace cyberdeck {

namespace fs = std::filesystem;

MediaScanner::MediaScanner(MediaDatabase& db, ThumbnailCache& thumbs)
    : db_(db), thumbs_(thumbs) {}

MediaScanner::~MediaScanner() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void MediaScanner::start(const MediaRootInfo& root) {
    if (!root.found || running_.load()) {
        return;
    }
    stop_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    {
        std::lock_guard lock(progressMutex_);
        progress_ = ScanProgress{};
        progress_.running = true;
        progress_.message = "Scanning PI LIB…";
    }
    running_ = true;
    worker_ = std::thread([this, root]() { run(root); });
}

void MediaScanner::requestStop() {
    stop_ = true;
}

ScanProgress MediaScanner::progress() const {
    std::lock_guard lock(progressMutex_);
    return progress_;
}

std::string MediaScanner::displayNameFromPath(const std::string& path) {
    return fs::path(path).stem().string();
}

void MediaScanner::run(MediaRootInfo root) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    std::vector<std::string> livePaths;
    int seen = 0;
    int updated = 0;
    int skipped = 0;

    std::error_code ec;
    const fs::path rootPath(root.path);

    for (fs::recursive_directory_iterator it(
             rootPath, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end && !stop_.load(); it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }

        const fs::path& path = it->path();
        // Skip cache / hidden cyberdeck metadata.
        bool skip = false;
        for (const auto& part : path) {
            const std::string name = part.string();
            if (name == ".cyberdeck" || name == ".DS_Store" || name == "@eaDir") {
                skip = true;
                break;
            }
        }
        if (skip) {
            if (it->is_directory()) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!it->is_regular_file()) {
            continue;
        }

        const std::string ext = path.has_extension() ? path.extension().string() : "";
        if (!isSupportedMediaExtension(ext)) {
            continue;
        }

        ++seen;
        const std::string absPath = fs::weakly_canonical(path, ec).string();
        if (ec) {
            ec.clear();
            continue;
        }
        livePaths.push_back(absPath);

        {
            std::lock_guard lock(progressMutex_);
            progress_.filesSeen = seen;
            progress_.currentPath = absPath;
        }

        std::int64_t mtime = 0;
        std::int64_t size = 0;
        struct stat st {};
        if (stat(absPath.c_str(), &st) == 0) {
            mtime = static_cast<std::int64_t>(st.st_mtime);
            size = static_cast<std::int64_t>(st.st_size);
        }
        const fs::path rel = fs::relative(path, rootPath, ec);
        const std::string relative = ec ? path.filename().string() : rel.string();
        // Sole authority on classification. It already folds in the extension
        // for loose files and the Movies-folder case, and it deliberately
        // returns Other for artwork and sidecars sitting beside real media —
        // so re-deriving a type from the extension here would overrule that and
        // put a release's promo JPG back in the library as a photo.
        const MediaType type = mediaTypeFromRelativePath(relative);

        if (auto existing = db_.findByPath(absPath)) {
            // Type is part of the skip test, not just mtime and size. A file
            // whose bytes never change still needs re-recording when the
            // classifier's verdict changes, otherwise a fix to classification
            // only ever applies to newly added files and every existing row
            // keeps its stale category forever.
            if (existing->mtime == mtime && existing->fileSize == size &&
                existing->type == type) {
                ++skipped;
                std::lock_guard lock(progressMutex_);
                progress_.filesSkipped = skipped;
                continue;
            }
        }

        MediaItem item;
        item.path = absPath;
        item.type = type;
        item.name = displayNameFromPath(absPath);
        item.fileSize = size;
        item.mtime = mtime;
        item.lastScanned = now;
        item.thumbnailPath = thumbs_.ensureThumbnail(absPath, type);
        item.metadata = relative;

        if (db_.upsertItem(item)) {
            ++updated;
        }

        std::lock_guard lock(progressMutex_);
        progress_.filesUpdated = updated;
        progress_.filesSkipped = skipped;
    }

    if (!stop_.load()) {
        db_.removeMissing(livePaths);
        db_.setScanTimestamp(root.path, now);
    }

    {
        std::lock_guard lock(progressMutex_);
        progress_.running = false;
        progress_.finished = true;
        progress_.filesSeen = seen;
        progress_.filesUpdated = updated;
        progress_.filesSkipped = skipped;
        progress_.message = stop_.load() ? "Scan cancelled" : "Scan complete";
        progress_.currentPath.clear();
    }

    running_ = false;
    std::cout << "Media scan finished. seen=" << seen << " updated=" << updated
              << " skipped=" << skipped << '\n';
}

}  // namespace cyberdeck
