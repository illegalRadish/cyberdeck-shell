#pragma once

#include "media/MediaLibrary.hpp"
#include "media/MediaTypes.hpp"
#include "render/Font.hpp"
#include "render/Texture.hpp"
#include "ui/Screen.hpp"

#include <string>
#include <vector>

namespace cyberdeck {

class AsciiSpinner;

class LibraryBrowserScreen final : public Screen {
public:
    LibraryBrowserScreen(NodeId id, std::string title, MediaType type, Font* titleFont,
                         Font* bodyFont, MediaLibrary* library, bool showAll = false);

    void onEnter() override;
    void handleInput(const Input& input) override;
    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    void rebuildList();
    void loadFocusedThumb();

    MediaType type_;
    bool showAll_ = false;
    Font* titleFont_ = nullptr;
    Font* bodyFont_ = nullptr;
    MediaLibrary* library_ = nullptr;
    AsciiSpinner* spinner_ = nullptr;
    std::vector<MediaItem> items_;
    int focusIndex_ = 0;
    std::string status_;
    Texture thumb_;
    std::string thumbSource_;
    float refreshTimer_ = 0.0f;
};

}  // namespace cyberdeck
