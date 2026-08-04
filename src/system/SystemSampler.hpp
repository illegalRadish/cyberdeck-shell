#pragma once

#include "platform/SystemInfo.hpp"

#include <string>

namespace cyberdeck {

class SystemSampler {
public:
    void update(float dt);

    const SystemInfo& info() const { return info_; }
    std::string clockString() const;
    std::string wifiString() const;
    std::string batteryString() const;

private:
    void sampleHost();

    SystemInfo info_{};
    float sampleTimer_ = 0.0f;
    float pulse_ = 0.0f;
};

}  // namespace cyberdeck
