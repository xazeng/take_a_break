#include "config.h"
#include "localization.h"
#include <iostream>
#include <string>

namespace {
int failures = 0;
std::wstring fakeStartupCommand;
bool fakeReadFails = false, fakeWriteFails = false;
unsigned fakeWrites = 0;
bool ReadFakeStartup(std::wstring& command, std::wstring& error) {
    if (fakeReadFails) { error = L"read failure"; return false; }
    command = fakeStartupCommand;
    return true;
}
bool WriteFakeStartup(const std::wstring& command, std::wstring& error) {
    ++fakeWrites;
    if (fakeWriteFails) { error = L"write failure"; return false; }
    fakeStartupCommand = command;
    return true;
}
void Check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
}

int wmain() {
    using namespace tab;
    Check(LanguageFor(0x0804) == Language::Chinese, "Simplified Chinese UI language");
    Check(LanguageFor(0x0404) == Language::Chinese, "Traditional Chinese uses Simplified Chinese");
    Check(LanguageFor(0x0C04) == Language::Chinese, "Hong Kong Chinese uses Simplified Chinese");
    Check(LanguageFor(0x0409) == Language::English, "English UI language");
    Check(LanguageFor(0x0411) == Language::English && LanguageFor(0) == Language::English, "unsupported or unknown language falls back to English");
    for (const auto& translation : translations) {
        Check(translation.chinese && *translation.chinese && translation.english && *translation.english, "both translations exist");
    }
    std::wstring localizedError;
    uiLanguage = Language::Chinese;
    Check(!SaveConfig(L"", Config{}, localizedError) && localizedError == L"配置路径或数值无效。", "Chinese configuration error");
    uiLanguage = Language::English;
    Check(!SaveConfig(L"", Config{}, localizedError) && localizedError == L"The settings path or values are invalid.", "English configuration error");
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
    Check(loaded.executablePath == ExecutablePath(), "save records absolute executable location");
    Check(loaded.intervalMinutes == 37 && loaded.breakSeconds == 12 && loaded.hourly, "round trip INI");
    // 旧字段不影响其他设置，保存时不再写回。
    WritePrivateProfileStringW(L"General", L"RestrictInput", L"1", path.c_str());
    loaded = LoadConfig(path, &repaired);
    Check(!repaired && loaded.intervalMinutes == 37 && loaded.breakSeconds == 12 && loaded.hourly, "legacy restriction ignored");
    Check(SaveConfig(path, loaded, error), "save legacy configuration");
    wchar_t legacyValue[16]{};
    GetPrivateProfileStringW(L"General", L"RestrictInput", L"missing", legacyValue, ARRAYSIZE(legacyValue), path.c_str());
    Check(std::wstring(legacyValue) == L"missing", "obsolete field removed on save");
    const StartupAccess fakeStartup{ReadFakeStartup, WriteFakeStartup};
    const std::wstring newExecutable = L"C:\\New Folder\\TakeABreak.exe";
    // 老 INI 缺少路径字段，但有自启动项时迁移；不访问真实注册表。
    WritePrivateProfileStringW(L"General", L"ExecutablePath", nullptr, path.c_str());
    loaded = LoadConfig(path);
    fakeStartupCommand = L"\"C:\\Old\\TakeABreak.exe\"";
    Check(SyncExecutableLocation(path, loaded, newExecutable, error, fakeStartup), "legacy path migration");
    Check(fakeWrites == 1 && fakeStartupCommand == L"\"" + newExecutable + L"\"", "registered command moves and quotes spaces");
    auto migrated = LoadConfig(path);
    Check(migrated.executablePath == newExecutable && migrated.intervalMinutes == 37 && migrated.breakSeconds == 12 && migrated.hourly,
          "migration persists location and preserves settings");
    Check(SyncExecutableLocation(path, loaded, newExecutable, error, fakeStartup) && fakeWrites == 1, "unchanged location does not rewrite startup");
    fakeStartupCommand.clear();
    Check(SyncExecutableLocation(path, loaded, L"D:\\Moved\\TakeABreak.exe", error, fakeStartup) && fakeWrites == 1 && fakeStartupCommand.empty(),
          "moving does not enable disabled auto-start");
    fakeStartupCommand = L"\"C:\\Old\\TakeABreak.exe\"";
    const auto lastLocation = loaded.executablePath;
    fakeWriteFails = true;
    Check(!SyncExecutableLocation(path, loaded, newExecutable, error, fakeStartup), "startup write failure reported");
    Check(loaded.executablePath == lastLocation && LoadConfig(path).executablePath == lastLocation, "failed migration keeps old location for retry");
    fakeWriteFails = false;
    fakeReadFails = true;
    Check(!SyncExecutableLocation(path, loaded, newExecutable, error, fakeStartup), "startup read failure reported");
    fakeReadFails = false;
    Check(SyncExecutableLocation(path, loaded, newExecutable, error, fakeStartup), "failed migration retries successfully");
    const auto missingPath = directory + L"\\first-run.ini";
    Check(SyncExecutableLocation(missingPath, loaded, newExecutable, error, fakeStartup) && ConfigMissing(missingPath),
          "path tracking does not create first-run configuration");
    Check(!SyncExecutableLocation(path, loaded, L"relative.exe", error, fakeStartup), "reject relative executable location");
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
