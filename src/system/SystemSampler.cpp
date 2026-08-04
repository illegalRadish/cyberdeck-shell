#include "system/SystemSampler.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstdlib>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace cyberdeck {

void SystemSampler::update(float dt) {
    pulse_ += dt;
    sampleTimer_ += dt;
    if (sampleTimer_ >= 0.5f) {
        sampleTimer_ = 0.0f;
        sampleHost();
    }
}

std::string SystemSampler::clockString() const {
    using clock = std::chrono::system_clock;
    const std::time_t now = clock::to_time_t(clock::now());
    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", local.tm_hour, local.tm_min);
    return buf;
}

std::string SystemSampler::wifiString() const {
    return info_.wifiConnected ? "WiFi · OK" : "WiFi · --";
}

std::string SystemSampler::batteryString() const {
    if (info_.batteryPercent < 0.0f) {
        return "BAT · AC";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "BAT · %.0f%%", info_.batteryPercent);
    return buf;
}

void SystemSampler::sampleHost() {
#if defined(__APPLE__)
    // RAM
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_statistics64_data_t vmstat;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vmstat), &count) == KERN_SUCCESS) {
        const natural_t pageSize = static_cast<natural_t>(vm_kernel_page_size);
        const uint64_t used =
            (static_cast<uint64_t>(vmstat.active_count) + vmstat.inactive_count +
             vmstat.wire_count) *
            pageSize;
        int64_t memSize = 0;
        size_t len = sizeof(memSize);
        if (sysctlbyname("hw.memsize", &memSize, &len, nullptr, 0) == 0 && memSize > 0) {
            info_.ramPercent = 100.0f * static_cast<float>(used) / static_cast<float>(memSize);
        }
    }

    // CPU stub-ish: load average scaled into a readable percent.
    double loads[3] = {0, 0, 0};
    if (getloadavg(loads, 3) == 3) {
        info_.cpuPercent = std::clamp(static_cast<float>(loads[0]) * 25.0f, 0.0f, 100.0f);
    }

    info_.wifiConnected = true;
    info_.batteryPercent = -1.0f;
#else
    // Placeholder animated metrics for non-Apple hosts until Pi metrics land.
    info_.cpuPercent = 28.0f + 12.0f * std::sin(pulse_ * 0.7f);
    info_.ramPercent = 45.0f + 8.0f * std::sin(pulse_ * 0.45f + 1.0f);
    info_.wifiConnected = true;
    info_.batteryPercent = -1.0f;
#endif

    // Storage remains a stub until media root mounts exist.
    info_.storagePercent = 62.0f + 3.0f * std::sin(pulse_ * 0.2f);
    info_.cpuPercent = std::clamp(info_.cpuPercent, 0.0f, 100.0f);
    info_.ramPercent = std::clamp(info_.ramPercent, 0.0f, 100.0f);
    info_.storagePercent = std::clamp(info_.storagePercent, 0.0f, 100.0f);
}

}  // namespace cyberdeck
