#include "screens/TerminalScreen.hpp"

#include "core/Types.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace cyberdeck {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxLines = 300;
constexpr int kVisibleLines = 18;

}  // namespace

TerminalScreen::TerminalScreen(Font* titleFont, Font* bodyFont)
    : Screen(800, "Terminal"), titleFont_(titleFont), bodyFont_(bodyFont) {
    bounds_ = Rect{0, 0, 1280, 720};
    setFill(Color{0, 0, 0, 0});

    std::error_code ec;
    cwd_ = fs::current_path(ec).string();
    if (ec) {
        cwd_ = ".";
    }

    appendLine("CYBERDECK TERMINAL");
    appendLine("Type shell commands. Esc returns home.");
    appendLine("cwd: " + cwd_);
    appendLine("");
}

TerminalScreen::~TerminalScreen() = default;

void TerminalScreen::onEnter() {
    Screen::onEnter();
}

void TerminalScreen::onExit() {
    Screen::onExit();
}

void TerminalScreen::clampScrollback() {
    while (static_cast<int>(lines_.size()) > kMaxLines) {
        lines_.erase(lines_.begin());
    }
}

void TerminalScreen::appendLine(std::string line) {
    // Wrap long lines roughly for the panel width.
    constexpr std::size_t kWrap = 88;
    if (line.size() <= kWrap) {
        lines_.push_back(std::move(line));
    } else {
        for (std::size_t i = 0; i < line.size(); i += kWrap) {
            lines_.push_back(line.substr(i, kWrap));
        }
    }
    clampScrollback();
    scrollOffset_ = 0;
}

void TerminalScreen::runCommand(const std::string& command) {
    appendLine("> " + command);

    if (command.empty()) {
        return;
    }

    if (command == "clear" || command == "cls") {
        lines_.clear();
        appendLine("CYBERDECK TERMINAL");
        return;
    }

    if (command == "pwd") {
        appendLine(cwd_);
        return;
    }

    if (command.rfind("cd ", 0) == 0 || command == "cd") {
        std::string target = command.size() > 3 ? command.substr(3) : std::string(getenv("HOME") ? getenv("HOME") : "/");
        while (!target.empty() && (target.front() == ' ' || target.front() == '\t')) {
            target.erase(target.begin());
        }
        fs::path next = fs::path(target);
        if (!next.is_absolute()) {
            next = fs::path(cwd_) / next;
        }
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(next, ec);
        if (!ec && fs::is_directory(canon, ec)) {
            cwd_ = canon.string();
            appendLine(cwd_);
        } else {
            appendLine("cd: no such directory");
        }
        return;
    }

    // Run in the terminal's cwd (quote path for spaces).
    const std::string full =
        "cd \"" + cwd_ + "\" && " + command + " 2>&1";
    FILE* pipe = popen(full.c_str(), "r");
    if (!pipe) {
        appendLine("error: failed to run command");
        return;
    }

    std::array<char, 512> buffer {};
    bool any = false;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        any = true;
        std::string line(buffer.data());
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        appendLine(std::move(line));
    }
    const int code = pclose(pipe);
    if (!any) {
        appendLine("(no output)");
    }
    if (code != 0) {
        appendLine("exit code: " + std::to_string(code));
    }
}

void TerminalScreen::handleInput(const Input& input) {
    // Text entry
    if (!input.textChars().empty()) {
        input_ += input.textChars();
    }
    if (input.textBackspace() && !input_.empty()) {
        input_.pop_back();
    }

    for (Action action : input.actions()) {
        if (action == Action::Confirm) {
            const std::string cmd = input_;
            input_.clear();
            runCommand(cmd);
        } else if (action == Action::Up) {
            scrollOffset_ = std::min(scrollOffset_ + 1, std::max(0, static_cast<int>(lines_.size()) - 1));
        } else if (action == Action::Down) {
            scrollOffset_ = std::max(0, scrollOffset_ - 1);
        }
    }
}

void TerminalScreen::draw(IRenderer& renderer) {
    renderer.drawRect(Rect{0, 0, 1280, 720}, kBgDark);
    renderer.drawRect(Rect{40, 40, 1200, 640}, kCard);
    renderer.drawRect(Rect{40, 40, 1200, 4}, kAccent);

    if (titleFont_) {
        titleFont_->draw(renderer, "TERMINAL", {64, 56}, kAccent);
    }
    if (bodyFont_) {
        bodyFont_->draw(renderer, cwd_, {280, 64}, kTextDim);
    }

    const int total = static_cast<int>(lines_.size());
    const int end = total - scrollOffset_;
    const int start = std::max(0, end - kVisibleLines);

    float y = 110.0f;
    if (bodyFont_) {
        for (int i = start; i < end; ++i) {
            bodyFont_->draw(renderer, lines_[static_cast<std::size_t>(i)], {64, y}, kTextBright);
            y += 28.0f;
        }

        const std::string prompt = "> " + input_ + "_";
        bodyFont_->draw(renderer, prompt, {64, 620}, kAccent);
        bodyFont_->draw(renderer, "ENTER RUN  ·  UP/DOWN SCROLL  ·  ESC EXIT", {64, 660},
                        kTextDim);
    }
}

}  // namespace cyberdeck
