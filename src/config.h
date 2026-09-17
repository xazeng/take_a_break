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
void Log(const wchar_t* message, DWORD code = ERROR_SUCCESS) noexcept;
}
