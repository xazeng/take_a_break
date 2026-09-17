#pragma once
#include <windows.h>
#include <cstdint>

namespace tab {
struct Config {
    unsigned intervalMinutes = 60;
    unsigned breakSeconds = 10;
    bool hourly = false;
};

inline bool ValidConfig(const Config& value) noexcept {
    return value.intervalMinutes >= 1 && value.intervalMinutes <= 1440 &&
           value.breakSeconds >= 1 && value.breakSeconds <= 3600;
}

// 整点模式严格选择下一个整点；间隔模式从当前时刻重新计时。
inline DWORD ReminderDelay(const Config& value, const SYSTEMTIME& local) noexcept {
    if (!value.hourly) return value.intervalMinutes * 60'000U;
    return 3'600'000U - (local.wMinute * 60'000U + local.wSecond * 1'000U + local.wMilliseconds);
}

inline unsigned RemainingSeconds(ULONGLONG deadline, ULONGLONG now) noexcept {
    return now >= deadline ? 0U : static_cast<unsigned>((deadline - now + 999U) / 1000U);
}

struct SessionState {
    bool available = false;
    bool paused = false;
    bool CanRemind() const noexcept { return available && !paused; }
};
}
