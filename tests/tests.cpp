#include "config.h"
#include <iostream>
#include <string>

namespace {
int failures = 0;
void Check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
}

int wmain() {
    using namespace tab;
    Config config;
    SYSTEMTIME time{};
    time.wMinute = 59; time.wSecond = 59; time.wMilliseconds = 999;
    Check(ReminderDelay(config, time) == 3600000, "interval ignores wall-clock boundary");
    config.hourly = true;
    Check(ReminderDelay(config, time) == 1, "next hour one millisecond away");
    time = {};
    Check(ReminderDelay(config, time) == 3600000, "exact hour selects next hour");
    Check(RemainingSeconds(10001, 1) == 10, "countdown starts at configured duration");
    Check(RemainingSeconds(10001, 10000) == 1, "countdown rounds up");
    Check(RemainingSeconds(10001, 10001) == 0, "countdown ends at deadline");
    Check(RemainingSeconds(10001, 20000) == 0, "late wake has no underflow");
    SessionState session;
    Check(!session.CanRemind(), "unavailable session cannot remind");
    session.available = true;
    Check(session.CanRemind(), "unlocked session can remind");
    session.paused = true;
    session.available = false; session.available = true;
    Check(!session.CanRemind(), "unlock preserves manual pause");

    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(ARRAYSIZE(temporary), temporary)) return 2;
    const auto directory = std::wstring(temporary) + L"TakeABreakTest-" + std::to_wstring(GetCurrentProcessId());
    const auto path = directory + L"\\settings.ini";
    Check(ConfigMissing(path), "missing configuration marks first run");
    Check(!ConfigMissing(L""), "invalid configuration path is not first run");
    bool repaired = false;
    auto loaded = LoadConfig(path, &repaired);
    Check(loaded.intervalMinutes == 60 && loaded.breakSeconds == 10 && !repaired, "missing config defaults");
    std::wstring error;
    config.intervalMinutes = 37; config.breakSeconds = 12;
    Check(SaveConfig(path, config, error), "save unicode INI");
    Check(!ConfigMissing(path), "saved configuration prevents first run prompt");
    loaded = LoadConfig(path);
    Check(loaded.intervalMinutes == 37 && loaded.breakSeconds == 12 && loaded.hourly, "round trip INI");
    // 旧字段不影响其他设置，保存时不再写回。
    WritePrivateProfileStringW(L"General", L"RestrictInput", L"1", path.c_str());
    loaded = LoadConfig(path, &repaired);
    Check(!repaired && loaded.intervalMinutes == 37 && loaded.breakSeconds == 12 && loaded.hourly, "legacy restriction ignored");
    Check(SaveConfig(path, loaded, error), "save legacy configuration");
    wchar_t legacyValue[16]{};
    GetPrivateProfileStringW(L"General", L"RestrictInput", L"missing", legacyValue, ARRAYSIZE(legacyValue), path.c_str());
    Check(std::wstring(legacyValue) == L"missing", "obsolete field removed on save");
    WritePrivateProfileStringW(L"General", L"IntervalMinutes", L"-1", path.c_str());
    WritePrivateProfileStringW(L"General", L"BreakSeconds", L"9999999999999999999999999", path.c_str());
    WritePrivateProfileStringW(L"General", L"Hourly", L"1garbage", path.c_str());
    loaded = LoadConfig(path, &repaired);
    Check(repaired && loaded.intervalMinutes == 60 && loaded.breakSeconds == 10 && !loaded.hourly, "malformed fields safely default");
    config.breakSeconds = 0;
    Check(!SaveConfig(path, config, error), "reject zero duration");
    config.breakSeconds = 3601;
    Check(!ValidConfig(config), "reject excessive duration");

    DeleteFileW(path.c_str());
    RemoveDirectoryW(directory.c_str());
    std::cout << (failures ? "FAILED" : "PASS") << ": scheduling, session policy, countdown, INI compatibility\n";
    return failures ? 1 : 0;
}
