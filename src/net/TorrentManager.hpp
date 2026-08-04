#pragma once

#include "media/MediaRoot.hpp"
#include "media/MediaTypes.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cyberdeck {

class MediaLibrary;

struct TorrentState {
    std::string id;
    std::string name;
    std::string hash;
    std::string status;  // downloading / seeding / stopped / checking / queued
    std::string error;
    std::string downloadDir;  // where the payload currently lives
    int percent = 0;  // whole percent only: this reaches Font-cached strings
    std::int64_t sizeBytes = 0;
    std::int64_t haveBytes = 0;
    std::int64_t rateBytes = 0;
    MediaType route = MediaType::Download;
    bool filed = false;  // payload already moved into the library
};

struct TorrentStatus {
    bool engineInstalled = false;  // transmission-daemon on disk
    bool daemonRunning = false;    // and answering RPC
    bool installing = false;
    bool helpersFound = false;  // assets/net/ scripts resolved
    std::string message;
    std::vector<TorrentState> torrents;
};

// Owns the torrent engine: polls Transmission over RPC, and moves finished
// payloads into the media library.
//
// Mirrors AiAssets' threading contract — one worker thread, atomic run/stop
// flags, and a mutex-guarded snapshot the UI polls. Owned by Application, never
// by a screen, so a multi-gigabyte download survives navigation.
//
// The difference from AiAssets is that the worker is long-lived rather than
// per-operation: it polls on a tick and drains a command queue the UI pushes
// to. Every Process instance is created and destroyed inside that one thread,
// which is what satisfies Process's single-owner rule.
class TorrentManager {
public:
    TorrentManager() = default;
    ~TorrentManager();

    TorrentManager(const TorrentManager&) = delete;
    TorrentManager& operator=(const TorrentManager&) = delete;

    // mediaRoot may be null when PI LIB is absent; the feature then reports
    // unavailable rather than downloading into an arbitrary directory.
    // library may be null; filing then skips the rescan.
    void initialize(const MediaRootInfo* mediaRoot, MediaLibrary* library);

    TorrentStatus status() const;

    // All of these are non-blocking: they queue work for the poll thread.
    void addMagnet(const std::string& magnet, MediaType route);
    void remove(const std::string& id);
    void pause(const std::string& id);
    void resume(const std::string& id);
    void startInstall();

    bool isInstalling() const { return installing_.load(); }

    // Absolute path to tpb_search.py, or empty when it isn't on disk. The
    // search screen drives it directly: a search is short, cancellable, and
    // tied to a screen that is on-screen for its whole duration, so it does not
    // belong on the shared worker.
    std::string searchScriptPath() const;
    std::vector<std::string> helperEnv() const;

    const std::string& downloadsDir() const { return downloadsDir_; }

private:
    enum class CommandKind { Add, Remove, Pause, Resume, Install };

    struct Command {
        CommandKind kind = CommandKind::Add;
        std::string arg;  // magnet URI or torrent id
        MediaType route = MediaType::Download;
    };

    void run();
    void drainCommands();
    void pollOnce();
    void handleAdd(const Command& command);
    void handleInstall();
    void fileCompleted(const TorrentState& torrent);
    bool runHelper(const std::vector<std::string>& argv, std::vector<std::string>& linesOut);
    bool ensureDaemon();  // true when a start was actually attempted
    void setMessage(const std::string& message);
    void queue(Command command);

    void loadRoutes();
    void saveRoutes() const;

    MediaRootInfo root_{};
    MediaLibrary* library_ = nullptr;
    std::string ctlPath_;
    std::string searchPath_;
    std::string installPath_;
    std::string downloadsDir_;
    std::string configDir_;
    std::string routesPath_;
    bool initialized_ = false;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> installing_{false};

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    TorrentStatus status_;
    std::deque<Command> commands_;

    // hash -> {route the user chose, whether the payload has been filed}.
    // Persisted beside the library database so a route survives a restart
    // mid-download. Deliberately a sidecar file rather than a MediaDatabase
    // schema change: the data is transient and per-torrent, and none of it
    // outlives the download it describes.
    struct Route {
        MediaType route = MediaType::Download;
        bool filed = false;
        bool explicitChoice = false;
    };
    std::map<std::string, Route> routes_;

    // Guards against respawning the daemon in a tight loop when it refuses to
    // come up: at most one attempt every few seconds.
    std::int64_t lastDaemonAttemptMs_ = 0;

    // Why the last command succeeded or failed, and how long that explanation
    // outlives it. Without this the poll tick two seconds later overwrites
    // status_.message with a bland "No active torrents", so an add that was
    // rejected looks exactly like an add that worked. Mirrors the sticky
    // message AiAssets keeps for the same reason.
    std::int64_t stickyUntilMs_ = 0;
};

}  // namespace cyberdeck
