#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <string>
#include <cwchar>
#include <exception>
#include <vector>
#include "config.h"
#include "resource.h"

namespace {
using namespace tab;
constexpr wchar_t kControllerClass[] = L"TakeABreak.Controller";
constexpr wchar_t kBreakClass[] = L"TakeABreak.Reminder";
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_REFIT_BREAK = WM_APP + 2;
constexpr UINT_PTR kReminderTimer = 1, kDisplayTimer = 2, kSmokeTimer = 3;
enum Command : UINT { RestNow = 2001, Pause, Settings, AutoStart, RemoveStartup, Quit };

struct App {
    HINSTANCE instance = nullptr;
    HICON iconSmall = nullptr;
    HICON iconLarge = nullptr;
    HWND controller = nullptr;
    HWND reminder = nullptr;
    std::vector<HWND> reminders;
    HWND settings = nullptr;
    HANDLE mutex = nullptr;
    Config config;
    SessionState session;
    std::wstring configPath;
    std::wstring notice;
    ULONGLONG deadline = 0;
    ULONGLONG reminderDue = 0;
    UINT taskbarCreated = 0;
    bool trayAdded = false;
    bool registeredSession = false;
    bool smoke = false;
    bool preview = false;
    bool firstRun = false;
    bool initialSettingsPending = false;
    bool shuttingDown = false;
    unsigned smokePhase = 0;
    unsigned smokeTicks = 0;
    int exitCode = 0;
} app;

LRESULT CALLBACK ControllerProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK BreakProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK SettingsProc(HWND, UINT, WPARAM, LPARAM);
void EndBreak();
void Schedule();
void OpenInitialSettings();

void Error(const std::wstring& message, HWND owner = nullptr) {
    MessageBoxW(owner ? owner : app.controller, message.c_str(), L"起来歇会儿", MB_OK | MB_ICONWARNING);
}

void ShowOwnedWindow(HWND window) {
    ShowWindow(window, SW_SHOWNORMAL);
    // 登录启动器可能传入 SW_HIDE/SW_SHOWMINIMIZED，第一次 ShowWindow 会服从它。
    if (!IsWindowVisible(window) || IsIconic(window)) ShowWindow(window, SW_SHOWNORMAL);
}

// 同时核对连接状态和锁定状态；查询失败时不弹窗。
bool SessionReady() {
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) return false;
    LPWSTR buffer = nullptr;
    DWORD bytes = 0;
    bool active = false;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sessionId, WTSSessionInfoEx, &buffer, &bytes)) {
        const auto info = reinterpret_cast<WTSINFOEXW*>(buffer);
        active = bytes >= sizeof(WTSINFOEXW) && info->Level == 1 &&
                 info->Data.WTSInfoExLevel1.SessionState == WTSActive &&
                 info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_UNLOCK;
        WTSFreeMemory(buffer);
    }
    return active;
}

bool DesktopReady() {
    const HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return false;
    wchar_t name[128]{};
    DWORD needed = 0;
    const bool ready = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed) &&
                       _wcsicmp(name, L"Default") == 0;
    CloseDesktop(desktop);
    return ready;
}

void UpdateTray(bool add = false) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = app.controller;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = WM_TRAY;
    data.hIcon = app.iconSmall;
    const wchar_t* status = app.session.paused ? L"提醒已暂停" :
                            app.reminder ? L"正在休息" : app.session.available ? L"计时中" : L"等待登录或解锁";
    swprintf_s(data.szTip, L"起来歇会儿 · %s", status);
    if (Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data)) {
        app.trayAdded = true;
        if (add) { data.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &data); }
    } else {
        Log(L"更新托盘图标失败", GetLastError());
    }
}

void Balloon(const wchar_t* message) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = app.controller;
    data.uID = 1;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_WARNING;
    wcscpy_s(data.szInfoTitle, L"起来歇会儿");
    wcsncpy_s(data.szInfo, message, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void Schedule() {
    KillTimer(app.controller, kReminderTimer);
    app.reminderDue = 0;
    if (app.shuttingDown || app.smoke || !app.session.CanRemind() || app.reminder) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    const DWORD delay = ReminderDelay(app.config, now);
    app.reminderDue = GetTickCount64() + delay;
    if (!SetTimer(app.controller, kReminderTimer, delay, nullptr)) {
        app.reminderDue = 0;
        Log(L"创建提醒定时器失败", GetLastError());
        Balloon(L"无法创建提醒定时器，请退出并重新运行。当前不会触发提醒。");
    }
}

void SetAvailable(bool available) {
    app.session.available = available;
    // 锁屏、挂起、切换会话都丢弃旧周期，恢复后从头开始。
    EndBreak();
    Schedule();
    UpdateTray();
    Log(available ? L"会话可用，重置提醒周期" : L"会话不可用，停止提醒");
    OpenInitialSettings();
}

void EndBreak() {
    KillTimer(app.controller, kDisplayTimer);
    app.reminder = nullptr;
    if (!app.reminders.empty()) {
        for (HWND window : app.reminders) DestroyWindow(window);
        app.reminders.clear();
        Log(L"休息结束，窗口资源已释放");
    }
    if (!app.shuttingDown) { Schedule(); UpdateTray(); }
}

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) return FALSE;
    try {
        reinterpret_cast<std::vector<RECT>*>(context)->push_back(info.rcMonitor);
    } catch (...) { return FALSE; }
    return TRUE;
}

bool CreateBreakWindows() {
    std::vector<RECT> monitors;
    if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors)) || monitors.empty()) return false;
    std::vector<HWND> windows;
    windows.reserve(monitors.size());
    // 使用完整显示器矩形而非工作区，覆盖任务栏；先准备全部窗口再替换旧窗口。
    for (const RECT& bounds : monitors) {
        HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kBreakClass, L"起来歇会儿",
            WS_POPUP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            app.controller, nullptr, app.instance, nullptr);
        if (!window) {
            for (HWND created : windows) DestroyWindow(created);
            return false;
        }
        windows.push_back(window);
    }
    for (size_t index = 0; index < windows.size(); ++index) {
        const auto& bounds = monitors[index];
        ShowOwnedWindow(windows[index]);
        const BOOL placed = SetWindowPos(windows[index], HWND_TOPMOST, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (!placed || !IsWindowVisible(windows[index]) || IsIconic(windows[index])) {
            for (HWND created : windows) DestroyWindow(created);
            return false;
        }
        UpdateWindow(windows[index]);
    }
    app.reminder = nullptr;
    for (HWND previous : app.reminders) DestroyWindow(previous);
    app.reminders.swap(windows);
    app.reminder = app.reminders.front();
    SetForegroundWindow(app.reminder);
    return true;
}

void StartBreak() {
    if (app.reminder || app.shuttingDown) return;
    if (!app.smoke && (!SessionReady() || !DesktopReady())) { Schedule(); return; }
    if (app.settings) { Schedule(); return; }
    KillTimer(app.controller, kReminderTimer);
    app.reminderDue = 0;
    app.notice = L"离开座位，伸展一下，让眼睛看看远处。";
    app.deadline = GetTickCount64() + app.config.breakSeconds * 1000ULL;
    if (!CreateBreakWindows()) { Log(L"无法创建全屏休息窗口", GetLastError()); Schedule(); return; }
    if (!SetTimer(app.controller, kDisplayTimer, 200, nullptr)) {
        EndBreak(); Balloon(L"倒计时初始化失败，本次休息已取消。"); return;
    }
    app.deadline = GetTickCount64() + app.config.breakSeconds * 1000ULL;
    UpdateTray();
    Log(L"开始休息");
}

void OpenSettings() {
    if (app.settings) { SetForegroundWindow(app.settings); return; }
    if (app.reminder) return;
    app.settings = CreateDialogParamW(app.instance, MAKEINTRESOURCEW(IDD_SETTINGS), app.controller, SettingsProc, 0);
    if (!app.settings) { Error(L"无法创建设置窗口。"); return; }
    ShowOwnedWindow(app.settings);
    SetForegroundWindow(app.settings);
}

void OpenInitialSettings() {
    if (!app.initialSettingsPending || app.shuttingDown) return;
    if (!app.smoke && (!app.session.available || !DesktopReady())) return;
    OpenSettings();
    if (app.settings) {
        app.initialSettingsPending = false;
        Log(L"首次运行，打开设置窗口；登录自启动待保存后生效");
    }
}

void ShowMenu() {
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | (app.session.available && !app.reminder ? 0 : MF_GRAYED), RestNow, L"立即休息");
    AppendMenuW(menu, MF_STRING, Pause, app.session.paused ? L"恢复提醒" : L"暂停提醒");
    AppendMenuW(menu, MF_STRING | (app.reminder ? MF_GRAYED : 0), Settings, L"设置…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (AutoStartEnabled() ? MF_CHECKED : 0), AutoStart, L"登录时自动运行");
    AppendMenuW(menu, MF_STRING, RemoveStartup, L"移除自动启动并退出");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Quit, L"退出");
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(app.controller);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        point.x, point.y, 0, app.controller, nullptr));
    DestroyMenu(menu);
    PostMessageW(app.controller, WM_NULL, 0, 0);
    if (command) PostMessageW(app.controller, WM_COMMAND, command, 0);
}

INT_PTR CALLBACK SettingsProc(HWND window, UINT message, WPARAM wParam, LPARAM) {
    switch (message) {
    case WM_INITDIALOG:
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app.iconSmall));
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app.iconLarge));
        SetDlgItemInt(window, IDC_INTERVAL, app.config.intervalMinutes, FALSE);
        SetDlgItemInt(window, IDC_DURATION, app.config.breakSeconds, FALSE);
        SendDlgItemMessageW(window, IDC_INTERVAL, EM_SETLIMITTEXT, 4, 0);
        SendDlgItemMessageW(window, IDC_DURATION, EM_SETLIMITTEXT, 4, 0);
        CheckDlgButton(window, IDC_HOURLY, app.config.hourly ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_AUTOSTART, (app.firstRun || AutoStartEnabled()) ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(window, IDC_INTERVAL), !app.config.hourly);
        SetDlgItemTextW(window, IDC_CONFIG_PATH, app.configPath.c_str());
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_HOURLY:
            EnableWindow(GetDlgItem(window, IDC_INTERVAL), IsDlgButtonChecked(window, IDC_HOURLY) != BST_CHECKED);
            return TRUE;
        case IDOK: {
            if (app.smoke) return TRUE;
            BOOL intervalValid = FALSE, durationValid = FALSE;
            Config next;
            next.intervalMinutes = GetDlgItemInt(window, IDC_INTERVAL, &intervalValid, FALSE);
            next.breakSeconds = GetDlgItemInt(window, IDC_DURATION, &durationValid, FALSE);
            next.hourly = IsDlgButtonChecked(window, IDC_HOURLY) == BST_CHECKED;
            if (!intervalValid || !durationValid || !ValidConfig(next)) {
                Error(L"提醒间隔必须为 1～1440 分钟，休息时长必须为 1～3600 秒。", window); return TRUE;
            }
            std::wstring error;
            if (!SaveConfig(app.configPath, next, error)) { Error(error, window); return TRUE; }
            app.firstRun = false;
            app.config = next;
            Schedule();
            const bool autoStart = IsDlgButtonChecked(window, IDC_AUTOSTART) == BST_CHECKED;
            if (autoStart != AutoStartEnabled() && !SetAutoStart(autoStart, error)) {
                Error(L"提醒设置已保存，但" + error, window); return TRUE;
            }
            DestroyWindow(window);
            return TRUE;
        }
        case IDCANCEL: DestroyWindow(window); return TRUE;
        }
        break;
    case WM_CLOSE: DestroyWindow(window); return TRUE;
    case WM_DESTROY: app.settings = nullptr; return TRUE;
    }
    return FALSE;
}

void Text(HDC dc, const wchar_t* text, RECT bounds, int size, int weight, COLORREF color, UINT format) {
    const HFONT font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    const auto old = SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &bounds, format);
    SelectObject(dc, old);
    DeleteObject(font);
}

LRESULT CALLBACK BreakProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const auto background = CreateSolidBrush(RGB(245, 248, 244));
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        const auto scaled = [dpi](int value) { return MulDiv(value, dpi, 96); };
        // 每个屏幕独立按 DPI 缩放，内容整体垂直居中。
        SetViewportOrgEx(dc, 0, (client.bottom - scaled(320)) / 2, nullptr);
        Text(dc, L"TAKE A BREAK", {0, scaled(23), client.right, scaled(47)}, scaled(12), FW_SEMIBOLD,
             RGB(80, 111, 91), DT_CENTER | DT_SINGLELINE);
        Text(dc, L"起来歇会儿", {0, scaled(56), client.right, scaled(103)}, scaled(31), FW_SEMIBOLD,
             RGB(28, 62, 44), DT_CENTER | DT_SINGLELINE);
        const auto remaining = RemainingSeconds(app.deadline, GetTickCount64());
        const auto count = std::to_wstring(remaining);
        Text(dc, count.c_str(), {0, scaled(108), client.right, scaled(193)}, scaled(66), FW_NORMAL,
             RGB(37, 104, 68), DT_CENTER | DT_SINGLELINE);
        Text(dc, L"秒后结束休息", {0, scaled(198), client.right, scaled(223)}, scaled(14), FW_NORMAL,
             RGB(86, 102, 92), DT_CENTER | DT_SINGLELINE);
        Text(dc, app.notice.c_str(), {scaled(20), scaled(235), client.right - scaled(20), scaled(263)},
             scaled(14), FW_NORMAL, RGB(49, 66, 54), DT_CENTER | DT_SINGLELINE);
        const wchar_t* hint = L"休息结束后自动返回";
        Text(dc, hint, {0, scaled(278), client.right, scaled(301)}, scaled(11), FW_NORMAL,
             RGB(110, 121, 114), DT_CENTER | DT_SINGLELINE);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_KEYDOWN: return 0;
    case WM_CLOSE: return 0;
    case WM_SYSCOMMAND:
        switch (wParam & 0xFFF0) {
        case SC_CLOSE: case SC_MINIMIZE: case SC_MAXIMIZE: case SC_MOVE: case SC_SIZE: return 0;
        }
        break;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED && app.reminder) PostMessageW(app.controller, WM_REFIT_BREAK, 0, 0);
        return 0;
    case WM_DPICHANGED: {
        MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
            const auto& bounds = monitor.rcMonitor;
            SetWindowPos(window, HWND_TOPMOST, bounds.left, bounds.top, bounds.right - bounds.left,
                         bounds.bottom - bounds.top, SWP_NOACTIVATE);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void SmokeTick() {
    ++app.smokeTicks;
    if (app.smokeTicks > (app.preview ? 400U : 60U)) { app.exitCode = 11; EndBreak(); PostMessageW(app.controller, WM_CLOSE, 0, 0); return; }
    if (app.smokePhase == 0) {
        if (IsWindowVisible(app.controller) || (GetWindowLongPtrW(app.controller, GWL_EXSTYLE) & WS_EX_APPWINDOW)) {
            app.exitCode = 12;
        }
        const bool startupBefore = AutoStartEnabled();
        app.firstRun = true;
        app.initialSettingsPending = true;
        OpenInitialSettings();
        if (!app.settings || IsDlgButtonChecked(app.settings, IDC_AUTOSTART) != BST_CHECKED) app.exitCode = 18;
        if (app.settings) SendMessageW(app.settings, WM_COMMAND, IDCANCEL, 0);
        if (app.settings || !app.firstRun || AutoStartEnabled() != startupBefore) app.exitCode = 19;
        // 取消后本次不反复弹窗；下次启动仍由配置缺失重新触发。
        OpenInitialSettings();
        if (app.settings) app.exitCode = 20;
        app.firstRun = false;
        OpenInitialSettings();
        if (app.settings) app.exitCode = 21;
        OpenSettings();
        if ((IsDlgButtonChecked(app.settings, IDC_AUTOSTART) == BST_CHECKED) != startupBefore) app.exitCode = 22;
        if (!app.settings || !IsWindowVisible(app.settings) ||
            GetDlgItemInt(app.settings, IDC_DURATION, nullptr, FALSE) != app.config.breakSeconds) app.exitCode = 13;
        app.smokePhase = 1;
    } else if (app.smokePhase == 1 && app.smokeTicks >= (app.preview ? 150U : 5U)) {
        if (app.settings) DestroyWindow(app.settings);
        StartBreak();
        if (!app.reminder || !IsWindowVisible(app.reminder) ||
            !(GetWindowLongPtrW(app.reminder, GWL_EXSTYLE) & WS_EX_TOPMOST)) app.exitCode = 14;
        std::vector<RECT> monitorBounds;
        if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitorBounds)) ||
            monitorBounds.size() != app.reminders.size()) app.exitCode = 23;
        for (size_t index = 0; index < app.reminders.size(); ++index) {
            HWND reminder = app.reminders[index];
            RECT bounds{};
            GetWindowRect(reminder, &bounds);
            if (index >= monitorBounds.size() || !EqualRect(&bounds, &monitorBounds[index]) ||
                (GetWindowLongPtrW(reminder, GWL_STYLE) & (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX)) ||
                !(GetWindowLongPtrW(reminder, GWL_EXSTYLE) & WS_EX_TOPMOST)) app.exitCode = 24;
            SendMessageW(reminder, WM_KEYDOWN, VK_ESCAPE, 0);
            SendMessageW(reminder, WM_SYSCOMMAND, SC_CLOSE, 0);
            SendMessageW(reminder, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            SendMessageW(reminder, WM_CLOSE, 0, 0);
            if (!IsWindowVisible(reminder) || IsIconic(reminder)) app.exitCode = 25;
        }
        const auto deadline = app.deadline;
        const bool paused = app.session.paused;
        SendMessageW(app.controller, WM_COMMAND, Pause, 0);
        SendMessageW(app.controller, WM_COMMAND, Quit, 0);
        SendMessageW(app.controller, WM_COMMAND, RemoveStartup, 0);
        SendMessageW(app.controller, WM_CLOSE, 0, 0);
        if (!app.reminder || app.session.paused != paused || app.shuttingDown) app.exitCode = 26;
        SendMessageW(app.controller, WM_DISPLAYCHANGE, 0, 0);
        if (!app.reminder || app.reminders.size() != monitorBounds.size() || app.deadline != deadline) app.exitCode = 27;
        app.smokePhase = 2;
    } else if (app.smokePhase == 2 && !app.reminder) {
        // 模拟会话不可用，验证强制提醒仍可被锁屏事件安全取消。
        StartBreak();
        const auto cancelledWindows = app.reminders;
        SetAvailable(false);
        if (app.reminder || !app.reminders.empty()) app.exitCode = 28;
        for (HWND cancelled : cancelledWindows) if (IsWindow(cancelled)) app.exitCode = 28;
        // 模拟旧 WM_TIMER 在解锁重置后到达，不改变真实会话或输入状态。
        app.smoke = false;
        app.session.available = true;
        Schedule();
        const auto expectedDue = app.reminderDue;
        SendMessageW(app.controller, WM_TIMER, kReminderTimer, 0);
        if (!expectedDue || app.reminder || app.reminderDue != expectedDue) app.exitCode = 16;
        app.session.paused = true;
        SetAvailable(false);
        SendMessageW(app.controller, WM_TIMER, kReminderTimer, 0);
        if (app.reminder || app.reminderDue) app.exitCode = 17;
        SetAvailable(true);
        if (!app.session.paused || app.session.CanRemind()) app.exitCode = 15;
        app.smoke = true;
        PostMessageW(app.controller, WM_CLOSE, 0, 0);
        app.smokePhase = 3;
    }
}

LRESULT CALLBACK ControllerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (app.taskbarCreated && message == app.taskbarCreated) { UpdateTray(true); return 0; }
    switch (message) {
    case WM_TRAY:
        if (app.reminder) return 0;
        if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP) ShowMenu();
        else if (LOWORD(lParam) == NIN_SELECT || LOWORD(lParam) == NIN_KEYSELECT) OpenSettings();
        return 0;
    case WM_COMMAND: {
        // 防止托盘旧消息或快捷键绕过正在进行的休息。
        if (app.reminder) return 0;
        std::wstring error;
        switch (LOWORD(wParam)) {
        case RestNow: StartBreak(); break;
        case Pause:
            app.session.paused = !app.session.paused;
            EndBreak();
            Log(app.session.paused ? L"用户暂停提醒" : L"用户恢复提醒");
            break;
        case Settings: OpenSettings(); break;
        case AutoStart: if (!SetAutoStart(!AutoStartEnabled(), error)) Error(error); break;
        case RemoveStartup:
            if (!SetAutoStart(false, error)) { Error(error); break; }
            PostMessageW(window, WM_CLOSE, 0, 0); break;
        case Quit: PostMessageW(window, WM_CLOSE, 0, 0); break;
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == kReminderTimer) {
            // KillTimer 不移除已经排队的 WM_TIMER；旧周期消息不得提前触发新周期。
            if (!app.reminderDue || GetTickCount64() < app.reminderDue) return 0;
            KillTimer(window, kReminderTimer);
            app.reminderDue = 0;
            if (app.session.CanRemind()) StartBreak();
        } else if (wParam == kDisplayTimer && app.reminder) {
            if (GetTickCount64() >= app.deadline) EndBreak();
            else for (HWND reminder : app.reminders) {
                SetWindowPos(reminder, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                InvalidateRect(reminder, nullptr, FALSE);
            }
        } else if (wParam == kSmokeTimer) SmokeTick();
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_REFIT_BREAK:
        if (app.reminder && !CreateBreakWindows()) {
            Log(L"显示器变化后重建提醒失败，取消本次休息", GetLastError());
            EndBreak();
        }
        return 0;
    case WM_WTSSESSION_CHANGE:
        switch (wParam) {
        case WTS_SESSION_LOCK: case WTS_SESSION_LOGOFF: case WTS_CONSOLE_DISCONNECT: case WTS_REMOTE_DISCONNECT:
            SetAvailable(false); break;
        case WTS_SESSION_UNLOCK: case WTS_SESSION_LOGON: case WTS_CONSOLE_CONNECT: case WTS_REMOTE_CONNECT:
        case WTS_SESSION_DESKTOP_READY:
            SetAvailable(SessionReady()); break;
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) SetAvailable(false);
        else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) SetAvailable(SessionReady());
        return TRUE;
    case WM_TIMECHANGE:
        if (app.config.hourly) Schedule();
        return 0;
    case WM_QUERYENDSESSION:
        app.shuttingDown = true;
        EndBreak();
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) DestroyWindow(window);
        else { app.shuttingDown = false; Schedule(); }
        return 0;
    case WM_CLOSE:
        if (app.reminder) return 0;
        app.shuttingDown = true;
        EndBreak();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY: {
        app.shuttingDown = true;
        if (app.registeredSession) WTSUnRegisterSessionNotification(window);
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = window; data.uID = 1;
        if (app.trayAdded) Shell_NotifyIconW(NIM_DELETE, &data);
        PostQuitMessage(app.exitCode);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int Run(HINSTANCE instance) {
    app.instance = instance;
    // 图标嵌入 EXE，按系统尺寸加载共享句柄，无需外部资源文件。
    app.iconSmall = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    app.iconLarge = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    if (!app.iconSmall || !app.iconLarge) { Log(L"加载程序图标失败", GetLastError()); return 1; }
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    app.preview = argc == 2 && wcscmp(argv[1], L"--preview") == 0;
    app.smoke = app.preview || (argc == 2 && wcscmp(argv[1], L"--smoke-test") == 0);
    LocalFree(argv);
    app.mutex = CreateMutexW(nullptr, FALSE, app.smoke ? L"Local\\TakeABreak.Smoke.v1" : L"Local\\TakeABreak.Desktop.v1");
    if (!app.mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        Log(L"当前会话中已有实例，本次启动退出"); return 0;
    }
    app.configPath = ConfigPath();
    app.firstRun = !app.smoke && ConfigMissing(app.configPath);
    app.initialSettingsPending = app.firstRun;
    bool repaired = false;
    if (!app.smoke && !app.configPath.empty()) app.config = LoadConfig(app.configPath, &repaired);
    if (app.smoke) { app.config.breakSeconds = app.preview ? 20U : 2U; }
    app.session.available = SessionReady();
    app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSEXW controllerClass{sizeof(controllerClass)};
    controllerClass.hInstance = instance;
    controllerClass.lpszClassName = kControllerClass;
    controllerClass.lpfnWndProc = ControllerProc;
    controllerClass.hIcon = app.iconLarge;
    controllerClass.hIconSm = app.iconSmall;
    if (!RegisterClassExW(&controllerClass)) return 1;
    WNDCLASSEXW breakClass{sizeof(breakClass)};
    breakClass.hInstance = instance;
    breakClass.lpszClassName = kBreakClass;
    breakClass.lpfnWndProc = BreakProc;
    breakClass.hIcon = app.iconLarge;
    breakClass.hIconSm = app.iconSmall;
    breakClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&breakClass)) return 1;
    // 隐藏顶层窗口负责广播和托盘消息，不显示在任务栏。
    app.controller = CreateWindowExW(WS_EX_TOOLWINDOW, kControllerClass, L"TakeABreak.Controller", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!app.controller) return 1;
    // 兼容外部以较高权限启动时接收 Explorer 重启通知。
    ChangeWindowMessageFilterEx(app.controller, app.taskbarCreated, MSGFLT_ALLOW, nullptr);
    app.registeredSession = WTSRegisterSessionNotification(app.controller, NOTIFY_FOR_THIS_SESSION) != FALSE;
    if (!app.registeredSession && !app.smoke) {
        Error(L"无法监听登录和锁屏状态，程序将退出。请稍后重试。");
        DestroyWindow(app.controller); return 1;
    }
    UpdateTray(true);
    if (!app.trayAdded && !app.smoke) {
        Error(L"无法创建托盘图标，程序将退出。请在桌面加载完成后重试。");
        DestroyWindow(app.controller); return 1;
    }
    if (repaired) Balloon(L"部分配置值无效，已使用默认值。请在设置中检查并保存。");
    Schedule();
    if (app.smoke && !SetTimer(app.controller, kSmokeTimer, 150, nullptr)) return 10;
    OpenInitialSettings();
    Log(L"程序启动");
    MSG message{};
    BOOL received = 0;
    while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!app.settings || !IsDialogMessageW(app.settings, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (received == -1) { app.shuttingDown = true; EndBreak(); return 1; }
    return static_cast<int>(message.wParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int result = 1;
    try { result = Run(instance); }
    catch (const std::exception&) { Log(L"发生异常，退出程序"); }
    catch (...) { Log(L"发生未知异常，退出程序"); }
    if (IsWindow(app.controller)) DestroyWindow(app.controller);
    if (app.mutex) { CloseHandle(app.mutex); app.mutex = nullptr; }
    return result;
}
