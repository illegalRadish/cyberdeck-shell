#include "ui/SystemHud.hpp"

#include <algorithm>
#include <cstdio>

namespace cyberdeck {

SystemHud::SystemHud(NodeId id, Font* font, SystemSampler* sampler)
    : Node(id, "SystemHud"), font_(font), sampler_(sampler) {
    setFill(Color::fromBytes(9, 20, 12));
    setFocusable(false);
    rebuildRows();
}

void SystemHud::rebuildRows() {
    rows_.clear();
    if (!sampler_) {
        meters_.clear();
        return;
    }

    const SystemInfo& info = sampler_->info();
    char buf[32];

    std::snprintf(buf, sizeof(buf), "%.0f%%", info.cpuPercent);
    rows_.push_back({"CPU", buf, info.cpuPercent / 100.0f});

    std::snprintf(buf, sizeof(buf), "%.0f%%", info.ramPercent);
    rows_.push_back({"RAM", buf, info.ramPercent / 100.0f});

    std::snprintf(buf, sizeof(buf), "%.0f%%", info.storagePercent);
    rows_.push_back({"DISK", buf, info.storagePercent / 100.0f});

    rows_.push_back({"NET", sampler_->wifiString(), -1.0f});
    rows_.push_back({"PWR", sampler_->batteryString(), -1.0f});
    rows_.push_back({"TIME", sampler_->clockString(), -1.0f});

    if (meters_.size() != rows_.size()) {
        meters_.assign(rows_.size(), Tween{0.0f, 0.0f, 0.4f, Ease::OutCubic});
    }
}

void SystemHud::update(float dt) {
    refreshTimer_ += dt;
    if (refreshTimer_ >= 0.25f) {
        refreshTimer_ = 0.0f;
        rebuildRows();
    }
    // Smooth the meter fills toward their latest sampled targets.
    const std::size_t n = std::min(rows_.size(), meters_.size());
    for (std::size_t i = 0; i < n; ++i) {
        const float target = std::clamp(rows_[i].fill, 0.0f, 1.0f);
        meters_[i].chase(target, 6.0f, dt);
    }
    Node::update(dt);
}

void SystemHud::draw(IRenderer& renderer) {
    if (!visible_ || drawOpacity() <= 0.0f) {
        return;
    }

    const Rect panel = scaledBounds();
    const float radius = 10.0f;

    // Rounded panel.
    renderer.drawRoundedRect(panel, modulate(fill_), radius);

    // Accent header strip with rounded top corners.
    renderer.drawRoundedRect(Rect{panel.x, panel.y, panel.w, 4.0f}, modulate(kAccent), radius);

    if (!font_) {
        return;
    }

    float y = panel.y + 30.0f;
    const float left = panel.x + 24.0f;
    const float meterX = panel.x + 24.0f;
    const float meterW = panel.w - 48.0f;

    font_->draw(renderer, "SYSTEM", {left, y}, modulate(kAccent));
    y += 42.0f;

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i];
        font_->draw(renderer, row.label, {left, y}, modulate(kTextDim));
        const Vec2 valueSize = font_->measure(row.value);
        font_->draw(renderer, row.value,
                    {panel.x + panel.w - 24.0f - valueSize.x, y},
                    modulate(kTextBright));
        y += 30.0f;

        if (row.fill >= 0.0f) {
            const float trackH = 8.0f;
            const float trackR = trackH * 0.5f;
            // Track.
            renderer.drawRoundedRect(Rect{meterX, y, meterW, trackH},
                                     modulate(Color::fromBytes(10, 26, 14)), trackR);
            const float fill = (i < meters_.size())
                                   ? std::clamp(meters_[i].value(), 0.0f, 1.0f)
                                   : std::clamp(row.fill, 0.0f, 1.0f);
            const float filled = meterW * fill;
            if (filled > 1.0f) {
                renderer.drawRoundedRect(Rect{meterX, y, filled, trackH},
                                         modulate(Color{kAccent.r, kAccent.g, kAccent.b, 0.85f}),
                                         trackR);
            }
            y += 24.0f;
        } else {
            y += 10.0f;
        }
    }
}

}  // namespace cyberdeck
