#pragma once

#include "media/MediaTypes.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace cyberdeck {

struct MediaItem {
    std::int64_t id = 0;
    std::string path;
    MediaType type = MediaType::Other;
    std::string name;
    std::string thumbnailPath;
    std::int64_t durationMs = 0;
    std::string metadata;
    std::int64_t fileSize = 0;
    std::int64_t mtime = 0;
    std::int64_t lastScanned = 0;
};

class MediaDatabase {
public:
    MediaDatabase() = default;
    ~MediaDatabase();

    MediaDatabase(const MediaDatabase&) = delete;
    MediaDatabase& operator=(const MediaDatabase&) = delete;

    bool open(const std::string& dbPath);
    void close();
    bool isOpen() const { return db_ != nullptr; }

    bool upsertItem(const MediaItem& item);
    bool removeMissing(const std::vector<std::string>& livePaths);
    std::optional<MediaItem> findByPath(const std::string& path) const;

    std::vector<MediaItem> listByType(MediaType type, int limit = 500) const;
    std::vector<MediaItem> listAll(int limit = 2000) const;
    int countByType(MediaType type) const;
    int countAll() const;

    void setScanTimestamp(const std::string& rootPath, std::int64_t unixTime);
    std::int64_t lastScanTimestamp(const std::string& rootPath) const;

    void saveProgress(const std::string& path, double positionSec, double durationSec);
    double loadProgress(const std::string& path) const;
    std::vector<MediaItem> listContinueWatching(int limit = 20) const;

private:
    bool exec(const std::string& sql) const;

    mutable std::mutex mutex_;
    sqlite3* db_ = nullptr;
};

}  // namespace cyberdeck
