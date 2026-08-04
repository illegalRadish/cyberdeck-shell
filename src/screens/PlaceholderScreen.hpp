#pragma once

#include "render/Font.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

#include <string>

namespace cyberdeck {

// Generic destination screen until media/system features land.
class PlaceholderScreen final : public Screen {
public:
    PlaceholderScreen(NodeId id, std::string title, std::string detail, Font* titleFont,
                      Font* bodyFont);

    void handleInput(const Input& input) override;

private:
    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
};

}  // namespace cyberdeck
