#pragma once

#include "ai/AiAssets.hpp"
#include "render/Font.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

#include <string>

namespace cyberdeck {

// Per-asset presence, byte progress, and the download/verify actions.
// The worker lives in AiAssets (owned by Application), so leaving this screen
// does not interrupt a transfer.
class AiAssetsScreen final : public Screen {
public:
    AiAssetsScreen(Font* titleFont, Font* bodyFont, AiAssets* ai, ScreenManager* navigator);

    void onEnter() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    enum class Action_ { Download, Verify, Count };

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    AiAssets* ai_ = nullptr;
    ScreenManager* navigator_ = nullptr;

    AiAssetsStatus status_{};
    int focusIndex_ = 0;
    float refreshTimer_ = 0.0f;
    std::string flashMessage_;
    float flashTimer_ = 0.0f;
};

}  // namespace cyberdeck
