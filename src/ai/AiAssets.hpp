#pragma once

#include "media/MediaRoot.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cyberdeck {

enum class AiAssetKind {
    HttpFile,     // fetched with curl -C - (resumable)
    OllamaModel,  // pulled through ask_deck.py --pull
    External,     // binary the user installs on the device; presence only
};

struct AiAssetSpec {
    std::string key;
    std::string label;
    AiAssetKind kind = AiAssetKind::HttpFile;
    std::string url;       // http(s) URL, ollama tag, or binary name
    std::string destPath;  // absolute; empty for OllamaModel/External
    std::string note;      // install hint shown for External assets
    std::int64_t expectedBytes = 0;
    bool required = true;  // false => absence degrades the feature, not blocks it
};

struct AiAssetState {
    std::string key;
    std::string label;
    std::string note;
    AiAssetKind kind = AiAssetKind::HttpFile;
    bool present = false;
    bool downloading = false;
    bool required = true;
    std::int64_t haveBytes = 0;  // full size when present, .part size while fetching
    std::int64_t expectedBytes = 0;
};

struct AiAssetsStatus {
    bool ready = false;  // every required asset present
    bool zimPresent = false;
    bool running = false;
    bool finished = false;
    bool verifying = false;
    std::string message;
    std::int64_t totalMissingBytes = 0;
    std::int64_t downloadedBytes = 0;
    int percent = 0;  // whole percent only: this reaches Font-cached strings
    int presentCount = 0;
    int totalCount = 0;
    std::vector<AiAssetState> assets;
};

// Presence checking and on-demand download of the models the voice assistant
// needs. Mirrors MediaScanner: one worker thread, atomic run/stop flags, and a
// mutex-guarded snapshot the UI polls. Owned by Application, never by a screen,
// so a multi-gigabyte download survives navigation.
class AiAssets {
public:
    AiAssets() = default;
    ~AiAssets();

    AiAssets(const AiAssets&) = delete;
    AiAssets& operator=(const AiAssets&) = delete;

    // mediaRoot may be null when PI LIB is absent; the ZIM slot then reports
    // unavailable and the rest of the feature still works.
    void initialize(const MediaRootInfo* mediaRoot);

    // Cheap stat() pass over every asset. Throttled internally because the ZIM
    // lives on a USB drive that may be spun down.
    void refresh();

    AiAssetsStatus status() const;

    void startDownload();
    void startVerify();
    void requestStop();
    bool isRunning() const { return running_.load(); }

    const std::string& aiDir() const { return aiDir_; }
    const std::string& zimDir() const { return zimDir_; }
    const std::string& ollamaModelTag() const { return ollamaTag_; }

    // Absolute path to ask_deck.py, or empty when it isn't on disk.
    std::string helperScriptPath() const;
    // Same, for the dependency-free stage simulator used during development.
    std::string mockScriptPath() const;
    // CYBERDECK_* block handed to Process::start so the helper needs no argv
    // beyond its mode flag.
    std::vector<std::string> helperEnv() const;

private:
    void run(bool verifyOnly);
    bool downloadHttp(const AiAssetSpec& spec, std::size_t index);
    bool pullOllama(const AiAssetSpec& spec, std::size_t index);
    void verifyAll();
    void scanPresence();
    bool preflightSpace(std::string& messageOut) const;
    std::int64_t probeRemoteSize(const std::string& url) const;
    void setMessage(const std::string& message);

    std::vector<AiAssetSpec> specs_;
    std::string aiDir_;
    std::string zimDir_;
    std::string ollamaTag_;
    std::string helperPath_;
    std::string mockPath_;
    bool initialized_ = false;

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    AiAssetsStatus status_;

    // Outcome of the last run. The UI polls refresh() roughly twice a second
    // and scanPresence() rebuilds everything from disk, so without holding
    // these the reason a download stopped is visible for well under a second
    // and the user is left with a bare "Assets missing". Cleared when the next
    // run starts, and per-asset notes are dropped once the asset shows up.
    std::string stickyMessage_;
    std::vector<std::pair<std::string, std::string>> stickyNotes_;
};

}  // namespace cyberdeck
