#pragma once

#include "ui/Screen.hpp"

#include "render/Font.hpp"
#include "render/Texture.hpp"

#include <string>

namespace cyberdeck {

class DemoScreen final : public Screen {
public:
    DemoScreen(Font* titleFont, Font* bodyFont);

    void onEnter() override;
    void handleInput(const Input& input) override;

private:
    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    Texture demoIcon_;
    std::string statusText_ = "Arrow keys move focus  ·  Enter selects  ·  Esc quits";
};

}  // namespace cyberdeck
