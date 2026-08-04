#pragma once

#include "ai/AiAssets.hpp"
#include "media/MediaLibrary.hpp"
#include "render/Font.hpp"
#include "system/SystemSampler.hpp"
#include "ui/Screen.hpp"
#include "ui/ScreenManager.hpp"

#include <string>
#include <vector>

namespace cyberdeck {

class SettingsScreen final : public Screen {
public:
    SettingsScreen(Font* titleFont, Font* bodyFont, MediaLibrary* library,
                   SystemSampler* sampler, ScreenManager* navigator, AiAssets* ai);

    void onEnter() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    enum class Item {
        MediaRoot,
        Rescan,
        OpenLibrary,
        AiAssetsItem,
        Network,
        About,
        QuitApp,
        Count,
    };

    void activate(Item item);
    std::string labelFor(Item item) const;
    std::string detailFor(Item item) const;
    void openPath(const std::string& path);

    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    MediaLibrary* library_ = nullptr;
    SystemSampler* sampler_ = nullptr;
    ScreenManager* navigator_ = nullptr;
    AiAssets* ai_ = nullptr;
    int focusIndex_ = 0;
    std::string flashMessage_;
    float flashTimer_ = 0.0f;
};

}  // namespace cyberdeck
