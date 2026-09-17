#pragma once
#include <windows.h>
#include <cstddef>

namespace tab {
enum class Language { Chinese, English };

// 使用用户的 Windows 界面语言，所有中文变体统一使用简体中文。
constexpr Language LanguageFor(LANGID language) noexcept {
    return PRIMARYLANGID(language) == LANG_CHINESE ? Language::Chinese : Language::English;
}
inline Language uiLanguage = Language::English;
inline void InitializeLanguage() noexcept { uiLanguage = LanguageFor(GetUserDefaultUILanguage()); }

enum class UiText {
    AppName, SettingsTitle, Paused, Resting, Timing, Waiting,
    TimerError, CountdownError, SettingsError, BreakNotice,
    RestNow, Resume, Pause, Settings, AutoStart, RemoveStartup, Quit,
    InvalidDuration, SettingsSavedBut, SecondsRemaining, ReturnAfterBreak,
    SessionError, TrayError, InvalidConfig,
    SettingsIntro, IntervalLabel, DurationLabel, HourlyLabel, AutoStartLabel, Save, Cancel,
    ConfigInvalid, ConfigDirectoryError, ConfigWriteError, ConfigSaveError,
    ExecutablePathError, StartupError, ExistingInstanceError, Count
};

struct Translation { const wchar_t* chinese; const wchar_t* english; };
inline constexpr Translation translations[] = {
    {L"起来歇会儿", L"Take a Break"},
    {L"起来歇会儿 · 设置", L"Take a Break - Settings"},
    {L"提醒已暂停", L"Reminders paused"},
    {L"正在休息", L"Taking a break"},
    {L"计时中", L"Timer running"},
    {L"等待登录或解锁", L"Waiting for sign-in or unlock"},
    {L"无法创建提醒定时器，请退出并重新运行。当前不会触发提醒。", L"Unable to start the reminder timer. Please exit and restart the app. Reminders are currently unavailable."},
    {L"倒计时初始化失败，本次休息已取消。", L"Unable to start the countdown. This break has been cancelled."},
    {L"无法创建设置窗口。", L"Unable to open Settings."},
    {L"离开座位，伸展一下，让眼睛看看远处。", L"Stand up, stretch, and give your eyes a distant view."},
    {L"立即休息", L"Take a break now"},
    {L"恢复提醒", L"Resume reminders"},
    {L"暂停提醒", L"Pause reminders"},
    {L"设置…", L"Settings..."},
    {L"登录时自动运行", L"Run at sign-in"},
    {L"移除自动启动并退出", L"Disable auto-start and exit"},
    {L"退出", L"Exit"},
    {L"提醒间隔必须为 1～1440 分钟，休息时长必须为 1～3600 秒。", L"Reminder interval must be 1-1440 minutes; break duration must be 1-3600 seconds."},
    {L"提醒设置已保存，但", L"Reminder settings were saved, but: "},
    {L"秒后结束休息", L"seconds remaining"},
    {L"休息结束后自动返回", L"Your desktop returns when the break ends"},
    {L"无法监听登录和锁屏状态，程序将退出。请稍后重试。", L"Unable to monitor sign-in and lock status. The app will exit. Please try again later."},
    {L"无法创建托盘图标，程序将退出。请在桌面加载完成后重试。", L"Unable to create the tray icon. The app will exit. Please retry after the desktop has loaded."},
    {L"部分配置值无效，已使用默认值。请在设置中检查并保存。", L"Some settings were invalid and have been reset to defaults. Please review and save them in Settings."},
    {L"登录或解锁后重新计时；锁屏期间不提醒。", L"Timer resets at sign-in or unlock; no reminders while locked."},
    {L"提醒间隔（分钟）", L"Interval (minutes)"},
    {L"休息时长（秒）", L"Break duration (seconds)"},
    {L"改为每个整点提醒", L"Remind on the hour instead"},
    {L"登录时自动运行（当前用户）", L"Run at sign-in (current user)"},
    {L"保存", L"Save"},
    {L"取消", L"Cancel"},
    {L"配置路径或数值无效。", L"The settings path or values are invalid."},
    {L"无法创建配置目录。", L"Unable to create the settings folder."},
    {L"无法写入配置文件。", L"Unable to write the settings file."},
    {L"保存配置失败，请检查目录权限。", L"Unable to save settings. Please check folder permissions."},
    {L"无法获取程序路径。", L"Unable to determine the executable path."},
    {L"修改登录启动项失败，错误码：", L"Unable to change sign-in startup. Error code: "},
    {L"程序已在运行，但暂时无法打开设置。请通过已有实例的托盘图标操作。", L"The app is already running, but Settings could not be opened. Please use the existing tray icon."}
};
static_assert(ARRAYSIZE(translations) == static_cast<size_t>(UiText::Count));

inline const wchar_t* Tr(UiText id) noexcept {
    const auto index = static_cast<size_t>(id);
    if (index >= ARRAYSIZE(translations)) return L"";
    return uiLanguage == Language::Chinese ? translations[index].chinese : translations[index].english;
}
}
