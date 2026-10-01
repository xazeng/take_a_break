#include "config.h"
#include "localization.h"
#include <shlobj.h>
#include <cwchar>
#include <vector>

namespace tab {
namespace {
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunName[] = L"TakeABreak";

bool ValidExecutablePath(const std::wstring& path) {
    const bool absolute = (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) ||
                          (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\');
    return absolute && path.find_first_of(L"\r\n\"") == std::wstring::npos;
}

unsigned ReadNumber(const std::wstring& path, const wchar_t* key, unsigned fallback,
                    unsigned minimum, unsigned maximum, bool& repaired) {
    wchar_t buffer[64]{};
    GetPrivateProfileStringW(L"General", key, L"", buffer, ARRAYSIZE(buffer), path.c_str());
    if (!buffer[0]) return fallback;
    wchar_t* end = nullptr;
    const auto number = wcstoul(buffer, &end, 10);
    if (end == buffer || *end || buffer[0] == L'-' || number < minimum || number > maximum) {
        repaired = true;
        return fallback;
    }
    return number;
}
}

void Log(const wchar_t* message, DWORD code) noexcept {
    wchar_t line[768]{};
    SYSTEMTIME now{};
    GetLocalTime(&now);
    swprintf_s(line, L"[TakeABreak %02u:%02u:%02u] %s (code=%lu)\n",
               now.wHour, now.wMinute, now.wSecond, message, code);
    OutputDebugStringW(line);
}

std::wstring ConfigPath() {
    PWSTR directory = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &directory))) return {};
    std::wstring result = std::wstring(directory) + L"\\TakeABreak\\settings.ini";
    CoTaskMemFree(directory);
    return result;
}

bool ConfigMissing(const std::wstring& path) {
    if (path.empty() || GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    const DWORD error = GetLastError();
    // 无访问权限等错误不能被误判为首次运行。
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

Config LoadConfig(const std::wstring& path, bool* repaired) {
    bool invalid = false;
    Config result;
    result.intervalMinutes = ReadNumber(path, L"IntervalMinutes", 60, 1, 1440, invalid);
    result.breakSeconds = ReadNumber(path, L"BreakSeconds", 10, 1, 3600, invalid);
    result.hourly = ReadNumber(path, L"Hourly", 0, 0, 1, invalid) != 0;
    std::vector<wchar_t> executable(32768);
    GetPrivateProfileStringW(L"General", L"ExecutablePath", L"", executable.data(),
                            static_cast<DWORD>(executable.size()), path.c_str());
    result.executablePath = executable.data();
    if (repaired) *repaired = invalid;
    return result;
}

bool SaveConfig(const std::wstring& path, const Config& config, std::wstring& error) {
    if (path.empty() || !ValidConfig(config)) { error = Tr(UiText::ConfigInvalid); return false; }
    const auto executable = config.executablePath.empty() ? ExecutablePath() : config.executablePath;
    if (!ValidExecutablePath(executable)) { error = Tr(UiText::ExecutablePathError); return false; }
    const auto split = path.find_last_of(L"\\/");
    if (split != std::wstring::npos && !CreateDirectoryW(path.substr(0, split).c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        error = Tr(UiText::ConfigDirectoryError); return false;
    }
    // 先写临时文件再替换，避免设置只写入一部分。
    const std::wstring temporary = path + L".tmp";
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = Tr(UiText::ConfigWriteError); return false; }
    const std::wstring contents = L"\xFEFF[General]\r\nVersion=1\r\nIntervalMinutes=" +
        std::to_wstring(config.intervalMinutes) + L"\r\nBreakSeconds=" + std::to_wstring(config.breakSeconds) +
        L"\r\nHourly=" + (config.hourly ? L"1" : L"0") + L"\r\nExecutablePath=" + executable + L"\r\n";
    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(contents.size() * sizeof(wchar_t));
    const bool success = WriteFile(file, contents.data(), size, &written, nullptr) && written == size && FlushFileBuffers(file);
    CloseHandle(file);
    if (!success || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); error = Tr(UiText::ConfigSaveError); return false;
    }
    // 清除 Win32 INI 映射缓存，以便随后读取新文件。
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    Log(L"配置已保存");
    return true;
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return length && length < path.size() ? std::wstring(path.data(), length) : std::wstring{};
}

bool ReadStartupCommand(std::wstring& command, std::wstring& error) {
    wchar_t value[32768]{};
    DWORD size = sizeof(value);
    const auto result = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunName, RRF_RT_REG_SZ, nullptr, value, &size);
    command.clear();
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return true;
    if (result != ERROR_SUCCESS) { error = Tr(UiText::StartupError) + std::to_wstring(result); return false; }
    command = value;
    return true;
}

bool AutoStartEnabled() {
    std::wstring command, error;
    return ReadStartupCommand(command, error) && !command.empty();
}

bool WriteStartupCommand(const std::wstring& command, std::wstring& error) {
    HKEY key = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
        if (!command.empty()) {
            result = RegSetValueExW(key, kRunName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                   static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        } else {
            result = RegDeleteValueW(key, kRunName);
            if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
        }
        RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) { error = Tr(UiText::StartupError) + std::to_wstring(result); return false; }
    Log(command.empty() ? L"已移除登录自启动" : L"已更新登录自启动");
    return true;
}

bool SetAutoStart(bool enabled, std::wstring& error) {
    if (!enabled) return WriteStartupCommand(L"", error);
    const auto executable = ExecutablePath();
    if (!ValidExecutablePath(executable)) { error = Tr(UiText::ExecutablePathError); return false; }
    return WriteStartupCommand(L"\"" + executable + L"\"", error);
}

bool SyncExecutableLocation(const std::wstring& path, Config& config, const std::wstring& executable,
                            std::wstring& error, StartupAccess startup) {
    if (path.empty() || !ValidExecutablePath(executable) || !startup.read || !startup.write) {
        error = Tr(UiText::ExecutablePathError); return false;
    }
    if (ConfigMissing(path)) return true;
    std::wstring command;
    if (!startup.read(command, error)) return false;
    const auto expectedCommand = L"\"" + executable + L"\"";
    // 仅修复已存在的启动项，不重新启用用户已取消的自启动。
    if (!command.empty() && _wcsicmp(command.c_str(), expectedCommand.c_str()) != 0) {
        if (!startup.write(expectedCommand, error)) return false;
    }
    if (_wcsicmp(config.executablePath.c_str(), executable.c_str()) != 0) {
        Config next = config;
        next.executablePath = executable;
        if (!SaveConfig(path, next, error)) return false;
        config = std::move(next);
        Log(L"已记录当前 EXE 绝对路径");
    }
    return true;
}
}
