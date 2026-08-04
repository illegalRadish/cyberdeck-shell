#include "net/TorrentManager.hpp"

#include "core/Assets.hpp"
#include "core/JsonLine.hpp"
#include "media/MediaLibrary.hpp"
#include "net/TorrentFiler.hpp"
#include "platform/Process.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cyberdeck {

namespace fs = std::filesystem;

namespace {

constexpr int kPollIntervalMs = 2000;
constexpr int kHelperTimeoutMs = 30000;
constexpr int kDaemonRetryMs = 8000;
// How long a command's outcome survives the poll tick that would otherwise
// overwrite it.
constexpr int kStickyMessageMs = 9000;

bool exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::exists(path, ec);
}

std::string homeDir() {
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
    return ".";
}

std::string whichBinary(const std::string& name) {
    // Checked before PATH, because a GUI- or IDE-launched app on macOS inherits
    // a minimal PATH with no /opt/homebrew/bin. AiAssets hit exactly this and
    // resolves its binaries the same way. Without this the daemon is reported
    // "not installed" and ensureDaemon() never even tries to start it.
    for (const char* dir : {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin",
                            "/opt/homebrew/sbin", "/usr/sbin"}) {
        const fs::path candidate = fs::path(dir) / name;
        std::error_code ec;
        if (fs::exists(candidate, ec) && !fs::is_directory(candidate, ec)) {
            return candidate.string();
        }
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

std::string resolveAsset(const std::string& relative) {
    const std::string resolved = assets::resolve(relative);
    return exists(resolved) ? resolved : std::string{};
}

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::int64_t toInt64(const std::optional<std::string>& value) {
    if (!value || value->empty()) {
        return 0;
    }
    return std::strtoll(value->c_str(), nullptr, 10);
}

}  // namespace

TorrentManager::~TorrentManager() {
    stop_ = true;
    wake_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TorrentManager::initialize(const MediaRootInfo* mediaRoot, MediaLibrary* library) {
    library_ = library;
    if (mediaRoot) {
        root_ = *mediaRoot;
    }

    ctlPath_ = resolveAsset("net/torrentctl.py");
    searchPath_ = resolveAsset("net/tpb_search.py");
    installPath_ = resolveAsset("net/install-torrent-engine.sh");
    configDir_ = (fs::path(homeDir()) / ".config/transmission-daemon").string();

    if (root_.found) {
        downloadsDir_ = (fs::path(root_.path) / "Downloads").string();
        routesPath_ = (fs::path(root_.cacheDir) / "torrent_routes.tsv").string();
        std::error_code ec;
        fs::create_directories(fs::path(downloadsDir_) / "complete", ec);
        fs::create_directories(fs::path(downloadsDir_) / "incomplete", ec);
        fs::create_directories(fs::path(downloadsDir_) / ".watch", ec);
        loadRoutes();
    }

    {
        std::lock_guard lock(mutex_);
        status_.helpersFound = !ctlPath_.empty() && !searchPath_.empty();
        status_.engineInstalled = !whichBinary("transmission-daemon").empty();
        status_.message = root_.found ? "Starting…" : "PI LIB not mounted";
    }

    initialized_ = true;
    if (!root_.found) {
        return;  // nowhere to download to; leave the worker unstarted
    }

    running_ = true;
    worker_ = std::thread([this]() { run(); });
}

TorrentStatus TorrentManager::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::string TorrentManager::searchScriptPath() const {
    return searchPath_;
}

std::vector<std::string> TorrentManager::helperEnv() const {
    return {"PYTHONUNBUFFERED=1"};
}

void TorrentManager::setMessage(const std::string& message) {
    std::lock_guard lock(mutex_);
    status_.message = message;
    // Outlive the next poll, which would otherwise replace a real explanation
    // with a summary line and leave the user with no idea what happened.
    stickyUntilMs_ = nowMs() + kStickyMessageMs;
}

void TorrentManager::queue(Command command) {
    if (!initialized_ || !running_.load()) {
        return;  // no worker to drain the queue (usually: PI LIB not mounted)
    }
    {
        std::lock_guard lock(mutex_);
        commands_.push_back(std::move(command));
    }
    wake_.notify_all();
}

void TorrentManager::addMagnet(const std::string& magnet, MediaType route) {
    if (magnet.empty()) {
        return;
    }
    queue({CommandKind::Add, magnet, route});
}

void TorrentManager::remove(const std::string& id) {
    queue({CommandKind::Remove, id, MediaType::Download});
}

void TorrentManager::pause(const std::string& id) {
    queue({CommandKind::Pause, id, MediaType::Download});
}

void TorrentManager::resume(const std::string& id) {
    queue({CommandKind::Resume, id, MediaType::Download});
}

void TorrentManager::startInstall() {
    if (installing_.load()) {
        return;
    }
    queue({CommandKind::Install, {}, MediaType::Download});
}

// ---------------------------------------------------------------- route file

void TorrentManager::loadRoutes() {
    std::ifstream in(routesPath_);
    if (!in) {
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string hash;
        std::string type;
        std::string filed;
        if (!std::getline(fields, hash, '\t') || !std::getline(fields, type, '\t')) {
            continue;
        }
        std::getline(fields, filed, '\t');
        if (hash.empty()) {
            continue;
        }
        Route route;
        route.route = mediaTypeFromString(type);
        route.filed = filed == "1";
        route.explicitChoice = true;
        routes_[hash] = route;
    }
}

void TorrentManager::saveRoutes() const {
    if (routesPath_.empty()) {
        return;
    }
    const std::string temp = routesPath_ + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            return;
        }
        for (const auto& [hash, route] : routes_) {
            out << hash << '\t' << mediaTypeToString(route.route) << '\t'
                << (route.filed ? '1' : '0') << '\n';
        }
    }
    std::error_code ec;
    fs::rename(temp, routesPath_, ec);  // same directory, so atomic
}

// -------------------------------------------------------------------- worker

bool TorrentManager::runHelper(const std::vector<std::string>& argv,
                               std::vector<std::string>& linesOut) {
    Process helper;
    if (!helper.start(argv, helperEnv())) {
        setMessage("could not start python3");
        return false;
    }
    helper.waitFor(kHelperTimeoutMs, linesOut);
    if (!helper.finished()) {
        helper.kill();
        helper.waitFor(2000, linesOut);
        setMessage("torrent helper timed out");
        return false;
    }
    return helper.exitCode() == 0;
}

bool TorrentManager::ensureDaemon() {
    const std::string binary = whichBinary("transmission-daemon");
    {
        std::lock_guard lock(mutex_);
        status_.engineInstalled = !binary.empty();
    }
    if (binary.empty() || installing_.load()) {
        return false;
    }

    const std::int64_t now = nowMs();
    if (now - lastDaemonAttemptMs_ < kDaemonRetryMs) {
        return false;
    }
    lastDaemonAttemptMs_ = now;

    // --foreground, backgrounded by the shell, rather than letting Transmission
    // daemonise itself. This is not a style choice; its own daemonisation is
    // fatal on macOS:
    //
    //     SIGABRT, OBJC, "*** multi-threaded process forked ***"
    //     tr_ctor::set_metainfo_from_magnet_link -> parseMagnet -> set_name
    //       -> tr_strv_convert_utf8 -> objc_msgSend
    //       -> performForkChildInitialize -> _objc_fatal
    //
    // transmission-daemon forks without exec'ing, and converting a magnet's
    // display name to UTF-8 calls into Objective-C. Touching the ObjC runtime
    // in a forked-but-not-exec'd child aborts the process. The daemon therefore
    // died on the first torrent-add every time, while list calls — which never
    // parse a magnet — kept working, so it looked like a network fault.
    // --foreground never forks, so the crash cannot happen, and one code path
    // serves both macOS and the Pi.
    //
    // The rest of the shape matters too:
    //   * output redirected to /dev/null, or the daemon inherits our stdout
    //     pipe and holds it open forever, so Process::finished() (which needs
    //     pipe EOF) never becomes true;
    //   * nohup, so closing the terminal that launched the shell does not
    //     SIGHUP the daemon;
    //   * no kill() anywhere below — the daemon shares the process group we
    //     created, and signalling that group would take it down with us.
    const std::string command = "nohup '" + binary + "' --foreground --config-dir '" +
                                configDir_ + "' >/dev/null 2>&1 &";
    Process daemon;
    if (!daemon.start({"sh", "-c", command})) {
        return false;
    }
    std::vector<std::string> lines;
    daemon.waitFor(8000, lines);
    // Deliberately no kill() on timeout: the next poll reports whether RPC came
    // up, and that is a safe answer either way.
    return true;
}

void TorrentManager::handleAdd(const Command& command) {
    std::vector<std::string> argv = {"python3", "-u", ctlPath_, "--add", command.arg};
    if (!downloadsDir_.empty()) {
        argv.push_back("--dest");
        argv.push_back((fs::path(downloadsDir_) / "complete").string());
    }

    std::vector<std::string> lines;
    const bool ok = runHelper(argv, lines);

    for (const std::string& line : lines) {
        if (!jsonline::isObject(line)) {
            continue;
        }
        const auto kind = jsonline::field(line, "kind");
        if (!kind) {
            continue;
        }
        if (*kind == "error") {
            const auto message = jsonline::field(line, "message");
            setMessage(message ? *message : "could not add torrent");
            return;
        }
        if (*kind == "added") {
            const auto hash = jsonline::field(line, "hash");
            const auto name = jsonline::field(line, "name");
            const auto duplicate = jsonline::field(line, "duplicate");
            if (hash && !hash->empty()) {
                std::lock_guard lock(mutex_);
                Route& route = routes_[*hash];
                // An explicit route from the search screen or the add dialog
                // outranks anything the filer would infer from the name.
                if (command.route != MediaType::Download) {
                    route.route = command.route;
                    route.explicitChoice = true;
                }
                saveRoutes();
            }
            setMessage(duplicate && *duplicate == "1"
                           ? "Already downloading: " + (name ? *name : std::string{})
                           : "Added " + (name ? *name : std::string{}));
            return;
        }
    }

    if (!ok) {
        setMessage("could not add torrent");
    }
}

void TorrentManager::handleInstall() {
    if (installPath_.empty()) {
        setMessage("install script missing from assets/net/");
        return;
    }
    installing_ = true;
    {
        std::lock_guard lock(mutex_);
        status_.installing = true;
        status_.message = "Installing torrent engine…";
    }

    std::vector<std::string> argv = {"bash", installPath_};
    if (root_.found) {
        argv.push_back("--media-root");
        argv.push_back(root_.path);
    }

    Process install;
    std::string lastLine;
    if (!install.start(argv, helperEnv())) {
        lastLine = "could not run the install script";
    } else {
        std::vector<std::string> lines;
        std::size_t consumed = 0;
        // apt on a Pi is slow, so this gets its own generous budget and streams
        // progress rather than sitting silent for minutes.
        const std::int64_t deadline = nowMs() + 600000;
        while (!install.finished() && nowMs() < deadline) {
            if (stop_.load()) {
                install.requestStop();
                install.waitFor(3000, lines);
                break;
            }
            install.waitFor(500, lines);
            for (; consumed < lines.size(); ++consumed) {
                if (!lines[consumed].empty()) {
                    lastLine = lines[consumed];
                    setMessage(lastLine);
                }
            }
        }
        if (!install.finished()) {
            install.kill();
            install.waitFor(2000, lines);
            lastLine = "install timed out";
        }
        for (; consumed < lines.size(); ++consumed) {
            if (!lines[consumed].empty()) {
                lastLine = lines[consumed];
            }
        }
        if (install.exitCode() != 0 && lastLine.empty()) {
            lastLine = "install failed (exit " + std::to_string(install.exitCode()) + ")";
        }
    }

    installing_ = false;
    lastDaemonAttemptMs_ = 0;  // let the next poll start the freshly installed daemon
    std::lock_guard lock(mutex_);
    status_.installing = false;
    status_.engineInstalled = !whichBinary("transmission-daemon").empty();
    // The script's own last line is the outcome, success or failure.
    status_.message = lastLine.empty() ? "Install finished" : lastLine;
}

void TorrentManager::drainCommands() {
    while (!stop_.load()) {
        Command command;
        {
            std::lock_guard lock(mutex_);
            if (commands_.empty()) {
                return;
            }
            command = std::move(commands_.front());
            commands_.pop_front();
        }

        switch (command.kind) {
            case CommandKind::Install:
                handleInstall();
                break;
            case CommandKind::Add:
                handleAdd(command);
                break;
            case CommandKind::Remove:
            case CommandKind::Pause:
            case CommandKind::Resume: {
                const char* flag = command.kind == CommandKind::Remove  ? "--remove"
                                   : command.kind == CommandKind::Pause ? "--pause"
                                                                        : "--resume";
                std::vector<std::string> lines;
                runHelper({"python3", "-u", ctlPath_, flag, command.arg}, lines);
                break;
            }
        }
    }
}

void TorrentManager::pollOnce() {
    if (ctlPath_.empty()) {
        setMessage("torrentctl.py missing from assets/net/");
        return;
    }

    std::vector<std::string> lines;
    runHelper({"python3", "-u", ctlPath_, "--list"}, lines);

    std::vector<TorrentState> torrents;
    bool daemonRunning = true;
    std::string error;

    for (const std::string& line : lines) {
        if (!jsonline::isObject(line)) {
            continue;
        }
        const auto kind = jsonline::field(line, "kind");
        if (!kind) {
            continue;
        }
        if (*kind == "error") {
            daemonRunning = false;
            const auto message = jsonline::field(line, "message");
            error = message ? *message : "torrent engine unreachable";
            continue;
        }
        if (*kind != "torrent") {
            continue;
        }

        TorrentState state;
        state.id = jsonline::field(line, "id").value_or("");
        state.name = jsonline::field(line, "name").value_or("");
        state.hash = jsonline::field(line, "hash").value_or("");
        state.status = jsonline::field(line, "status").value_or("");
        state.error = jsonline::field(line, "error").value_or("");
        state.downloadDir = jsonline::field(line, "downloadDir").value_or("");
        state.percent = std::clamp(jsonline::toInt(jsonline::field(line, "percent")), 0, 100);
        state.sizeBytes = toInt64(jsonline::field(line, "sizeBytes"));
        state.haveBytes = toInt64(jsonline::field(line, "haveBytes"));
        state.rateBytes = toInt64(jsonline::field(line, "rate"));

        {
            std::lock_guard lock(mutex_);
            const auto it = routes_.find(state.hash);
            if (it != routes_.end()) {
                state.route = it->second.route;
                state.filed = it->second.filed;
            }
        }
        if (state.route == MediaType::Download) {
            state.route = torrentfiler::routeFromName(state.name);
        }

        // A torrent reporting an error may still be at 100% with a broken or
        // missing payload, so filing waits until it is clean.
        if (state.percent >= 100 && !state.filed && state.error.empty()) {
            fileCompleted(state);
            std::lock_guard lock(mutex_);
            const auto it = routes_.find(state.hash);
            state.filed = it != routes_.end() && it->second.filed;
        }

        torrents.push_back(std::move(state));
    }

    std::lock_guard lock(mutex_);
    status_.daemonRunning = daemonRunning;
    status_.torrents = std::move(torrents);
    // A daemon that has gone away is urgent enough to displace a sticky
    // message; a routine summary is not.
    const bool sticky = nowMs() < stickyUntilMs_;
    if (!status_.installing && (!sticky || !daemonRunning)) {
        if (!daemonRunning) {
            status_.message = error;
        } else if (status_.torrents.empty()) {
            status_.message = "No active torrents";
        } else {
            // Surface the first torrent-level error: a torrent sitting at 0%
            // because its tracker is unreachable is otherwise indistinguishable
            // from one that is simply slow.
            std::string problem;
            for (const TorrentState& t : status_.torrents) {
                if (!t.error.empty()) {
                    problem = t.error;
                    break;
                }
            }
            status_.message = problem.empty()
                                  ? std::to_string(status_.torrents.size()) + " torrent(s)"
                                  : problem;
        }
    }
}

void TorrentManager::fileCompleted(const TorrentState& torrent) {
    if (torrent.downloadDir.empty() || !root_.found) {
        return;
    }
    const std::string payload = (fs::path(torrent.downloadDir) / torrent.name).string();

    MediaType route = torrent.route;
    bool explicitChoice = false;
    {
        std::lock_guard lock(mutex_);
        const auto it = routes_.find(torrent.hash);
        if (it != routes_.end() && it->second.explicitChoice) {
            route = it->second.route;
            explicitChoice = true;
        }
    }
    if (!explicitChoice) {
        route = torrentfiler::routeFromPayload(payload,
                                               torrentfiler::routeFromName(torrent.name));
    }

    const std::string dest = torrentfiler::destinationDir(root_.path, route, torrent.name);
    auto markFiled = [&]() {
        std::lock_guard lock(mutex_);
        Route& entry = routes_[torrent.hash];
        entry.route = route;
        entry.filed = true;
        saveRoutes();
    };

    if (dest.empty()) {
        markFiled();  // nothing to route: it stays in Downloads, and that is final
        return;
    }

    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) {
        setMessage("could not create " + dest);
        return;
    }

    // Transmission does the move itself so it keeps seeding from the new path.
    // Relocating the files behind its back leaves it reporting "No data found"
    // and silently stops the seed.
    std::vector<std::string> lines;
    const bool moved =
        runHelper({"python3", "-u", ctlPath_, "--set-location", torrent.id, "--dest", dest},
                  lines);

    if (!moved) {
        // Fall back to moving it ourselves — the daemon may have died between
        // the poll and now, and the payload is complete either way.
        fs::rename(payload, fs::path(dest) / torrent.name, ec);
        if (ec) {
            // Across filesystems rename fails with EXDEV; copy and unlink.
            ec.clear();
            fs::copy(payload, fs::path(dest) / torrent.name,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            if (ec) {
                setMessage("could not file " + torrent.name);
                return;
            }
            fs::remove_all(payload, ec);
        }
    }

    markFiled();
    setMessage("Filed " + torrent.name + " to " +
               std::string(torrentfiler::folderForRoute(route)));

    // Index the new media so it shows up on the existing browse screens
    // without a restart.
    if (library_ && library_->ready() && !library_->scanner().isRunning()) {
        library_->startScan();
    }
}

void TorrentManager::run() {
    while (!stop_.load()) {
        drainCommands();
        if (stop_.load()) {
            break;
        }

        const bool engineInstalled = !whichBinary("transmission-daemon").empty();
        if (engineInstalled && !installing_.load()) {
            pollOnce();
            bool needsDaemon = false;
            {
                std::lock_guard lock(mutex_);
                needsDaemon = !status_.daemonRunning;
            }
            if (needsDaemon && !stop_.load()) {
                // A daemon we are about to start is a transient state, not a
                // fault the user has to act on, so say that rather than leaving
                // a raw "[Errno 61] Connection refused" on screen.
                {
                    std::lock_guard lock(mutex_);
                    status_.message = "Starting torrent engine…";
                }
                if (ensureDaemon()) {
                    // Re-poll in this same iteration. The RPC socket takes a
                    // moment to bind after the daemon forks, and waiting for the
                    // next tick left a connection error on screen for seconds
                    // after the engine was already healthy — which reads as a
                    // hard failure rather than a startup delay.
                    for (int attempt = 0; attempt < 6 && !stop_.load(); ++attempt) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        pollOnce();
                        std::lock_guard lock(mutex_);
                        if (status_.daemonRunning) {
                            break;
                        }
                    }
                }
            }
        } else if (!engineInstalled) {
            std::lock_guard lock(mutex_);
            status_.engineInstalled = false;
            status_.daemonRunning = false;
            if (!status_.installing) {
                status_.message = "Torrent engine not installed";
            }
        }

        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(kPollIntervalMs),
                       [this]() { return stop_.load() || !commands_.empty(); });
    }
    running_ = false;
}

}  // namespace cyberdeck
