#pragma once

namespace cyberdeck {

struct SystemInfo {
    float cpuPercent = 0.0f;
    float ramPercent = 0.0f;
    float storagePercent = 0.0f;
    float batteryPercent = -1.0f;  // -1 = unavailable
    bool wifiConnected = false;
};

}  // namespace cyberdeck
