#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace cyberdeck {

// A single child process with a merged stdout+stderr pipe and a stdin pipe.
//
// Spawned with posix_spawn into its own process group, so requestStop()/kill()
// signal the whole tree — ask_deck.py launches arecord/whisper/ollama/piper as
// grandchildren, and signalling only the python pid would orphan a live
// recorder still holding the microphone.
//
// NOT thread-safe: exactly one thread owns an instance for its lifetime.
// AskDeckScreen owns its instance on the UI thread; AiAssets owns its on the
// download worker. That ownership rule is what lets this class hold no locks.
class Process {
public:
    Process() = default;
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // argv[0] is resolved with execvp() semantics (PATH search).
    // extraEnv entries are "KEY=VALUE", appended to the inherited environment.
    // No cwd support by design — pass absolute paths.
    bool start(const std::vector<std::string>& argv,
               const std::vector<std::string>& extraEnv = {});

    bool started() const { return started_; }
    bool running() const { return started_ && !reaped_; }

    // Requires BOTH pipe EOF and a successful reap. Pipe EOF and process death
    // are independent events observable in either order, and bytes written
    // before death stay buffered after the process is already a zombie —
    // returning true on reap alone drops the final line, which is the payload.
    bool finished() const { return started_ && eof_ && reaped_; }

    int exitCode() const { return exitCode_; }  // >=0 exit status, <0 = -signal
    long pid() const { return pid_; }

    // Drain whatever is readable, append completed lines to out. Never blocks.
    // Returns !finished(), so it reads as "still going".
    bool poll(std::vector<std::string>& out);

    // Worker-thread convenience: poll on a 20ms tick until finished() or the
    // timeout expires. Returns finished(). Never call this on the UI thread.
    bool waitFor(int timeoutMs, std::vector<std::string>& out);

    bool writeLine(const std::string& line);  // to child stdin; false on EAGAIN/EPIPE
    void closeStdin();

    void requestStop();  // SIGTERM to the group, arms a 2s SIGKILL escalation
    void kill();         // SIGKILL to the group now

private:
    void reap(bool blocking);
    void closeFds();

    long pid_ = -1;  // pid_t widened so the header needs no <sys/types.h>
    int outFd_ = -1;
    int inFd_ = -1;
    bool started_ = false;
    bool eof_ = false;
    bool reaped_ = true;
    int exitCode_ = -1;
    std::string pending_;  // partial trailing line carried between polls
    std::chrono::steady_clock::time_point killDeadline_{};
    std::chrono::steady_clock::time_point drainDeadline_{};
    bool termSent_ = false;
    bool drainArmed_ = false;
};

}  // namespace cyberdeck
