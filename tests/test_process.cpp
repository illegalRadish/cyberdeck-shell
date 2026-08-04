// Scratch harness: exercises Process + JsonLine without SDL/GL.
#include "core/JsonLine.hpp"
#include "platform/Process.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace cyberdeck;

static int failures = 0;

static void check(bool cond, const std::string& what) {
    std::printf("%s  %s\n", cond ? "  ok  " : " FAIL ", what.c_str());
    if (!cond) ++failures;
}

static void testJsonLine() {
    std::puts("\n-- JsonLine --");
    check(jsonline::isObject("  {\"a\":\"b\"}"), "isObject leading space");
    check(!jsonline::isObject("traceback (most recent call last)"), "isObject rejects prose");

    const std::string line = R"({"stage":"answer","text":"A: b \"quoted\" and\nnewline"})";
    auto stage = jsonline::field(line, "stage");
    auto text = jsonline::field(line, "text");
    check(stage && *stage == "answer", "extract stage");
    check(text && *text == "A: b \"quoted\" and\nnewline", "unescape quotes + newline");

    // The trap a naive find() falls into: the key name appearing inside a value.
    const std::string trap = R"({"stage":"answer","text":"the word \"stage\":\"thinking\" appears here"})";
    auto trapStage = jsonline::field(trap, "stage");
    check(trapStage && *trapStage == "answer", "key text inside a value does not confuse lookup");

    check(!jsonline::field(line, "missing").has_value(), "absent key -> nullopt");
    check(jsonline::toInt(jsonline::field(R"({"pct":"37"})", "pct")) == 37, "toInt");
    check(jsonline::toInt(jsonline::field(R"({"pct":"x"})", "pct"), -1) == -1, "toInt fallback");

    auto uni = jsonline::field(R"({"t":"café"})", "t");
    check(uni && *uni == "caf\xc3\xa9", "\\u escape -> utf8");

    // Nested values must be skipped, not parsed, without losing later keys.
    auto after = jsonline::field(R"({"a":{"b":"c"},"d":"e"})", "d");
    check(after && *after == "e", "skips nested object");
    auto arr = jsonline::field(R"({"a":[1,2,{"x":"}"}],"d":"e"})", "d");
    check(arr && *arr == "e", "skips array containing a brace in a string");
}

static void testEcho() {
    std::puts("\n-- spawn + drain --");
    Process p;
    check(p.start({"/bin/sh", "-c", "printf 'one\\ntwo\\nthree'; echo; exit 7"}),
          "start /bin/sh");

    std::vector<std::string> lines;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (p.poll(lines) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(p.finished(), "finished (eof + reaped)");
    check(lines.size() == 3, "3 lines, got " + std::to_string(lines.size()));
    check(!lines.empty() && lines[0] == "one", "first line");
    check(lines.size() > 2 && lines[2] == "three", "last line arrives before exit is reported");
    check(p.exitCode() == 7, "exit code 7, got " + std::to_string(p.exitCode()));
}

static void testStderrMerged() {
    std::puts("\n-- stderr merge --");
    Process p;
    p.start({"/bin/sh", "-c", "echo out; echo err >&2"});
    std::vector<std::string> lines;
    while (p.poll(lines)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(lines.size() == 2, "stdout and stderr both captured");
}

static void testEnv() {
    std::puts("\n-- extraEnv --");
    Process p;
    p.start({"/bin/sh", "-c", "echo $CYBERDECK_TEST_VAR"}, {"CYBERDECK_TEST_VAR=hello42"});
    std::vector<std::string> lines;
    while (p.poll(lines)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(!lines.empty() && lines[0] == "hello42", "extraEnv reaches the child");
}

static void testStdinStop() {
    std::puts("\n-- writeLine / stop --");
    Process p;
    p.start({"/bin/sh", "-c", "read x; echo \"got:$x\""});
    check(p.writeLine("stop"), "writeLine");
    std::vector<std::string> lines;
    while (p.poll(lines)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(!lines.empty() && lines[0] == "got:stop", "child read our stdin line");
}

static void testGroupKill() {
    std::puts("\n-- group kill (the orphan test) --");
    // sh spawns a grandchild that outlives it unless the whole group is signalled.
    Process p;
    p.start({"/bin/sh", "-c",
             "sleep 300 & echo child=$!; wait"});
    std::vector<std::string> lines;
    for (int i = 0; i < 100 && lines.empty(); ++i) {
        p.poll(lines);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(!lines.empty(), "grandchild pid reported");
    std::string grandchild;
    if (!lines.empty() && lines[0].rfind("child=", 0) == 0) {
        grandchild = lines[0].substr(6);
    }

    p.requestStop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (p.poll(lines) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(p.finished(), "parent reaped after requestStop");

    if (!grandchild.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const std::string cmd = "kill -0 " + grandchild + " 2>/dev/null";
        const int alive = std::system(cmd.c_str());
        check(alive != 0, "grandchild (sleep 300) is gone, pid " + grandchild);
    }
}

static void testKillUnresponsive() {
    std::puts("\n-- SIGTERM-ignoring child escalates to SIGKILL --");
    Process p;
    p.start({"/bin/sh", "-c", "trap '' TERM; echo up; sleep 300"});
    std::vector<std::string> lines;
    for (int i = 0; i < 100 && lines.empty(); ++i) {
        p.poll(lines);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(!lines.empty(), "child running");
    p.requestStop();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (p.poll(lines) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    check(p.finished(), "escalated to SIGKILL within the 2s deadline");
}

static void testMissingBinary() {
    std::puts("\n-- missing binary --");
    Process p;
    const bool ok = p.start({"definitely-not-a-real-binary-xyz"});
    // posix_spawnp reports ENOENT synchronously on both platforms.
    check(!ok || p.exitCode() != 0, "missing binary does not hang or crash");
}

static void testPartialLines() {
    std::puts("\n-- partial line buffering --");
    Process p;
    p.start({"/bin/sh", "-c",
             "printf 'aa'; sleep 0.3; printf 'bb\\ncc'; sleep 0.3; printf 'dd\\n'"});
    std::vector<std::string> lines;
    while (p.poll(lines)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(lines.size() == 2, "2 reassembled lines, got " + std::to_string(lines.size()));
    check(!lines.empty() && lines[0] == "aabb", "fragment across reads joined");
    check(lines.size() > 1 && lines[1] == "ccdd", "second fragment joined");
}

static void testMock(const std::string& script) {
    std::puts("\n-- mock_deck.py --text --");
    Process p;
    const bool started = p.start({"python3", "-u", script, "--text", "what is a transistor",
                                  "--no-tts"},
                                 {"PYTHONUNBUFFERED=1"});
    if (!started) {
        check(false, "spawn python3");
        return;
    }
    std::vector<std::string> lines;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (p.poll(lines) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(p.finished(), "mock finished");
    check(p.exitCode() == 0, "mock exit 0, got " + std::to_string(p.exitCode()));

    std::string lastStage, answer, ctxTitle;
    int objects = 0;
    for (const auto& l : lines) {
        if (!jsonline::isObject(l)) continue;
        ++objects;
        if (auto s = jsonline::field(l, "stage")) {
            lastStage = *s;
            if (*s == "answer") {
                if (auto t = jsonline::field(l, "text")) answer = *t;
            }
            if (*s == "context") {
                if (auto t = jsonline::field(l, "title")) ctxTitle = *t;
            }
        }
    }
    check(objects >= 6, "protocol objects seen: " + std::to_string(objects));
    check(lastStage == "done", "ends on done, got '" + lastStage + "'");
    check(answer.size() > 40, "answer text captured (" + std::to_string(answer.size()) + " chars)");
    check(ctxTitle == "Transistor", "context title parsed, got '" + ctxTitle + "'");
}

static void testMockGarbage(const std::string& script) {
    std::puts("\n-- mock --fail garbage (non-JSON must not break parsing) --");
    Process p;
    p.start({"python3", "-u", script, "--text", "what is a transistor", "--no-tts",
             "--fail", "garbage"}, {"PYTHONUNBUFFERED=1"});
    std::vector<std::string> lines;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (p.poll(lines) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    int diagnostics = 0;
    std::string lastStage;
    for (const auto& l : lines) {
        if (!jsonline::isObject(l)) { ++diagnostics; continue; }
        if (auto s = jsonline::field(l, "stage")) lastStage = *s;
    }
    check(diagnostics >= 1, "non-JSON lines routed to diagnostics: " + std::to_string(diagnostics));
    check(lastStage == "done", "protocol still completed, got '" + lastStage + "'");
}

static void testRestart() {
    std::puts("\n-- reuse: one Process, several runs --");
    Process p;
    for (int run = 1; run <= 3; ++run) {
        const std::string msg = "run" + std::to_string(run);
        check(p.start({"/bin/sh", "-c", "echo " + msg}),
              "start #" + std::to_string(run) + " succeeds on a reused Process");
        std::vector<std::string> lines;
        while (p.poll(lines)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(!lines.empty() && lines.back() == msg,
              "run #" + std::to_string(run) + " output is fresh, not stale");
        check(p.exitCode() == 0, "run #" + std::to_string(run) + " exit 0");
    }

    // A live child must not be clobbered by a second start().
    Process busy;
    busy.start({"/bin/sh", "-c", "sleep 5"});
    std::vector<std::string> ignored;
    busy.poll(ignored);
    check(!busy.start({"/bin/sh", "-c", "echo nope"}),
          "start() refuses while a child is still running");
    busy.kill();
    while (busy.poll(ignored)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

int main(int argc, char** argv) {
    const std::string script = argc > 1 ? argv[1] : "assets/ai/mock_deck.py";
    testJsonLine();
    testEcho();
    testStderrMerged();
    testEnv();
    testStdinStop();
    testPartialLines();
    testRestart();
    testGroupKill();
    testKillUnresponsive();
    testMissingBinary();
    testMock(script);
    testMockGarbage(script);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
