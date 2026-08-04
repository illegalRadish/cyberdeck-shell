#include "media/MediaDatabase.hpp"

#include <sqlite3.h>

#include <ctime>
#include <iostream>

namespace cyberdeck {

namespace {

MediaItem readItem(sqlite3_stmt* stmt) {
    MediaItem item;
    item.id = sqlite3_column_int64(stmt, 0);
    item.path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    item.type = mediaTypeFromString(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    item.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    if (const unsigned char* thumb = sqlite3_column_text(stmt, 4)) {
        item.thumbnailPath = reinterpret_cast<const char*>(thumb);
    }
    item.durationMs = sqlite3_column_int64(stmt, 5);
    if (const unsigned char* meta = sqlite3_column_text(stmt, 6)) {
        item.metadata = reinterpret_cast<const char*>(meta);
    }
    item.fileSize = sqlite3_column_int64(stmt, 7);
    item.mtime = sqlite3_column_int64(stmt, 8);
    item.lastScanned = sqlite3_column_int64(stmt, 9);
    return item;
}

}  // namespace

MediaDatabase::~MediaDatabase() {
    close();
}

void MediaDatabase::close() {
    std::lock_guard lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool MediaDatabase::exec(const std::string& sql) const {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "SQLite error: " << (err ? err : "?") << '\n';
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool MediaDatabase::open(const std::string& dbPath) {
    {
        std::lock_guard lock(mutex_);
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3* opened = nullptr;
    if (sqlite3_open(dbPath.c_str(), &opened) != SQLITE_OK) {
        std::cerr << "Failed to open DB: " << dbPath << " — "
                  << (opened ? sqlite3_errmsg(opened) : "?") << '\n';
        if (opened) {
            sqlite3_close(opened);
        }
        return false;
    }

    const char* schema = R"SQL(
        PRAGMA journal_mode=WAL;
        CREATE TABLE IF NOT EXISTS media_items (
            id INTEGER PRIMARY KEY,
            path TEXT NOT NULL UNIQUE,
            type TEXT NOT NULL,
            name TEXT NOT NULL,
            thumbnail_path TEXT,
            duration_ms INTEGER NOT NULL DEFAULT 0,
            metadata TEXT,
            file_size INTEGER NOT NULL DEFAULT 0,
            mtime INTEGER NOT NULL DEFAULT 0,
            last_scanned INTEGER NOT NULL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_media_type ON media_items(type);
        CREATE INDEX IF NOT EXISTS idx_media_name ON media_items(name);
        CREATE TABLE IF NOT EXISTS scan_state (
            root_path TEXT PRIMARY KEY,
            last_scan INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE IF NOT EXISTS playback_progress (
            path TEXT PRIMARY KEY,
            position_sec REAL NOT NULL DEFAULT 0,
            duration_sec REAL NOT NULL DEFAULT 0,
            updated_at INTEGER NOT NULL DEFAULT 0
        );
    )SQL";

    char* err = nullptr;
    if (sqlite3_exec(opened, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "Schema error: " << (err ? err : "?") << '\n';
        sqlite3_free(err);
        sqlite3_close(opened);
        return false;
    }

    std::lock_guard lock(mutex_);
    db_ = opened;
    return true;
}

bool MediaDatabase::upsertItem(const MediaItem& item) {
    std::lock_guard lock(mutex_);
    if (!db_) {
        return false;
    }

    const char* sql =
        "INSERT INTO media_items(path,type,name,thumbnail_path,duration_ms,metadata,"
        "file_size,mtime,last_scanned) VALUES(?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "type=excluded.type, name=excluded.name, thumbnail_path=excluded.thumbnail_path, "
        "duration_ms=excluded.duration_ms, metadata=excluded.metadata, "
        "file_size=excluded.file_size, mtime=excluded.mtime, "
        "last_scanned=excluded.last_scanned;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, item.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, mediaTypeToString(item.type), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, item.name.c_str(), -1, SQLITE_TRANSIENT);
    if (item.thumbnailPath.empty()) {
        sqlite3_bind_null(stmt, 4);
    } else {
        sqlite3_bind_text(stmt, 4, item.thumbnailPath.c_str(), -1, SQLITE_TRANSIENT);
    }
    sqlite3_bind_int64(stmt, 5, item.durationMs);
    sqlite3_bind_text(stmt, 6, item.metadata.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, item.fileSize);
    sqlite3_bind_int64(stmt, 8, item.mtime);
    sqlite3_bind_int64(stmt, 9, item.lastScanned);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool MediaDatabase::removeMissing(const std::vector<std::string>& livePaths) {
    std::lock_guard lock(mutex_);
    if (!db_) {
        return false;
    }

    // Mark-and-sweep using a temp table of live paths.
    if (!exec("CREATE TEMP TABLE IF NOT EXISTS live_paths(path TEXT PRIMARY KEY);") ||
        !exec("DELETE FROM live_paths;")) {
        return false;
    }

    sqlite3_stmt* insert = nullptr;
    if (sqlite3_prepare_v2(db_, "INSERT OR IGNORE INTO live_paths(path) VALUES(?);", -1,
                           &insert, nullptr) != SQLITE_OK) {
        return false;
    }
    for (const auto& path : livePaths) {
        sqlite3_bind_text(insert, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(insert);
        sqlite3_reset(insert);
    }
    sqlite3_finalize(insert);

    return exec(
        "DELETE FROM media_items WHERE path NOT IN (SELECT path FROM live_paths);");
}

std::optional<MediaItem> MediaDatabase::findByPath(const std::string& path) const {
    std::lock_guard lock(mutex_);
    if (!db_) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT id,path,type,name,thumbnail_path,duration_ms,metadata,"
                           "file_size,mtime,last_scanned FROM media_items WHERE path=? LIMIT 1;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<MediaItem> item;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item = readItem(stmt);
    }
    sqlite3_finalize(stmt);
    return item;
}

std::vector<MediaItem> MediaDatabase::listByType(MediaType type, int limit) const {
    std::lock_guard lock(mutex_);
    std::vector<MediaItem> out;
    if (!db_) {
        return out;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT id,path,type,name,thumbnail_path,duration_ms,metadata,"
                           "file_size,mtime,last_scanned FROM media_items WHERE type=? "
                           "ORDER BY name COLLATE NOCASE LIMIT ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_text(stmt, 1, mediaTypeToString(type), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(readItem(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<MediaItem> MediaDatabase::listAll(int limit) const {
    std::lock_guard lock(mutex_);
    std::vector<MediaItem> out;
    if (!db_) {
        return out;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT id,path,type,name,thumbnail_path,duration_ms,metadata,"
                           "file_size,mtime,last_scanned FROM media_items "
                           "ORDER BY name COLLATE NOCASE LIMIT ?;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(readItem(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

int MediaDatabase::countByType(MediaType type) const {
    std::lock_guard lock(mutex_);
    if (!db_) {
        return 0;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM media_items WHERE type=?;", -1, &stmt,
                           nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, mediaTypeToString(type), -1, SQLITE_STATIC);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int MediaDatabase::countAll() const {
    std::lock_guard lock(mutex_);
    if (!db_) {
        return 0;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM media_items;", -1, &stmt, nullptr) !=
        SQLITE_OK) {
        return 0;
    }
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

void MediaDatabase::setScanTimestamp(const std::string& rootPath, std::int64_t unixTime) {
    std::lock_guard lock(mutex_);
    if (!db_) {
        return;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO scan_state(root_path,last_scan) VALUES(?,?) "
                           "ON CONFLICT(root_path) DO UPDATE SET last_scan=excluded.last_scan;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, rootPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, unixTime);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::int64_t MediaDatabase::lastScanTimestamp(const std::string& rootPath) const {
    std::lock_guard lock(mutex_);
    if (!db_) {
        return 0;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT last_scan FROM scan_state WHERE root_path=? LIMIT 1;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, rootPath.c_str(), -1, SQLITE_TRANSIENT);
    std::int64_t value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

void MediaDatabase::saveProgress(const std::string& path, double positionSec,
                                 double durationSec) {
    std::lock_guard lock(mutex_);
    if (!db_ || path.empty()) {
        return;
    }
    // Ignore tiny positions and completed titles.
    if (positionSec < 5.0 || (durationSec > 0.0 && positionSec / durationSec > 0.95)) {
        sqlite3_stmt* del = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM playback_progress WHERE path=?;", -1, &del,
                               nullptr) == SQLITE_OK) {
            sqlite3_bind_text(del, 1, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(del);
            sqlite3_finalize(del);
        }
        return;
    }

    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "INSERT INTO playback_progress(path,position_sec,duration_sec,updated_at) "
                           "VALUES(?,?,?,?) "
                           "ON CONFLICT(path) DO UPDATE SET position_sec=excluded.position_sec, "
                           "duration_sec=excluded.duration_sec, updated_at=excluded.updated_at;",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, positionSec);
    sqlite3_bind_double(stmt, 3, durationSec);
    sqlite3_bind_int64(stmt, 4, now);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

double MediaDatabase::loadProgress(const std::string& path) const {
    std::lock_guard lock(mutex_);
    if (!db_) {
        return 0.0;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
                           "SELECT position_sec FROM playback_progress WHERE path=? LIMIT 1;", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        return 0.0;
    }
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    double pos = 0.0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        pos = sqlite3_column_double(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return pos;
}

std::vector<MediaItem> MediaDatabase::listContinueWatching(int limit) const {
    std::lock_guard lock(mutex_);
    std::vector<MediaItem> out;
    if (!db_) {
        return out;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(
            db_,
            "SELECT m.id,m.path,m.type,m.name,m.thumbnail_path,m.duration_ms,m.metadata,"
            "m.file_size,m.mtime,m.last_scanned "
            "FROM playback_progress p "
            "JOIN media_items m ON m.path = p.path "
            "WHERE m.type IN ('movie','tv','video') "
            "ORDER BY p.updated_at DESC LIMIT ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(readItem(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

}  // namespace cyberdeck
