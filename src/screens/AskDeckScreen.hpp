#pragma once

#include "ai/AiAssets.hpp"
#include "media/IPlayer.hpp"
#include "media/MediaLibrary.hpp"
#include "platform/Process.hpp"
#include "render/Font.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

#include <chrono>
#include <deque>
#include <string>
#include <vector>

namespace cyberdeck {

// Offline voice assistant, presented as a single conversation view: one
// scrolling chat log, an ASCII robot that mouths along while it speaks, and a
// prompt line that is always ready for input. Spawns ask_deck.py on demand and
// kills the whole process group when the screen closes, so nothing is left
// holding the mic or RAM.
class AskDeckScreen final : public Screen {
public:
    AskDeckScreen(Font* titleFont, Font* bodyFont, ScreenManager* navigator, AiAssets* ai,
                  MediaLibrary* library, IPlayer* player);

    void onEnter() override;
    void onExit() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

    // Typing is the resting state, so the prompt is live whenever we are idle.
    bool wantsTextInput() const override { return stage_ == Stage::Idle; }
    // Esc cancels a query in flight instead of tearing the screen down.
    bool consumesBack() const override { return isBusy(); }

private:
    enum class Stage {
        Blocked,       // required assets absent
        Idle,          // prompt is live
        Recording,
        Transcribing,
        Searching,
        Thinking,
        Speaking,
    };

    struct Message {
        bool fromUser = false;
        bool isError = false;
        std::string text;
        std::string note;   // e.g. which Wikipedia articles were used
        std::vector<std::string> lines;
    };

    void recheckAssets();
    void startVoice();
    void startText(const std::string& question);
    bool spawnHelper(const std::vector<std::string>& extraArgs);
    void consumeLine(const std::string& line);
    void pumpHelper();
    void cancelRun();
    void goIdle();

    Message& pushMessage(bool fromUser, const std::string& text, bool isError = false);
    void rewrap(Message& msg);
    void rewrapAll();
    int totalLines() const;
    void scrollToBottom();

    void drawRobot(IRenderer& renderer, float x, float y);
    void drawLog(IRenderer& renderer);
    const char* statusText() const;
    bool isBusy() const;

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    ScreenManager* navigator_ = nullptr;
    AiAssets* ai_ = nullptr;
    MediaLibrary* library_ = nullptr;
    IPlayer* player_ = nullptr;

    // Private face for conversation text. Those strings are unbounded and
    // Font's texture cache never evicts, so they must not share the shared
    // fonts' cache; this one is cleared when a reply finishes streaming.
    Font chatFont_;

    Process proc_;
    Stage stage_ = Stage::Idle;
    std::vector<Message> messages_;
    std::string typed_;
    std::string streaming_;        // reply text accumulated from token stages
    int streamingIndex_ = -1;      // message currently being streamed into
    std::deque<std::string> diagnostics_;

    int scroll_ = 0;               // first visible line
    bool autoScroll_ = true;
    bool stopping_ = false;
    bool pausedPlaybackForUs_ = false;
    float animTime_ = 0.0f;
    float streamTimer_ = 0.0f;
    float blockedRecheckTimer_ = 0.0f;
    std::chrono::steady_clock::time_point recordStart_{};
};

}  // namespace cyberdeck
