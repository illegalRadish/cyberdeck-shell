#pragma once

#include "render/Font.hpp"
#include "ui/Screen.hpp"

#include <string>
#include <vector>

namespace cyberdeck {

class TerminalScreen final : public Screen {
public:
    TerminalScreen(Font* titleFont, Font* bodyFont);
    ~TerminalScreen() override;

    void onEnter() override;
    void onExit() override;
    void handleInput(const Input& input) override;
    void draw(IRenderer& renderer) override;
    bool wantsTextInput() const override { return true; }

private:
    void appendLine(std::string line);
    void runCommand(const std::string& command);
    void clampScrollback();

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    std::vector<std::string> lines_;
    std::string input_;
    std::string cwd_;
    int scrollOffset_ = 0;
};

}  // namespace cyberdeck
