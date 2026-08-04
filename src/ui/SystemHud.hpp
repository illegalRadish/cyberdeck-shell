#pragma once

#include "platform/SystemInfo.hpp"
#include "render/Font.hpp"
#include "system/SystemSampler.hpp"
#include "ui/Node.hpp"
#include "ui/Tween.hpp"

#include <string>
#include <vector>


namespace cyberdeck {

// Right-side / footer system dashboard strip.
class SystemHud final : public Node {
public:
    SystemHud(NodeId id, Font* font, SystemSampler* sampler);

    void update(float dt) override;
    void draw(IRenderer& renderer) override;

private:
    struct Row {
        std::string label;
        std::string value;
        float fill = 0.0f;  // 0-1 meter, <0 hides meter
    };

    void rebuildRows();

    Font* font_ = nullptr;
    SystemSampler* sampler_ = nullptr;
    std::vector<Row> rows_;
    std::vector<Tween> meters_;  // smoothed meter fill per row
    float refreshTimer_ = 0.0f;

};

}  // namespace cyberdeck
