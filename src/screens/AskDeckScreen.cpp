#include "screens/AskDeckScreen.hpp"

#include "core/Assets.hpp"
#include "core/JsonLine.hpp"
#include "core/Types.hpp"
#include "render/TextLayout.hpp"
#include "screens/AiAssetsScreen.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace cyberdeck {

namespace {

constexpr float kMaxRecordSeconds = 30.0f;
constexpr float kRecordWatchdogSeconds = 32.0f;
constexpr std::size_t kMaxDiagnostics = 20;
constexpr std::size_t kMaxMessages = 40;   // bounds both RAM and the font cache

// Conversation column.
constexpr float kLogX = 64.0f;
constexpr float kLogY = 96.0f;
constexpr float kLogW = 812.0f;
constexpr float kLogH = 474.0f;
constexpr float kLabelW = 74.0f;
constexpr float kLineH = 26.0f;
constexpr float kTextW = kLogW - kLabelW;

// Robot column.
constexpr float kBotX = 916.0f;
constexpr float kBotY = 96.0f;
constexpr float kBotW = 300.0f;

constexpr float kPromptY = 596.0f;
constexpr float kHintY = 664.0f;

// Re-wrapping streamed text on every token would thrash both the layout and the
// glyph cache; twelve times a second still reads as live typing.
constexpr float kStreamRewrapInterval = 0.08f;

// Fixed frame strings, so the shared font cache sees a bounded alphabet.
constexpr const char* kMouths[] = {"-----", ".ooo.", "ooooo", ".ooo."};
constexpr int kMouthCount = 4;
constexpr const char* kEyesOpen = "O   O";
constexpr const char* kEyesShut = "-   -";
constexpr const char* kEyesLook = "o   o";

}  // namespace

AskDeckScreen::AskDeckScreen(Font* titleFont, Font* bodyFont, ScreenManager* navigator,
                             AiAssets* ai, MediaLibrary* library, IPlayer* player)
    : Screen(900, "AskDeck"),
      titleFont_(titleFont),
      bodyFont_(bodyFont),
      navigator_(navigator),
      ai_(ai),
      library_(library),
      player_(player) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});
    setFocusable(false);

    chatFont_.load(assets::findFont(), 22);
}

bool AskDeckScreen::isBusy() const {
    switch (stage_) {
        case Stage::Recording:
        case Stage::Transcribing:
        case Stage::Searching:
        case Stage::Thinking:
        case Stage::Speaking:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------- messages --

AskDeckScreen::Message& AskDeckScreen::pushMessage(bool fromUser, const std::string& text,
                                                   bool isError) {
    Message msg;
    msg.fromUser = fromUser;
    msg.isError = isError;
    msg.text = text;
    messages_.push_back(std::move(msg));
    if (messages_.size() > kMaxMessages) {
        messages_.erase(messages_.begin(), messages_.begin() + 8);
        chatFont_.clearCache();  // the trimmed lines will never be drawn again
        if (streamingIndex_ >= 0) {
            streamingIndex_ = std::max(0, streamingIndex_ - 8);
        }
    }
    rewrap(messages_.back());
    scrollToBottom();
    return messages_.back();
}

void AskDeckScreen::rewrap(Message& msg) {
    msg.lines = wrapText(chatFont_, msg.text, kTextW, 200);
    if (msg.lines.empty() && !msg.text.empty()) {
        msg.lines.push_back(msg.text);
    }
}

void AskDeckScreen::rewrapAll() {
    for (Message& msg : messages_) {
        rewrap(msg);
    }
}

int AskDeckScreen::totalLines() const {
    int n = 0;
    for (const Message& msg : messages_) {
        n += static_cast<int>(msg.lines.size());
        if (!msg.note.empty()) {
            ++n;
        }
        ++n;  // blank spacer between turns
    }
    return n;
}

void AskDeckScreen::scrollToBottom() {
    autoScroll_ = true;
    const int visible = static_cast<int>(kLogH / kLineH);
    scroll_ = std::max(0, totalLines() - visible);
}

// ------------------------------------------------------------------ assets --

void AskDeckScreen::recheckAssets() {
    if (!ai_) {
        stage_ = Stage::Blocked;
        return;
    }
    ai_->refresh();
    if (!ai_->status().ready) {
        stage_ = Stage::Blocked;
        return;
    }
    if (stage_ == Stage::Blocked) {
        stage_ = Stage::Idle;
    }
}

void AskDeckScreen::onEnter() {
    if (!isBusy()) {
        recheckAssets();
        if (messages_.empty() && stage_ != Stage::Blocked) {
            pushMessage(false,
                        "Hey. Ask me anything — press ENTER to talk, or just start "
                        "typing. I run entirely offline.");
        }
    }

    // mpv owns the audio device while it plays, which would starve the recorder
    // and talk over the spoken reply.
    if (player_) {
        const PlayerStatus ps = player_->status();
        if (ps.playing && !ps.paused) {
            player_->togglePause();
            pausedPlaybackForUs_ = true;
        }
    }
    Screen::onEnter();
}

void AskDeckScreen::onExit() {
    // Last chance to signal before destruction. ScreenManager stops updating a
    // popping screen, so SIGTERM->SIGKILL escalation lives in ~Process().
    proc_.requestStop();

    if (pausedPlaybackForUs_ && player_) {
        if (player_->status().paused) {
            player_->togglePause();
        }
        pausedPlaybackForUs_ = false;
    }
    Screen::onExit();
}

// ------------------------------------------------------------------ helper --

bool AskDeckScreen::spawnHelper(const std::vector<std::string>& extraArgs) {
    if (!ai_) {
        return false;
    }
    std::string script = ai_->helperScriptPath();
    if (script.empty()) {
        script = ai_->mockScriptPath();
    }
    if (script.empty()) {
        pushMessage(false, "I can't find ask_deck.py in assets/ai/.", true);
        stage_ = Stage::Idle;
        return false;
    }

    std::vector<std::string> argv = {"python3", "-u", script};
    argv.insert(argv.end(), extraArgs.begin(), extraArgs.end());

    diagnostics_.clear();
    stopping_ = false;
    streaming_.clear();
    streamingIndex_ = -1;

    if (!proc_.start(argv, ai_->helperEnv())) {
        pushMessage(false, "I couldn't start python3.", true);
        stage_ = Stage::Idle;
        return false;
    }
    return true;
}

void AskDeckScreen::startVoice() {
    char limit[16];
    std::snprintf(limit, sizeof(limit), "%.0f", kMaxRecordSeconds);
    if (spawnHelper({"--voice", "--max-seconds", limit})) {
        stage_ = Stage::Recording;
        recordStart_ = std::chrono::steady_clock::now();
    }
}

void AskDeckScreen::startText(const std::string& question) {
    pushMessage(true, question);
    if (spawnHelper({"--text", question})) {
        stage_ = Stage::Thinking;
    }
}

void AskDeckScreen::consumeLine(const std::string& line) {
    if (!jsonline::isObject(line)) {
        if (!line.empty()) {
            diagnostics_.push_back(line);
            while (diagnostics_.size() > kMaxDiagnostics) {
                diagnostics_.pop_front();
            }
        }
        return;
    }

    const auto stage = jsonline::field(line, "stage");
    if (!stage) {
        return;
    }
    const std::string& s = *stage;

    if (s == "recording") {
        stage_ = Stage::Recording;
        recordStart_ = std::chrono::steady_clock::now();
    } else if (s == "transcribing") {
        stage_ = Stage::Transcribing;
        stopping_ = false;
    } else if (s == "transcript") {
        if (auto text = jsonline::field(line, "text")) {
            pushMessage(true, *text);
        }
    } else if (s == "searching") {
        stage_ = Stage::Searching;
    } else if (s == "context") {
        const int used = jsonline::toInt(jsonline::field(line, "used"));
        std::string note;
        if (used > 0) {
            const auto sources = jsonline::field(line, "sources");
            const auto title = jsonline::field(line, "title");
            note = "from wikipedia · ";
            note += sources && !sources->empty() ? *sources : (title ? *title : "offline copy");
        } else {
            const auto reason = jsonline::field(line, "reason");
            const std::string r = reason ? *reason : "";
            if (r == "no_zim") {
                note = "no offline wikipedia installed — answering from memory";
            } else if (r == "zim_error") {
                note = "couldn't read the wikipedia file — answering from memory";
            } else {
                note = "nothing in wikipedia matched — answering from memory";
            }
        }
        // Attach to the reply we are about to stream.
        Message& msg = pushMessage(false, "");
        msg.note = note;
        streamingIndex_ = static_cast<int>(messages_.size()) - 1;
        streaming_.clear();
    } else if (s == "thinking") {
        stage_ = Stage::Thinking;
        if (streamingIndex_ < 0) {
            pushMessage(false, "");
            streamingIndex_ = static_cast<int>(messages_.size()) - 1;
            streaming_.clear();
        }
    } else if (s == "token") {
        if (auto text = jsonline::field(line, "text")) {
            streaming_ += *text;
        }
    } else if (s == "answer") {
        // Authoritative, tidied text replaces whatever the tokens built up.
        if (auto text = jsonline::field(line, "text")) {
            if (streamingIndex_ >= 0 &&
                streamingIndex_ < static_cast<int>(messages_.size())) {
                messages_[static_cast<std::size_t>(streamingIndex_)].text = *text;
                chatFont_.clearCache();  // drop the partial-line textures
                rewrapAll();
                scrollToBottom();
            } else {
                pushMessage(false, *text);
            }
        }
        streaming_.clear();
        streamingIndex_ = -1;
    } else if (s == "speaking") {
        stage_ = Stage::Speaking;
    } else if (s == "done") {
        goIdle();
    } else if (s == "error") {
        const auto text = jsonline::field(line, "text");
        const auto code = jsonline::field(line, "code");
        std::string message = text && !text->empty() ? *text : "Something went wrong.";
        if (code && *code == "no_speech") {
            message = "I didn't catch that — try again, or type it instead.";
        } else if (code && *code == "no_mic") {
            message = std::string("I can't reach a microphone. ") +
                      "You can still type your question.";
        }
        if (streamingIndex_ >= 0 && streamingIndex_ < static_cast<int>(messages_.size()) &&
            messages_[static_cast<std::size_t>(streamingIndex_)].text.empty()) {
            messages_.pop_back();  // drop the empty reply bubble
        }
        streamingIndex_ = -1;
        streaming_.clear();
        pushMessage(false, message, true);
        goIdle();
    }
}

void AskDeckScreen::pumpHelper() {
    if (!proc_.started()) {
        return;
    }
    std::vector<std::string> lines;
    proc_.poll(lines);
    for (const std::string& line : lines) {
        consumeLine(line);
    }

    if (proc_.finished() && isBusy()) {
        if (streamingIndex_ >= 0 && streamingIndex_ < static_cast<int>(messages_.size()) &&
            messages_[static_cast<std::size_t>(streamingIndex_)].text.empty()) {
            messages_.pop_back();
        }
        streamingIndex_ = -1;
        pushMessage(false,
                    "The helper stopped unexpectedly (exit " +
                        std::to_string(proc_.exitCode()) + ").",
                    true);
        goIdle();
    }
}

void AskDeckScreen::cancelRun() {
    proc_.requestStop();
    if (streamingIndex_ >= 0 && streamingIndex_ < static_cast<int>(messages_.size()) &&
        messages_[static_cast<std::size_t>(streamingIndex_)].text.empty()) {
        messages_.pop_back();
    }
    streamingIndex_ = -1;
    streaming_.clear();
    goIdle();
}

void AskDeckScreen::goIdle() {
    stage_ = Stage::Idle;
    stopping_ = false;
    scrollToBottom();
}

// ------------------------------------------------------------------- input --

void AskDeckScreen::handleInput(const Input& input) {
    for (Action action : input.actions()) {
        if (action == Action::Back && isBusy()) {
            cancelRun();
            return;
        }
    }

    if (stage_ == Stage::Idle) {
        if (!input.textChars().empty()) {
            typed_ += input.textChars();
        }
        if (input.textBackspace() && !typed_.empty()) {
            typed_.pop_back();
        }
    }

    const int visible = static_cast<int>(kLogH / kLineH);
    for (Action action : input.actions()) {
        switch (action) {
            case Action::Up:
                scroll_ = std::max(0, scroll_ - 1);
                autoScroll_ = false;
                break;
            case Action::Down: {
                const int maxScroll = std::max(0, totalLines() - visible);
                scroll_ = std::min(maxScroll, scroll_ + 1);
                autoScroll_ = scroll_ >= maxScroll;
                break;
            }
            case Action::Confirm:
                switch (stage_) {
                    case Stage::Blocked:
                        if (navigator_ && ai_) {
                            navigator_->push(std::make_unique<AiAssetsScreen>(
                                titleFont_, bodyFont_, ai_, navigator_));
                        }
                        break;
                    case Stage::Idle:
                        // Empty prompt means "talk to me"; otherwise send it.
                        if (typed_.empty()) {
                            startVoice();
                        } else {
                            const std::string question = typed_;
                            typed_.clear();
                            startText(question);
                        }
                        break;
                    case Stage::Recording:
                        if (!stopping_) {
                            stopping_ = true;
                            proc_.writeLine("stop");
                        }
                        break;
                    case Stage::Speaking:
                        proc_.writeLine("skip");
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }
}

void AskDeckScreen::update(float dt) {
    animTime_ += dt;
    pumpHelper();

    // Push streamed text into the visible bubble on a throttle.
    if (streamingIndex_ >= 0 && !streaming_.empty()) {
        streamTimer_ += dt;
        if (streamTimer_ >= kStreamRewrapInterval) {
            streamTimer_ = 0.0f;
            const std::size_t i = static_cast<std::size_t>(streamingIndex_);
            if (i < messages_.size() && messages_[i].text != streaming_) {
                messages_[i].text = streaming_;
                rewrap(messages_[i]);
                if (autoScroll_) {
                    scrollToBottom();
                }
            }
        }
    }

    if (stage_ == Stage::Recording) {
        const float elapsed =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - recordStart_)
                .count();
        if (elapsed > kRecordWatchdogSeconds) {
            proc_.requestStop();
            pushMessage(false, "The recorder didn't stop — check the microphone.", true);
            goIdle();
        }
    }

    if (stage_ == Stage::Blocked) {
        blockedRecheckTimer_ += dt;
        if (blockedRecheckTimer_ >= 1.0f) {
            blockedRecheckTimer_ = 0.0f;
            recheckAssets();
        }
    }

    Screen::update(dt);
}

// ------------------------------------------------------------------ drawing --

const char* AskDeckScreen::statusText() const {
    switch (stage_) {
        case Stage::Blocked: return "NEEDS SETUP";
        case Stage::Idle: return "READY";
        case Stage::Recording: return "LISTENING";
        case Stage::Transcribing: return "HEARING YOU";
        case Stage::Searching: return "LOOKING IT UP";
        case Stage::Thinking: return "THINKING";
        case Stage::Speaking: return "SPEAKING";
    }
    return "";
}

void AskDeckScreen::drawRobot(IRenderer& renderer, float x, float y) {
    if (!bodyFont_) {
        return;
    }

    // Mouth only moves while speaking; a slow blink otherwise keeps it alive.
    const char* mouth = kMouths[0];
    if (stage_ == Stage::Speaking) {
        const int frame = static_cast<int>(animTime_ * 9.0f) % kMouthCount;
        mouth = kMouths[frame];
    } else if (stage_ == Stage::Thinking || stage_ == Stage::Searching) {
        mouth = (static_cast<int>(animTime_ * 2.0f) % 2) ? ".ooo." : "-----";
    }

    const char* eyes = kEyesOpen;
    const float blink = std::fmod(animTime_, 4.0f);
    if (blink < 0.12f) {
        eyes = kEyesShut;
    } else if (stage_ == Stage::Recording || stage_ == Stage::Transcribing) {
        eyes = (static_cast<int>(animTime_ * 3.0f) % 2) ? kEyesLook : kEyesOpen;
    }

    const bool live = isBusy();
    const Color shell = live ? kAccent : kTextDim;
    const Color face = live ? kTextBright : kTextDim;

    // Assembled from fixed pieces, so the glyph cache stays small. Every row is
    // the same character count: VT323 is monospace, so a short row would pull
    // the antenna off-centre from the head.
    const std::string rows[] = {
        "     \\   /    ",
        "   .-------.  ",
        std::string("   | ") + eyes + " |  ",
        "   |       |  ",
        std::string("   | ") + mouth + " |  ",
        "   '-------'  ",
        "    |     |   ",
    };

    float ry = y;
    for (std::size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        const bool isFace = (i == 2 || i == 4);
        bodyFont_->draw(renderer, rows[i], {x, ry}, isFace ? face : shell);
        ry += 28.0f;
    }

    // Status chip under the robot.
    const Rect chip{x, ry + 12.0f, 230.0f, 34.0f};
    renderer.drawRect(chip, live ? kCardFocused : kCard);
    renderer.drawRect(Rect{chip.x, chip.y, 4.0f, chip.h}, live ? kAccent : kBorder);
    bodyFont_->draw(renderer, statusText(), {chip.x + 16.0f, chip.y + 4.0f},
                    live ? kTextBright : kTextDim);

    if (stage_ == Stage::Recording) {
        const float elapsed =
            std::chrono::duration<float>(std::chrono::steady_clock::now() - recordStart_)
                .count();
        const float frac = std::clamp(elapsed / kMaxRecordSeconds, 0.0f, 1.0f);
        renderer.drawRect(Rect{chip.x, chip.y + 46.0f, 230.0f, 8.0f},
                          Color::fromBytes(12, 28, 16));
        renderer.drawRect(Rect{chip.x, chip.y + 46.0f, 230.0f * frac, 8.0f}, kAccent);
    }
}

void AskDeckScreen::drawLog(IRenderer& renderer) {
    if (!chatFont_.valid() || !bodyFont_) {
        return;
    }
    const int visible = static_cast<int>(kLogH / kLineH);

    int lineIndex = 0;   // running line number across all messages
    int drawn = 0;
    float y = kLogY;

    auto emitLine = [&](const std::string& text, float indent, const Color& colour,
                        bool useChat) {
        if (lineIndex >= scroll_ && drawn < visible) {
            if (useChat) {
                chatFont_.draw(renderer, text, {kLogX + indent, y}, colour);
            } else {
                bodyFont_->draw(renderer, text, {kLogX + indent, y}, colour);
            }
            y += kLineH;
            ++drawn;
        }
        ++lineIndex;
    };

    for (const Message& msg : messages_) {
        const Color colour =
            msg.isError ? Color::fromBytes(255, 140, 90)
                        : (msg.fromUser ? kTextDim : kTextBright);

        for (std::size_t i = 0; i < msg.lines.size(); ++i) {
            if (i == 0 && lineIndex >= scroll_ && drawn < visible) {
                bodyFont_->draw(renderer, msg.fromUser ? "YOU" : "DECK", {kLogX, y},
                                msg.fromUser ? kTextFaint : kAccent);
            }
            emitLine(msg.lines[i], kLabelW, colour, true);
        }
        if (!msg.note.empty()) {
            emitLine(msg.note, kLabelW, kTextFaint, false);
        }
        emitLine("", 0.0f, kTextDim, false);   // spacer
    }

    // Caret on the reply currently streaming in.
    if (streamingIndex_ >= 0 && stage_ == Stage::Thinking &&
        static_cast<int>(animTime_ * 2.0f) % 2 == 0 && drawn < visible) {
        chatFont_.draw(renderer, "_", {kLogX + kLabelW, y - kLineH}, kAccent);
    }

    if (totalLines() > visible) {
        const float frac = static_cast<float>(visible) / static_cast<float>(totalLines());
        const float track = kLogH;
        const float thumb = std::max(24.0f, track * frac);
        const float maxScroll = static_cast<float>(std::max(1, totalLines() - visible));
        const float pos = (track - thumb) * (static_cast<float>(scroll_) / maxScroll);
        renderer.drawRect(Rect{kLogX + kLogW + 12.0f, kLogY, 3.0f, track}, kCard);
        renderer.drawRect(Rect{kLogX + kLogW + 12.0f, kLogY + pos, 3.0f, thumb}, kBorderFocused);
    }
}

void AskDeckScreen::draw(IRenderer& renderer) {
    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    if (titleFont_) {
        titleFont_->draw(renderer, "ASK THE DECK", {64, 32}, kAccent);
    }

    if (stage_ == Stage::Blocked) {
        const AiAssetsStatus status = ai_ ? ai_->status() : AiAssetsStatus{};
        renderer.drawRect(Rect{64, 150, 1152, 160}, kCard);
        if (bodyFont_) {
            bodyFont_->draw(renderer, "I'm not set up yet.", {88, 172}, kAccent);
            bodyFont_->draw(renderer,
                            std::to_string(status.presentCount) + " of " +
                                std::to_string(status.totalCount) + " pieces installed",
                            {88, 210}, kTextDim);
            bodyFont_->draw(renderer, "PRESS ENTER TO OPEN AI ASSETS", {88, 250}, kTextBright);
            bodyFont_->draw(renderer, "ENTER OPEN SETUP  ·  ESC BACK", {64, kHintY}, kTextDim);
        }
        drawRobot(renderer, kBotX, kBotY);
        return;
    }

    drawLog(renderer);
    drawRobot(renderer, kBotX, kBotY);

    // Prompt line. Always visible so the screen reads as a conversation rather
    // than a machine with modes.
    const Rect prompt{kLogX, kPromptY, kLogW + 16.0f, 44.0f};
    renderer.drawRect(prompt, stage_ == Stage::Idle ? kCard : kBgPanel);
    renderer.drawRect(Rect{prompt.x, prompt.y, 4.0f, prompt.h},
                      stage_ == Stage::Idle ? kAccent : kBorder);

    if (chatFont_.valid()) {
        if (stage_ == Stage::Idle) {
            const bool caret = static_cast<int>(animTime_ * 2.0f) % 2 == 0;
            const std::string shown =
                typed_.empty()
                    ? std::string("Ask me something…")
                    : typed_;
            chatFont_.draw(renderer, shown, {prompt.x + 18.0f, prompt.y + 10.0f},
                           typed_.empty() ? kTextFaint : kTextBright);
            if (caret) {
                const float w = chatFont_.measure(typed_).x;
                chatFont_.draw(renderer, "_",
                               {prompt.x + 18.0f + (typed_.empty() ? 0.0f : w),
                                prompt.y + 10.0f},
                               kAccent);
            }
        } else if (bodyFont_) {
            bodyFont_->draw(renderer, statusText(), {prompt.x + 18.0f, prompt.y + 8.0f},
                            kTextDim);
        }
    }

    if (bodyFont_) {
        const char* hint = "ENTER SEND  ·  EMPTY + ENTER TO TALK  ·  UP/DOWN SCROLL  ·  ESC BACK";
        switch (stage_) {
            case Stage::Recording:
                hint = "ENTER STOP RECORDING  ·  ESC CANCEL";
                break;
            case Stage::Speaking:
                hint = "ENTER SKIP  ·  ESC CANCEL";
                break;
            case Stage::Transcribing:
            case Stage::Searching:
            case Stage::Thinking:
                hint = "WORKING…  ·  ESC CANCEL";
                break;
            default:
                break;
        }
        bodyFont_->draw(renderer, hint, {64, kHintY}, kTextDim);
    }
}

}  // namespace cyberdeck
