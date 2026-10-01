#pragma once
#include "model.h"
#include <string>

namespace tab {
std::wstring ConfigPath();
bool ConfigMissing(const std::wstring& path);
Config LoadConfig(const std::wstring& path, bool* repaired = nullptr);
bool SaveConfig(const std::wstring& path, const Config& config, std::wstring& error);
std::wstring ExecutablePath();
bool AutoStartEnabled();
bool SetAutoStart(bool enabled, std::wstring& error);
struct StartupAccess {
    bool (*read)(std::wstring& command, std::wstring& error);
    bool (*write)(const std::wstring& command, std::wstring& error);
};
bool ReadStartupCommand(std::wstring& command, std::wstring& error);
bool WriteStartupCommand(const std::wstring& command, std::wstring& error);
// 仅迁移已有配置；首次运行仍在用户保存后才创建 INI。
bool SyncExecutableLocation(const std::wstring& path, Config& config, const std::wstring& executable,
                            std::wstring& error,
                            StartupAccess startup = {ReadStartupCommand, WriteStartupCommand});
void Log(const wchar_t* message, DWORD code = ERROR_SUCCESS) noexcept;
}
