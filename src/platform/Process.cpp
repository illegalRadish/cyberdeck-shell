#include "platform/Process.hpp"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

extern char** environ;

namespace cyberdeck {

namespace {

constexpr std::size_t kReadBudget = 64 * 1024;   // bytes drained per poll()
constexpr std::size_t kMaxPendingLine = 64 * 1024;

// Serialises pipe creation + spawn across threads.
//
// Between pipe() and fcntl(FD_CLOEXEC) the new fds are inheritable. If the
// AiAssets worker spawns curl inside that window it inherits AskDeckScreen's
// pipe write end, that end never closes, and the screen's EOF never arrives —
// the run appears to hang forever. Holding this across the whole sequence
// removes the race entirely.
std::mutex& spawnMutex() {
    static std::mutex m;
    return m;
}

// writeLine() to a child that has already exited raises SIGPIPE, whose default
// disposition kills the shell. Nothing here depends on SIGPIPE delivery.
void ignoreSigPipeOnce() {
    static const bool once = []() {
        std::signal(SIGPIPE, SIG_IGN);
        return true;
    }();
    (void)once;
}

bool setCloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD);
    return flags >= 0 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void closeFd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

}  // namespace

Process::~Process() {
    if (running()) {
        // SIGKILL rather than SIGTERM: a destructor cannot wait out a graceful
        // shutdown. This is the only teardown path when a screen is popped,
        // because ScreenManager stops calling update() during the pop
        // animation, so no poll() runs to escalate an earlier SIGTERM.
        ::kill(static_cast<pid_t>(-pid_), SIGKILL);
        reap(true);
    }
    closeFds();
}

bool Process::start(const std::vector<std::string>& argv,
                    const std::vector<std::string>& extraEnv) {
    if (argv.empty()) {
        return false;
    }
    // Reusable across runs: a screen holds one Process for its whole lifetime
    // and asks question after question. Refuse only while a child is genuinely
    // still alive; otherwise clear the previous run's state and spawn again.
    if (running()) {
        return false;
    }
    if (started_) {
        reap(false);
        closeFds();
        pending_.clear();
        started_ = false;
        eof_ = false;
        reaped_ = true;
        exitCode_ = -1;
        termSent_ = false;
        drainArmed_ = false;
        pid_ = -1;
    }
    ignoreSigPipeOnce();

    std::vector<char*> cArgv;
    cArgv.reserve(argv.size() + 1);
    for (const std::string& arg : argv) {
        cArgv.push_back(const_cast<char*>(arg.c_str()));
    }
    cArgv.push_back(nullptr);

    // Inherited environment plus our additions. Strings must outlive the spawn.
    std::vector<std::string> envStorage;
    for (char** e = environ; e && *e; ++e) {
        envStorage.emplace_back(*e);
    }
    for (const std::string& kv : extraEnv) {
        envStorage.push_back(kv);
    }
    std::vector<char*> cEnv;
    cEnv.reserve(envStorage.size() + 1);
    for (std::string& kv : envStorage) {
        cEnv.push_back(const_cast<char*>(kv.c_str()));
    }
    cEnv.push_back(nullptr);

    int outPipe[2] = {-1, -1};
    int inPipe[2] = {-1, -1};
    pid_t spawned = -1;
    int spawnErr = 0;

    {
        std::lock_guard lock(spawnMutex());

        if (::pipe(outPipe) != 0) {
            std::cerr << "Process::start: pipe failed: " << std::strerror(errno) << '\n';
            return false;
        }
        if (::pipe(inPipe) != 0) {
            std::cerr << "Process::start: pipe failed: " << std::strerror(errno) << '\n';
            ::close(outPipe[0]);
            ::close(outPipe[1]);
            return false;
        }
        setCloexec(outPipe[0]);
        setCloexec(outPipe[1]);
        setCloexec(inPipe[0]);
        setCloexec(inPipe[1]);
        setNonBlocking(outPipe[0]);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, inPipe[0], STDIN_FILENO);
        // stderr is merged into stdout so python tracebacks reach the UI
        // instead of vanishing; non-JSON lines are kept as diagnostics.
        posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, outPipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, inPipe[1]);
        posix_spawn_file_actions_addclose(&actions, outPipe[0]);

        // Own process group, so signals reach the whole descendant tree.
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attr, 0);

        spawnErr = ::posix_spawnp(&spawned, cArgv[0], &actions, &attr, cArgv.data(),
                                  cEnv.data());

        posix_spawnattr_destroy(&attr);
        posix_spawn_file_actions_destroy(&actions);

        // The parent must drop the child's ends, or read() never sees EOF.
        ::close(outPipe[1]);
        outPipe[1] = -1;
        ::close(inPipe[0]);
        inPipe[0] = -1;
    }

    if (spawnErr != 0) {
        std::cerr << "Process::start: posix_spawnp(" << argv[0]
                  << ") failed: " << std::strerror(spawnErr) << '\n';
        ::close(outPipe[0]);
        ::close(inPipe[1]);
        return false;
    }

    pid_ = static_cast<long>(spawned);
    outFd_ = outPipe[0];
    inFd_ = inPipe[1];
    started_ = true;
    eof_ = false;
    reaped_ = false;
    termSent_ = false;
    drainArmed_ = false;
    exitCode_ = -1;
    pending_.clear();
    return true;
}

bool Process::poll(std::vector<std::string>& out) {
    if (!started_) {
        return false;
    }

    if (outFd_ >= 0) {
        char buf[4096];
        std::size_t budget = kReadBudget;
        while (budget > 0) {
            const ssize_t n = ::read(outFd_, buf, sizeof(buf));
            if (n > 0) {
                pending_.append(buf, static_cast<std::size_t>(n));
                budget -= static_cast<std::size_t>(n);
                continue;
            }
            if (n == 0) {
                eof_ = true;
                closeFd(outFd_);
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            eof_ = true;  // EIO etc: the child is gone
            closeFd(outFd_);
            break;
        }

        std::size_t start = 0;
        std::size_t nl = pending_.find('\n', start);
        while (nl != std::string::npos) {
            std::string line = pending_.substr(start, nl - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            out.push_back(std::move(line));
            start = nl + 1;
            nl = pending_.find('\n', start);
        }
        pending_.erase(0, start);

        if (pending_.size() > kMaxPendingLine) {
            out.push_back(pending_);  // runaway line with no newline; flush it
            pending_.clear();
        }
    }

    if (termSent_ && !reaped_ && std::chrono::steady_clock::now() > killDeadline_) {
        ::kill(static_cast<pid_t>(-pid_), SIGKILL);
        killDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    }

    reap(false);

    // Reaped but still no EOF means a grandchild inherited the write end and
    // outlived its parent. Give the pipe a moment to drain, then give up on it
    // rather than reporting "running" forever.
    if (reaped_ && !eof_) {
        const auto now = std::chrono::steady_clock::now();
        if (!drainArmed_) {
            drainArmed_ = true;
            drainDeadline_ = now + std::chrono::seconds(1);
        } else if (now > drainDeadline_) {
            eof_ = true;
            closeFd(outFd_);
        }
    }

    return !finished();
}

bool Process::waitFor(int timeoutMs, std::vector<std::string>& out) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        poll(out);
        if (finished() || std::chrono::steady_clock::now() >= deadline) {
            return finished();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool Process::writeLine(const std::string& line) {
    if (inFd_ < 0) {
        return false;
    }
    const std::string payload = line + "\n";
    std::size_t written = 0;
    while (written < payload.size()) {
        const ssize_t n =
            ::write(inFd_, payload.data() + written, payload.size() - written);
        if (n > 0) {
            written += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;  // EAGAIN or EPIPE; caller decides whether it matters
    }
    return true;
}

void Process::closeStdin() {
    closeFd(inFd_);
}

void Process::requestStop() {
    if (!running()) {
        return;
    }
    ::kill(static_cast<pid_t>(-pid_), SIGTERM);
    termSent_ = true;
    killDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

void Process::kill() {
    if (!running()) {
        return;
    }
    ::kill(static_cast<pid_t>(-pid_), SIGKILL);
    termSent_ = true;
    killDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
}

void Process::reap(bool blocking) {
    if (reaped_ || pid_ < 0) {
        return;
    }
    int status = 0;
    const pid_t result =
        ::waitpid(static_cast<pid_t>(pid_), &status, blocking ? 0 : WNOHANG);
    if (result == static_cast<pid_t>(pid_)) {
        reaped_ = true;
        if (WIFEXITED(status)) {
            exitCode_ = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exitCode_ = -WTERMSIG(status);
        } else {
            exitCode_ = -1;
        }
        return;
    }
    if (result < 0 && errno == ECHILD) {
        reaped_ = true;  // already reaped elsewhere; exit status is unknowable
        exitCode_ = -1;
    }
}

void Process::closeFds() {
    closeFd(outFd_);
    closeFd(inFd_);
}

}  // namespace cyberdeck
