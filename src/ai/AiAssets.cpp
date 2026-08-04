#include "ai/AiAssets.hpp"

#include "core/Assets.hpp"
#include "core/JsonLine.hpp"
#include "platform/Process.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace cyberdeck {

namespace fs = std::filesystem;

namespace {

// Smallest viable models for a 4GB Pi 4. Both are one-line swaps: bump the
// whisper model to ggml-base.en.bin or the ollama tag to qwen2.5:1.5b once
// measurements on the device justify the extra memory and latency.
constexpr const char* kWhisperModelFile = "ggml-tiny.en.bin";
constexpr const char* kWhisperModelUrl =
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin";
constexpr std::int64_t kWhisperModelBytes = 77704715;

constexpr const char* kPiperVoiceFile = "en_US-amy-low.onnx";
constexpr const char* kPiperVoiceUrl =
    "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/low/"
    "en_US-amy-low.onnx";
constexpr std::int64_t kPiperVoiceBytes = 63104526;

constexpr const char* kPiperConfigUrl =
    "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/low/"
    "en_US-amy-low.onnx.json";
constexpr std::int64_t kPiperConfigBytes = 4164;

// Kiwix publishes dated builds and the undated alias 404s, so the month is
// pinned here. When it eventually disappears, list
// https://download.kiwix.org/zim/wikipedia/ and bump this; the failure note
// shows the URL so it is obvious what went stale.
constexpr const char* kZimFile = "wikipedia_en_simple_all_nopic_2026-05.zim";
constexpr const char* kZimUrl =
    "https://download.kiwix.org/zim/wikipedia/wikipedia_en_simple_all_nopic_2026-05.zim";
constexpr std::int64_t kZimBytes = 982788870;

constexpr const char* kOllamaTag = "qwen2.5:0.5b";
constexpr std::int64_t kOllamaBytes = 397800000;

constexpr std::int64_t kSpaceHeadroomBytes = 512ll * 1024 * 1024;
constexpr std::uint32_t kZimMagic = 0x44D8FA5A;

std::string homeDir() {
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
    return ".";
}

std::int64_t fileSizeOr0(const std::string& path) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    return ec ? 0 : static_cast<std::int64_t>(size);
}

bool exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::exists(path, ec);
}

// Presence of an Ollama model is a filesystem question so that refresh() never
// has to spawn a process on the UI thread. Tag "qwen2.5:0.5b" maps to
// ~/.ollama/models/manifests/registry.ollama.ai/library/qwen2.5/0.5b
std::string ollamaManifestPath(const std::string& tag) {
    const std::size_t colon = tag.find(':');
    const std::string name = tag.substr(0, colon);
    const std::string version = colon == std::string::npos ? "latest" : tag.substr(colon + 1);
    fs::path base(homeDir());
    base /= ".ollama/models/manifests/registry.ollama.ai/library";
    base /= name;
    base /= version;
    return base.string();
}

std::string whichBinary(const std::string& name) {
    if (name.find('/') != std::string::npos) {
        return exists(name) ? name : std::string{};
    }
    if (const char* pathEnv = std::getenv("PATH")) {
        std::string paths(pathEnv);
        std::size_t start = 0;
        while (start <= paths.size()) {
            const std::size_t sep = paths.find(':', start);
            const std::string dir =
                paths.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
            if (!dir.empty()) {
                const fs::path candidate = fs::path(dir) / name;
                std::error_code ec;
                if (fs::exists(candidate, ec) && !fs::is_directory(candidate, ec)) {
                    return candidate.string();
                }
            }
            if (sep == std::string::npos) {
                break;
            }
            start = sep + 1;
        }
    }
    return {};
}

std::string humanBytes(std::int64_t bytes) {
    char buf[48];
    const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    if (mb >= 1024.0) {
        std::snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    } else if (mb >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.0f MB", mb);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    }
    return buf;
}

int wholePercent(std::int64_t have, std::int64_t total) {
    if (total <= 0) {
        return 0;
    }
    const long long pct = have * 100 / total;
    return static_cast<int>(std::clamp<long long>(pct, 0, 100));
}

}  // namespace

AiAssets::~AiAssets() {
    requestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AiAssets::initialize(const MediaRootInfo* mediaRoot) {
    // Small models on the SD card so voice survives the drive being unplugged;
    // the ~1GB ZIM on the media drive so the SD card stays free.
    aiDir_ = (fs::path(homeDir()) / ".local/share/cyberdeck/ai").string();
    ollamaTag_ = kOllamaTag;

    std::error_code ec;
    fs::create_directories(fs::path(aiDir_) / "models", ec);
    fs::create_directories(fs::path(aiDir_) / "voices", ec);
    fs::create_directories(fs::path(aiDir_) / "bin", ec);
    fs::create_directories(fs::path(aiDir_) / "logs", ec);

    if (mediaRoot && mediaRoot->found) {
        zimDir_ = (fs::path(mediaRoot->cacheDir) / "ai/zim").string();
        fs::create_directories(zimDir_, ec);
    } else {
        zimDir_.clear();
    }

    // assets::resolve returns a path even when nothing is there, so existence
    // has to be checked here or posix_spawn fails with a bare ENOENT later.
    if (const char* override = std::getenv("CYBERDECK_AI_HELPER"); override && exists(override)) {
        helperPath_ = override;
    } else {
        const std::string resolved = assets::resolve("ai/ask_deck.py");
        helperPath_ = exists(resolved) ? resolved : std::string{};
    }
    const std::string mock = assets::resolve("ai/mock_deck.py");
    mockPath_ = exists(mock) ? mock : std::string{};

    const fs::path models = fs::path(aiDir_) / "models";
    const fs::path voices = fs::path(aiDir_) / "voices";

    specs_.clear();
    specs_.push_back({"whisper-model", "WHISPER TINY.EN", AiAssetKind::HttpFile,
                      kWhisperModelUrl, (models / kWhisperModelFile).string(), "",
                      kWhisperModelBytes, true});
    specs_.push_back({"piper-voice", "PIPER VOICE AMY-LOW", AiAssetKind::HttpFile,
                      kPiperVoiceUrl, (voices / kPiperVoiceFile).string(), "",
                      kPiperVoiceBytes, true});
    specs_.push_back({"piper-voice-cfg", "PIPER VOICE CONFIG", AiAssetKind::HttpFile,
                      kPiperConfigUrl,
                      (voices / (std::string(kPiperVoiceFile) + ".json")).string(), "",
                      kPiperConfigBytes, true});
    specs_.push_back({"ollama-model", std::string("LLM ") + kOllamaTag,
                      AiAssetKind::OllamaModel, kOllamaTag, "", "", kOllamaBytes, true});
    specs_.push_back({"zim", "OFFLINE WIKIPEDIA (SIMPLE)", AiAssetKind::HttpFile, kZimUrl,
                      zimDir_.empty() ? std::string{} : (fs::path(zimDir_) / kZimFile).string(),
                      zimDir_.empty() ? "PI LIB not mounted" : "", kZimBytes, false});

    specs_.push_back({"whisper-bin", "WHISPER.CPP BINARY", AiAssetKind::External,
                      "whisper-cli", "", "build whisper.cpp on the device", 0, true});
    specs_.push_back({"piper-bin", "PIPER BINARY", AiAssetKind::External, "piper", "",
                      "pip install piper-tts", 0, true});
    // Install hints have to fit the note column, so the long one points at the
    // README rather than being shown truncated mid-URL.
    specs_.push_back({"ollama-bin", "OLLAMA BINARY", AiAssetKind::External, "ollama", "",
                      "run the ollama install script (README)", 0, true});
    specs_.push_back({"helper", "PIPELINE HELPER", AiAssetKind::External, "ask_deck.py", "",
                      "shipped in assets/ai/", 0, true});

    initialized_ = true;
    scanPresence();
}

void AiAssets::refresh() {
    if (!initialized_ || running_.load()) {
        return;  // never race the worker's own bookkeeping
    }
    scanPresence();
}

void AiAssets::scanPresence() {
    std::vector<AiAssetState> states;
    states.reserve(specs_.size());

    int presentCount = 0;
    std::int64_t missingBytes = 0;
    bool ready = true;
    bool zimPresent = false;

    for (const AiAssetSpec& spec : specs_) {
        AiAssetState state;
        state.key = spec.key;
        state.label = spec.label;
        state.kind = spec.kind;
        state.required = spec.required;
        state.expectedBytes = spec.expectedBytes;
        state.note = spec.note;

        switch (spec.kind) {
            case AiAssetKind::HttpFile:
                if (spec.destPath.empty()) {
                    state.present = false;
                    state.note = spec.note.empty() ? "unavailable" : spec.note;
                } else {
                    state.present = exists(spec.destPath);
                    state.haveBytes = state.present ? fileSizeOr0(spec.destPath)
                                                    : fileSizeOr0(spec.destPath + ".part");
                    if (!state.present && state.haveBytes > 0) {
                        state.note = "partial — will resume";
                    }
                }
                break;
            case AiAssetKind::OllamaModel:
                state.present = exists(ollamaManifestPath(spec.url));
                state.haveBytes = state.present ? spec.expectedBytes : 0;
                break;
            case AiAssetKind::External: {
                if (spec.key == "helper") {
                    state.present = !helperPath_.empty();
                    if (state.present) {
                        state.note = helperPath_;
                    }
                } else {
                    const std::string found = whichBinary(spec.url);
                    // A device-local build under aiDir/bin wins over PATH.
                    const std::string local = (fs::path(aiDir_) / "bin" / spec.url).string();
                    if (exists(local)) {
                        state.present = true;
                        state.note = local;
                    } else if (!found.empty()) {
                        state.present = true;
                        state.note = found;
                    } else {
                        state.present = false;
                        state.note = spec.note;
                    }
                }
                break;
            }
        }

        if (state.present) {
            ++presentCount;
        } else {
            if (spec.required) {
                ready = false;
            }
            if (spec.kind != AiAssetKind::External && !spec.destPath.empty()) {
                missingBytes += std::max<std::int64_t>(0, spec.expectedBytes - state.haveBytes);
            } else if (spec.kind == AiAssetKind::OllamaModel) {
                missingBytes += spec.expectedBytes;
            }
        }
        if (spec.key == "zim") {
            zimPresent = state.present;
        }
        states.push_back(std::move(state));
    }

    std::lock_guard lock(mutex_);
    status_.assets = std::move(states);
    status_.ready = ready;
    status_.zimPresent = zimPresent;
    status_.presentCount = presentCount;
    status_.totalCount = static_cast<int>(specs_.size());
    status_.totalMissingBytes = missingBytes;

    // Re-apply why the last run stopped. A rescan only knows an asset is absent,
    // not that it is blocked on a missing binary or a rotted URL.
    for (const auto& [key, note] : stickyNotes_) {
        for (AiAssetState& state : status_.assets) {
            if (state.key == key && !state.present && !note.empty()) {
                state.note = note;
                break;
            }
        }
    }

    if (!status_.running) {
        status_.downloadedBytes = 0;
        status_.percent = 0;
        if (!stickyMessage_.empty() && !ready) {
            status_.message = stickyMessage_;
        } else {
            status_.message = ready ? "Ready" : "Assets missing";
        }
    }
}

AiAssetsStatus AiAssets::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::string AiAssets::helperScriptPath() const {
    return helperPath_;
}

std::string AiAssets::mockScriptPath() const {
    return mockPath_;
}

std::vector<std::string> AiAssets::helperEnv() const {
    const fs::path models = fs::path(aiDir_) / "models";
    const fs::path voices = fs::path(aiDir_) / "voices";
    const fs::path bin = fs::path(aiDir_) / "bin";

    // Temp audio goes to tmpfs on Linux so an interaction writes nothing to the
    // SD card; macOS has no /dev/shm, so fall back to the usual temp dir.
    std::string tmp;
#if defined(__linux__)
    tmp = "/dev/shm/cyberdeck";
#else
    if (const char* t = std::getenv("TMPDIR")) {
        tmp = t;
    } else {
        tmp = "/tmp";
    }
#endif

    std::string zimPath;
    if (!zimDir_.empty()) {
        const std::string candidate = (fs::path(zimDir_) / kZimFile).string();
        if (exists(candidate)) {
            zimPath = candidate;
        }
    }

    // Resolve binaries here rather than leaving it to the helper's PATH lookup:
    // a GUI-launched app on macOS inherits a minimal PATH with no /opt/homebrew,
    // so `which` inside the helper would come up empty.
    const std::string whisperBin = exists((bin / "whisper-cli").string())
                                       ? (bin / "whisper-cli").string()
                                       : whichBinary("whisper-cli");
    const std::string piperBin =
        exists((bin / "piper").string()) ? (bin / "piper").string() : whichBinary("piper");
    const std::string ollamaBin =
        exists((bin / "ollama").string()) ? (bin / "ollama").string() : whichBinary("ollama");

    return {
        "CYBERDECK_AI_DIR=" + aiDir_,
        "CYBERDECK_AI_TMP=" + tmp,
        "CYBERDECK_ZIM=" + zimPath,
        "CYBERDECK_WHISPER_BIN=" + whisperBin,
        "CYBERDECK_WHISPER_MODEL=" + (models / kWhisperModelFile).string(),
        "CYBERDECK_PIPER_BIN=" + piperBin,
        "CYBERDECK_PIPER_VOICE=" + (voices / kPiperVoiceFile).string(),
        "CYBERDECK_OLLAMA_BIN=" + ollamaBin,
        "CYBERDECK_OLLAMA_MODEL=" + ollamaTag_,
        "CYBERDECK_LOG=" + (fs::path(aiDir_) / "logs/last_run.log").string(),
        "PYTHONUNBUFFERED=1",
    };
}

void AiAssets::requestStop() {
    stop_ = true;
}

void AiAssets::startDownload() {
    if (!initialized_ || running_.load()) {
        return;
    }
    stop_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    {
        std::lock_guard lock(mutex_);
        stickyMessage_.clear();
        stickyNotes_.clear();
        status_.running = true;
        status_.finished = false;
        status_.verifying = false;
        status_.downloadedBytes = 0;
        status_.percent = 0;
        status_.message = "Starting…";
    }
    running_ = true;
    worker_ = std::thread([this]() { run(false); });
}

void AiAssets::startVerify() {
    if (!initialized_ || running_.load()) {
        return;
    }
    stop_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    {
        std::lock_guard lock(mutex_);
        stickyMessage_.clear();
        stickyNotes_.clear();
        status_.running = true;
        status_.finished = false;
        status_.verifying = true;
        status_.message = "Verifying…";
    }
    running_ = true;
    worker_ = std::thread([this]() { run(true); });
}

void AiAssets::setMessage(const std::string& message) {
    std::lock_guard lock(mutex_);
    status_.message = message;
}

bool AiAssets::preflightSpace(std::string& messageOut) const {
    // The SD card and the media drive are separate filesystems, so their needs
    // are summed separately.
    std::int64_t needSd = 0;
    std::int64_t needZim = 0;
    for (const AiAssetSpec& spec : specs_) {
        if (spec.kind == AiAssetKind::External || spec.destPath.empty()) {
            if (spec.kind == AiAssetKind::OllamaModel && !exists(ollamaManifestPath(spec.url))) {
                needSd += spec.expectedBytes;
            }
            continue;
        }
        if (exists(spec.destPath)) {
            continue;
        }
        const std::int64_t remaining =
            std::max<std::int64_t>(0, spec.expectedBytes - fileSizeOr0(spec.destPath + ".part"));
        if (spec.key == "zim") {
            needZim += remaining;
        } else {
            needSd += remaining;
        }
    }

    std::error_code ec;
    if (needSd > 0) {
        const auto space = fs::space(aiDir_, ec);
        if (!ec && static_cast<std::int64_t>(space.available) < needSd + kSpaceHeadroomBytes) {
            messageOut = "Need " + humanBytes(needSd) + " on the SD card, only " +
                         humanBytes(static_cast<std::int64_t>(space.available)) + " free";
            return false;
        }
    }
    if (needZim > 0 && !zimDir_.empty()) {
        const auto space = fs::space(zimDir_, ec);
        if (!ec && static_cast<std::int64_t>(space.available) < needZim + kSpaceHeadroomBytes) {
            messageOut = "Need " + humanBytes(needZim) + " on PI LIB, only " +
                         humanBytes(static_cast<std::int64_t>(space.available)) + " free";
            return false;
        }
    }
    return true;
}

std::int64_t AiAssets::probeRemoteSize(const std::string& url) const {
    // Read the Content-Length header out of curl's own output rather than using
    // a --write-out variable: %{content_length_download} does not exist and
    // %header{...} needs curl >= 7.84, which is not a safe assumption on a Pi.
    // Returns 0 when unknown, and 0 must never be treated as an error.
    Process probe;
    if (!probe.start({"curl", "-sIL", "--connect-timeout", "15", url})) {
        return 0;
    }
    std::vector<std::string> lines;
    probe.waitFor(45000, lines);
    if (!probe.finished()) {
        probe.kill();
        probe.waitFor(2000, lines);
    }

    std::int64_t size = 0;
    for (const std::string& line : lines) {
        // Header names are case-insensitive, and redirects mean several
        // responses arrive; the last one wins.
        std::string lower;
        lower.reserve(line.size());
        for (char c : line) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (lower.rfind("content-length:", 0) != 0) {
            continue;
        }
        const long long value = std::strtoll(line.c_str() + 15, nullptr, 10);
        if (value > 0) {
            size = static_cast<std::int64_t>(value);
        }
    }
    return size;
}

bool AiAssets::downloadHttp(const AiAssetSpec& spec, std::size_t index) {
    if (spec.destPath.empty()) {
        return false;
    }
    const std::string part = spec.destPath + ".part";
    std::error_code ec;
    fs::create_directories(fs::path(spec.destPath).parent_path(), ec);

    // Two separate numbers, and conflating them is a trap:
    //   `expected`  - display only. May come from a compile-time constant that
    //                 has drifted, so it must never decide success or failure.
    //   `authoritative` - Content-Length the server reported for THIS transfer.
    //                 Zero when unknown. Only this may reject a file.
    const std::int64_t authoritative = probeRemoteSize(spec.url);
    const std::int64_t expected = authoritative > 0 ? authoritative : spec.expectedBytes;
    {
        std::lock_guard lock(mutex_);
        if (index < status_.assets.size()) {
            status_.assets[index].expectedBytes = expected;
            status_.assets[index].downloading = true;
        }
    }

    // A .part that already matches the server's length is a finished download
    // whose rename never happened. Claim it instead of re-fetching — and
    // without this, `curl -C -` would ask for a range past EOF, get a 416, and
    // fail forever.
    if (authoritative > 0 && fileSizeOr0(part) == authoritative) {
        fs::rename(part, spec.destPath, ec);
        if (!ec) {
            std::lock_guard lock(mutex_);
            if (index < status_.assets.size()) {
                status_.assets[index].present = true;
                status_.assets[index].downloading = false;
                status_.assets[index].haveBytes = authoritative;
                status_.assets[index].note.clear();
            }
            status_.downloadedBytes += authoritative;
            return true;
        }
    }

    const std::int64_t baseDownloaded = [this]() {
        std::lock_guard lock(mutex_);
        return status_.downloadedBytes;
    }();

    auto runCurl = [&](bool resume) -> int {
        std::vector<std::string> argv = {"curl", "-L", "--fail", "--retry", "3",
                                        "--retry-delay", "2", "--connect-timeout", "15",
                                        // abort a transfer stalled under 1KB/s for a
                                        // minute rather than hanging on flaky wifi
                                        "--speed-time", "60", "--speed-limit", "1024"};
        if (resume) {
            argv.push_back("-C");
            argv.push_back("-");
        }
        argv.push_back("-o");
        argv.push_back(part);
        argv.push_back(spec.url);

        Process curl;
        if (!curl.start(argv)) {
            setMessage("curl not available");
            return -1;
        }
        std::vector<std::string> lines;
        // curl's own progress meter is \r-delimited and unparseable, so progress
        // comes from the growing .part file instead.
        while (!curl.finished()) {
            if (stop_.load()) {
                curl.requestStop();
                curl.waitFor(3000, lines);
                break;
            }
            curl.waitFor(400, lines);
            const std::int64_t have = fileSizeOr0(part);
            std::lock_guard lock(mutex_);
            if (index < status_.assets.size()) {
                status_.assets[index].haveBytes = have;
            }
            status_.downloadedBytes = baseDownloaded + have;
            const int pct = wholePercent(status_.downloadedBytes, status_.totalMissingBytes);
            if (pct != status_.percent) {
                status_.percent = pct;
                status_.message = "Downloading " + spec.label + "  " + std::to_string(pct) + "%";
            }
        }
        return curl.exitCode();
    };

    int code = runCurl(true);
    // 33 = server refuses byte ranges. 22 = an HTTP error under --fail, which
    // for a resume is usually 416 (requested range unsatisfiable) from a .part
    // that is already complete or longer than the object. Either way the resume
    // itself is the problem, so discard it and fetch once from the start.
    if ((code == 33 || code == 22) && fileSizeOr0(part) > 0) {
        fs::remove(part, ec);
        code = runCurl(false);
    }

    {
        std::lock_guard lock(mutex_);
        if (index < status_.assets.size()) {
            status_.assets[index].downloading = false;
        }
    }

    if (stop_.load()) {
        return false;  // .part is left behind so the next run resumes
    }
    if (code != 0) {
        std::lock_guard lock(mutex_);
        if (index < status_.assets.size()) {
            // 22 means the server said no (404 is the usual one). Show the URL
            // so a rotted link is obvious rather than looking like a net fault.
            status_.assets[index].note = code == 22
                                             ? "server rejected the request — check " + spec.url
                                             : "download failed (curl " + std::to_string(code) + ")";
        }
        status_.message = spec.label + " failed";
        return false;
    }

    // curl exited 0 with --fail, so the HTTP status was good and the body was
    // written in full (a truncated transfer is exit 18). That is the success
    // signal. Only a length the server itself gave us may override it — never
    // spec.expectedBytes, which is a hardcoded estimate that goes stale when
    // upstream reissues a file.
    const std::int64_t got = fileSizeOr0(part);
    if (authoritative > 0 && got != authoritative) {
        std::lock_guard lock(mutex_);
        if (index < status_.assets.size()) {
            status_.assets[index].note = "incomplete (" + humanBytes(got) + " of " +
                                         humanBytes(authoritative) + ") — will resume";
        }
        return false;
    }
    if (got == 0) {
        std::lock_guard lock(mutex_);
        if (index < status_.assets.size()) {
            status_.assets[index].note = "server returned an empty file";
        }
        return false;
    }

    fs::rename(part, spec.destPath, ec);  // same directory, so atomic
    if (ec) {
        setMessage("could not finalise " + spec.label);
        return false;
    }
    std::lock_guard lock(mutex_);
    status_.downloadedBytes = baseDownloaded + got;
    if (index < status_.assets.size()) {
        status_.assets[index].present = true;
        status_.assets[index].haveBytes = got;
        status_.assets[index].note.clear();
    }
    return true;
}

bool AiAssets::pullOllama(const AiAssetSpec& spec, std::size_t index) {
    // `ollama pull` repaints its progress with ANSI escapes, but the helper
    // streams /api/pull as JSON lines through the same protocol everything else
    // uses, so no extra parsing is needed here.
    const std::string script = helperPath_.empty() ? mockPath_ : helperPath_;
    if (script.empty()) {
        setMessage("pipeline helper missing");
        return false;
    }
    Process pull;
    if (!pull.start({"python3", "-u", script, "--pull", spec.url}, helperEnv())) {
        setMessage("could not start python3");
        return false;
    }

    const std::int64_t baseDownloaded = [this]() {
        std::lock_guard lock(mutex_);
        return status_.downloadedBytes;
    }();

    std::vector<std::string> lines;
    std::size_t consumed = 0;
    while (!pull.finished()) {
        if (stop_.load()) {
            pull.requestStop();
            pull.waitFor(3000, lines);
            break;
        }
        pull.waitFor(300, lines);
        for (; consumed < lines.size(); ++consumed) {
            const std::string& line = lines[consumed];
            if (!jsonline::isObject(line)) {
                continue;
            }
            const auto stage = jsonline::field(line, "stage");
            if (!stage) {
                continue;
            }
            if (*stage == "pull") {
                const int assetPct = jsonline::toInt(jsonline::field(line, "pct"));
                std::lock_guard lock(mutex_);
                const std::int64_t have = spec.expectedBytes * assetPct / 100;
                if (index < status_.assets.size()) {
                    status_.assets[index].haveBytes = have;
                    status_.assets[index].downloading = true;
                }
                status_.downloadedBytes = baseDownloaded + have;
                const int pct = wholePercent(status_.downloadedBytes, status_.totalMissingBytes);
                if (pct != status_.percent) {
                    status_.percent = pct;
                    status_.message = "Pulling " + spec.label + "  " + std::to_string(pct) + "%";
                }
            } else if (*stage == "error") {
                const auto text = jsonline::field(line, "text");
                std::lock_guard lock(mutex_);
                if (index < status_.assets.size()) {
                    status_.assets[index].note = text ? *text : "pull failed";
                }
            }
        }
    }

    std::lock_guard lock(mutex_);
    if (index < status_.assets.size()) {
        status_.assets[index].downloading = false;
    }
    const bool ok = pull.exitCode() == 0 && !stop_.load();
    if (ok && index < status_.assets.size()) {
        status_.assets[index].present = true;
        status_.assets[index].haveBytes = spec.expectedBytes;
    }
    return ok;
}

void AiAssets::verifyAll() {
    for (std::size_t i = 0; i < specs_.size() && !stop_.load(); ++i) {
        const AiAssetSpec& spec = specs_[i];
        std::string note;
        bool present = false;

        switch (spec.kind) {
            case AiAssetKind::HttpFile: {
                if (spec.destPath.empty()) {
                    note = spec.note.empty() ? "unavailable" : spec.note;
                    break;
                }
                if (!exists(spec.destPath)) {
                    const std::int64_t partial = fileSizeOr0(spec.destPath + ".part");
                    note = partial > 0 ? "partial — will resume" : "missing";
                    break;
                }
                const std::int64_t size = fileSizeOr0(spec.destPath);
                if (size == 0) {
                    note = "empty file — delete and re-download";
                    break;
                }
                // spec.expectedBytes is an estimate that goes stale when upstream
                // reissues a file, so an exact mismatch is reported, not failed.
                // Only something far too small is treated as broken.
                if (spec.expectedBytes > 0 && size < spec.expectedBytes / 2) {
                    note = "truncated (" + humanBytes(size) + " of ~" +
                           humanBytes(spec.expectedBytes) + ") — re-download";
                    break;
                }
                if (spec.key == "zim") {
                    // Cheap structural check: a 4-byte magic read beats hashing
                    // a gigabyte off an SD card.
                    std::ifstream in(spec.destPath, std::ios::binary);
                    unsigned char magic[4] = {0, 0, 0, 0};
                    in.read(reinterpret_cast<char*>(magic), 4);
                    const std::uint32_t value =
                        static_cast<std::uint32_t>(magic[0]) |
                        (static_cast<std::uint32_t>(magic[1]) << 8) |
                        (static_cast<std::uint32_t>(magic[2]) << 16) |
                        (static_cast<std::uint32_t>(magic[3]) << 24);
                    if (value != kZimMagic) {
                        note = "not a valid ZIM file";
                        break;
                    }
                }
                present = true;
                note = humanBytes(size);
                break;
            }
            case AiAssetKind::OllamaModel:
                present = exists(ollamaManifestPath(spec.url));
                note = present ? "installed" : "missing";
                break;
            case AiAssetKind::External: {
                if (spec.key == "helper") {
                    present = !helperPath_.empty();
                    note = present ? helperPath_ : "ask_deck.py not found in assets/ai/";
                    break;
                }
                const std::string local = (fs::path(aiDir_) / "bin" / spec.url).string();
                const std::string binary = exists(local) ? local : whichBinary(spec.url);
                if (binary.empty()) {
                    note = spec.note;
                    break;
                }
                Process probe;
                if (probe.start({binary, "--version"})) {
                    std::vector<std::string> lines;
                    probe.waitFor(5000, lines);
                    if (!probe.finished()) {
                        probe.kill();
                        probe.waitFor(1000, lines);
                    }
                    present = true;
                    note = lines.empty() ? binary : lines.front();
                    if (note.size() > 60) {
                        note.resize(60);
                    }
                } else {
                    note = binary + " (not executable)";
                }
                break;
            }
        }

        std::lock_guard lock(mutex_);
        if (i < status_.assets.size()) {
            status_.assets[i].present = present;
            status_.assets[i].note = note;
        }
        status_.message = "Verifying " + spec.label;
    }
}

void AiAssets::run(bool verifyOnly) {
    if (verifyOnly) {
        verifyAll();
        {
            std::lock_guard lock(mutex_);
            status_.running = false;
            status_.finished = true;
            status_.verifying = false;
            status_.message = stop_.load() ? "Verify cancelled" : "Verify complete";
        }
        running_ = false;
        return;
    }

    std::string spaceMessage;
    if (!preflightSpace(spaceMessage)) {
        std::lock_guard lock(mutex_);
        status_.running = false;
        status_.finished = true;
        status_.message = spaceMessage;
        running_ = false;
        return;
    }

    int failures = 0;
    int downloaded = 0;
    int blocked = 0;
    std::string firstProblem;
    // Why each asset failed, keyed by asset key. The final scanPresence()
    // rebuilds status_.assets from disk, which would otherwise discard the
    // reasons and leave every failed row saying only "MISSING".
    std::vector<std::pair<std::string, std::string>> problemNotes;

    for (std::size_t i = 0; i < specs_.size() && !stop_.load(); ++i) {
        const AiAssetSpec& spec = specs_[i];
        if (spec.kind == AiAssetKind::External) {
            continue;  // user installs these on the device
        }
        const bool alreadyPresent = spec.kind == AiAssetKind::OllamaModel
                                        ? exists(ollamaManifestPath(spec.url))
                                        : exists(spec.destPath);
        if (alreadyPresent) {
            continue;  // idempotent: re-running downloads nothing
        }
        if (spec.destPath.empty() && spec.kind == AiAssetKind::HttpFile) {
            continue;  // no ZIM destination without PI LIB
        }
        // Pulling a model needs the ollama binary, which the user installs on
        // the device. Say that instead of reporting a bare download failure.
        if (spec.kind == AiAssetKind::OllamaModel && whichBinary("ollama").empty() &&
            !exists((fs::path(aiDir_) / "bin/ollama").string())) {
            ++blocked;
            problemNotes.emplace_back(spec.key, "install the ollama binary first");
            if (firstProblem.empty()) {
                firstProblem = spec.label + " needs the ollama binary";
            }
            continue;
        }

        const bool ok = spec.kind == AiAssetKind::OllamaModel ? pullOllama(spec, i)
                                                             : downloadHttp(spec, i);
        if (ok) {
            ++downloaded;
        } else if (!stop_.load()) {
            ++failures;
            std::string note;
            {
                std::lock_guard lock(mutex_);
                if (i < status_.assets.size()) {
                    note = status_.assets[i].note;
                }
            }
            problemNotes.emplace_back(spec.key, note);
            if (firstProblem.empty()) {
                firstProblem = spec.label + (note.empty() ? " failed" : ": " + note);
            }
        }
    }

    std::string outcome;
    if (stop_.load()) {
        outcome = "Download cancelled — progress kept";
    } else if (failures > 0 || blocked > 0) {
        // Name the first problem: a bare count sends the user hunting.
        outcome = firstProblem;
        const int remaining = failures + blocked - 1;
        if (remaining > 0) {
            outcome += "  (+" + std::to_string(remaining) + " more)";
        }
    } else if (downloaded > 0) {
        outcome = "Downloaded " + std::to_string(downloaded) + " asset(s)";
    } else {
        outcome = "Everything already present";
    }

    {
        std::lock_guard lock(mutex_);
        stickyNotes_ = std::move(problemNotes);
        stickyMessage_ = outcome;
    }

    scanPresence();  // re-applies the sticky notes and message

    std::lock_guard lock(mutex_);
    status_.running = false;
    status_.finished = true;
    status_.message = outcome;
    running_ = false;
}

}  // namespace cyberdeck
