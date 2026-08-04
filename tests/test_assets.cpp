// Scratch harness for AiAssets: presence scanning, the ready gate, verify
// checks, and download idempotence. Runs with no network — every assertion is
// about local state, and the one download call is expected to be a no-op.
#include "ai/AiAssets.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace cyberdeck;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool cond, const std::string& what) {
    std::printf("%s  %s\n", cond ? "  ok  " : " FAIL ", what.c_str());
    if (!cond) ++failures;
}

static const AiAssetState* find(const AiAssetsStatus& s, const std::string& key) {
    for (const auto& a : s.assets) {
        if (a.key == key) return &a;
    }
    return nullptr;
}

static void waitIdle(AiAssets& ai) {
    for (int i = 0; i < 600 && ai.isRunning(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

static void writeBytes(const fs::path& p, const std::string& bytes) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Rebuild the fake asset tree from scratch. Each block starts from a known
// state rather than undoing the previous one's mutations.
static void resetTree(const fs::path& home, const fs::path& fakeDrive) {
    const fs::path ai = home / ".local/share/cyberdeck/ai";
    std::error_code ec;
    fs::remove_all(ai, ec);
    fs::remove_all(fakeDrive, ec);
    for (const char* sub : {"models", "voices", "bin", "logs"}) {
        fs::create_directories(ai / sub, ec);
    }
    writeBytes(ai / "models/ggml-tiny.en.bin", "");
    writeBytes(ai / "voices/en_US-amy-low.onnx", "");
    writeBytes(ai / "voices/en_US-amy-low.onnx.json", "");
    fs::create_directories(
        home / ".ollama/models/manifests/registry.ollama.ai/library/qwen2.5/0.5b", ec);
    writeBytes(home / ".ollama/models/manifests/registry.ollama.ai/library/qwen2.5/0.5b/manifest",
               "{}");
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    const char* home = std::getenv("HOME");
    const fs::path homeP(home);
    const fs::path aiRoot = homeP / ".local/share/cyberdeck/ai";
    const fs::path fakeDrive = homeP / "fakedrive";

    // Stand in for a mounted PI LIB so the ZIM slot has a destination.
    MediaRootInfo root;
    root.path = fakeDrive.string();
    root.cacheDir = (fakeDrive / ".cyberdeck").string();
    root.dbPath = (fakeDrive / ".cyberdeck/library.db").string();
    root.thumbsDir = (fakeDrive / ".cyberdeck/thumbs").string();
    root.found = true;

    {
        resetTree(homeP, fakeDrive);
        std::puts("\n-- presence scan with a full fake tree --");
        AiAssets ai;
        ai.initialize(&root);
        const AiAssetsStatus s = ai.status();
        check(s.totalCount == 9, "9 assets tracked, got " + std::to_string(s.totalCount));
        check(s.ready, "all required assets present => ready");
        check(!s.zimPresent, "ZIM absent (it is optional, so ready stays true)");
        check(ai.zimDir() == (fakeDrive / ".cyberdeck/ai/zim").string(),
              "ZIM directory derives from the media root cache dir");
        check(fs::exists(ai.zimDir()), "ZIM directory was created");
        check(ai.ollamaModelTag() == "qwen2.5:0.5b", "smallest viable LLM by default");

        const AiAssetState* whisper = find(s, "whisper-model");
        check(whisper && whisper->required, "whisper model is required");
        const AiAssetState* zim = find(s, "zim");
        check(zim && !zim->required, "ZIM is explicitly optional");
    }

    {
        resetTree(homeP, fakeDrive);
        std::puts("\n-- ready gate drops when a required asset goes missing --");
        const fs::path model = aiRoot / "models/ggml-tiny.en.bin";
        fs::remove(model);
        AiAssets ai;
        ai.initialize(&root);
        check(!ai.status().ready, "missing whisper model clears ready");
        check(ai.status().totalMissingBytes > 70L * 1024 * 1024,
              "missing bytes reported for the headline figure");
    }

    {
        resetTree(homeP, fakeDrive);
        std::puts("\n-- partial download is reported as resumable --");
        const fs::path voice = aiRoot / "voices/en_US-amy-low.onnx";
        fs::remove(voice);
        writeBytes(voice.string() + ".part", std::string(4096, 'x'));
        AiAssets ai;
        ai.initialize(&root);
        // status() returns by value: bind it before taking a pointer into it.
        const AiAssetsStatus s = ai.status();
        const AiAssetState* v = find(s, "piper-voice");
        check(v && !v->present, "asset with only a .part is not present");
        check(v && v->haveBytes == 4096, "the .part size is surfaced as progress");
        check(v && v->note.find("resume") != std::string::npos,
              "note says it will resume, got: " + (v ? v->note : "<none>"));
    }

    {
        resetTree(homeP, fakeDrive);
        std::puts("\n-- verify: bad ZIM magic is caught without hashing 1GB --");
        // Filename must track kZimFile in AiAssets.cpp — Kiwix publishes dated
        // builds, so this changes whenever the pinned month is bumped.
        const fs::path zimFile =
            fs::path(fakeDrive) / ".cyberdeck/ai/zim/wikipedia_en_simple_all_nopic_2026-05.zim";
        writeBytes(zimFile, "NOTAZIMFILE.........");
        AiAssets ai;
        ai.initialize(&root);
        ai.startVerify();
        waitIdle(ai);
        const AiAssetsStatus s = ai.status();
        const AiAssetState* zim = find(s, "zim");
        check(zim && !zim->present, "bogus ZIM is not accepted");
        // A 20-byte file is caught by the truncation check before the magic read.
        check(zim && (zim->note.find("truncated") != std::string::npos ||
                      zim->note.find("not a valid ZIM") != std::string::npos),
              "verify explains why, got: " + (zim ? zim->note : "<none>"));
        check(s.finished && !s.running, "verify finishes and clears its running flag");
    }

    {
        // Regression: the hardcoded expectedBytes are estimates that go stale
        // when upstream reissues a file. Using one as an exact gate rejected
        // two perfectly good piper downloads and, because curl -C - then asked
        // for a range past EOF, wedged them permanently behind an HTTP 416.
        // A size that merely disagrees with the constant must still verify.
        resetTree(homeP, fakeDrive);
        std::puts("\n-- verify: a good file whose constant drifted is still accepted --");
        writeBytes(aiRoot / "models/ggml-tiny.en.bin", std::string(60L * 1024 * 1024, 'z'));
        AiAssets ai;
        ai.initialize(&root);
        ai.startVerify();
        waitIdle(ai);
        const AiAssetsStatus s = ai.status();
        const AiAssetState* w = find(s, "whisper-model");
        check(w && w->present,
              "size differing from the constant does not fail verify, note: " +
                  (w ? w->note : "<none>"));

        // Something far too small is still caught.
        writeBytes(aiRoot / "models/ggml-tiny.en.bin", std::string(1024, 'z'));
        AiAssets ai2;
        ai2.initialize(&root);
        ai2.startVerify();
        waitIdle(ai2);
        const AiAssetsStatus s2 = ai2.status();
        const AiAssetState* w2 = find(s2, "whisper-model");
        check(w2 && !w2->present, "a truncated file is still rejected");
        check(w2 && w2->note.find("truncated") != std::string::npos,
              "truncation is named, got: " + (w2 ? w2->note : "<none>"));
    }

    {
        resetTree(homeP, fakeDrive);
        std::puts("\n-- verify: stub binaries are probed via --version --");
        AiAssets ai;
        ai.initialize(&root);
        ai.startVerify();
        waitIdle(ai);
        const AiAssetsStatus s = ai.status();
        const AiAssetState* ollama = find(s, "ollama-bin");
        check(ollama && ollama->present, "stub ollama on PATH is detected");
        check(ollama && ollama->note.find("stub") != std::string::npos,
              "the --version output is captured, got: " + (ollama ? ollama->note : "<none>"));
    }

    {
        resetTree(homeP, fakeDrive);
        std::puts("\n-- download is idempotent (no network touched) --");
        AiAssets ai;
        ai.initialize(nullptr);  // no ZIM destination => nothing is fetchable
        const auto before = ai.status();
        ai.startDownload();
        waitIdle(ai);
        const auto after = ai.status();
        check(after.finished, "download run completed");
        check(after.message.find("already present") != std::string::npos,
              "nothing was fetched, got: " + after.message);
        check(before.presentCount == after.presentCount, "present count unchanged");
    }

    {
        resetTree(homeP, fakeDrive);
        std::puts("\n-- helper env block --");
        AiAssets ai;
        ai.initialize(&root);
        const auto env = ai.helperEnv();
        bool hasModel = false, hasTmp = false, hasUnbuffered = false, hasZim = false;
        for (const auto& kv : env) {
            if (kv.rfind("CYBERDECK_OLLAMA_MODEL=", 0) == 0) hasModel = true;
            if (kv.rfind("CYBERDECK_AI_TMP=", 0) == 0) hasTmp = true;
            if (kv == "PYTHONUNBUFFERED=1") hasUnbuffered = true;
            if (kv.rfind("CYBERDECK_ZIM=", 0) == 0) hasZim = true;
        }
        check(hasModel, "ollama model is passed through");
        check(hasTmp, "temp dir is passed through");
        check(hasZim, "ZIM var is always present (empty when absent)");
        check(hasUnbuffered, "PYTHONUNBUFFERED is set so stages stream live");
    }

    {
        resetTree(homeP, fakeDrive);
        std::puts("\n-- no media root: ZIM slot degrades, feature stays ready --");
        AiAssets ai;
        ai.initialize(nullptr);
        const auto s = ai.status();
        check(ai.zimDir().empty(), "no ZIM directory without PI LIB");
        check(s.ready, "still ready: the ZIM is optional");
        const AiAssetState* zim = find(s, "zim");
        check(zim && zim->note.find("PI LIB") != std::string::npos,
              "ZIM row explains the drive is missing, got: " + (zim ? zim->note : "<none>"));
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
