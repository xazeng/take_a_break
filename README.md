# Take a Break · 起来歇会儿

English | [简体中文](README.zh-CN.md)

“Just five more minutes.” Sound familiar? Before you know it, you and your chair are in a committed relationship.

**Take a Break** reminds you to stand up, stretch, and give your eyes some time away from the screen. It stays quietly in your system tray until break time, then shows up on every monitor. Yes, even the one you were about to sneak over to.

By default, take a 10-second break every 60 minutes. Set both durations to suit your day. Just run the EXE—no installation needed. Supports Windows 10/11 x64.

## Features

- Stays in the system tray without a taskbar button when idle.
- Shows the time until your next break in Settings, along with states such as paused or waiting for unlock.
- Starts counting after sign-in. Both the default 60-minute interval and 10-second break are configurable; reminders on the hour are also available.
- Cancels breaks when you lock, sign out, switch users, or suspend the computer. Unlocking starts a fresh interval, with no catch-up reminders.
- Shows a full-screen, topmost reminder on every monitor, covering the taskbar with a shared countdown. All reminder windows close when the break ends.
- Offers tray menu actions for taking a break now, pausing/resuming, opening Settings, running at sign-in, disabling auto-start and exiting, and exiting.
- Keeps manual pause in effect after unlocking. Restarting the app resumes normal scheduling.
- Uses no Windows service or network connection. Settings live in a small local INI file.
- Follows your Windows user interface language: Chinese variants use Simplified Chinese; all other languages use English. Restart the app after changing your Windows language.

## Getting started

Run `TakeABreak.exe`. On first launch, when no configuration file exists, Settings opens automatically with **Run at sign-in** checked. The startup entry is registered only after you click **Save**. Canceling or closing the window saves nothing and leaves startup unchanged; Settings will open again next time. With an existing configuration, the app starts in the tray.

Find the app icon in the notification area—it may be under the hidden icons arrow. Click it to open Settings, or right-click for the menu.

Only one instance runs per Windows session. Double-clicking the EXE again asks the existing instance to open Settings, then exits the new process. During a full-screen break, Settings waits until the countdown ends. If an older version does not support this notification, exit it before starting the new version.

Set the reminder interval to 1–1440 minutes and the break duration to 1–3600 seconds. Break windows have no close or minimize buttons; Esc, Alt+F4, and tray actions cannot end a break early. Wait for the countdown to finish. Locking, signing out, or suspending still cancels the break, and unlocking restarts the timer. Saving settings also restarts the timer. On-the-hour mode schedules the next local clock hour.

The countdown in Settings updates every second and reflects the active schedule, not unsaved edits. While Settings is open, breaks do not pop up: a due break is skipped and the next one is scheduled. Close Settings when you are done checking so the app can keep nudging you from the tray.

These are full-screen reminder windows; they do not block keyboard or mouse input. The Windows secure desktop, Ctrl+Alt+Del, and Task Manager remain available. The app cannot guarantee coverage of the secure desktop, exclusive full-screen applications, or every other topmost window.

You can also toggle **Run at sign-in** from the tray menu. It applies only to the current user and requires no service. The checked option on first launch takes effect only after saving. The INI records the EXE's absolute path. Before moving the program, exit it; after moving it, manually run the EXE from its new location once. An existing startup entry will be updated to the new path; disabled auto-start stays disabled. Moving the file alone is not enough—the old startup path can no longer launch it.

The configuration is created on first save at:

```text
%LOCALAPPDATA%\TakeABreak\settings.ini
```

```ini
[General]
Version=1
IntervalMinutes=60
BreakSeconds=10
Hourly=0
ExecutablePath=C:\Apps\TakeABreak\TakeABreak.exe
```

The app manages `ExecutablePath` automatically and fills it in for older INI files that lack this field. On first launch, recording the path does not create an INI before you save; canceling still brings back the first-run settings on the next launch.

Administrator privileges are not required. The legacy `RestrictInput` field is ignored and removed on save; other settings remain compatible.

To remove the app, select **Disable auto-start and exit**, then delete the EXE. To remove your settings too, delete the `TakeABreak` configuration folder above.

## Building and testing

Built with **C++17 and the native Win32 API**, without Qt, Electron, or .NET.

Supported platforms: Windows 10 1607+ / Windows 11 x64. Building requires Visual Studio 2022 with **Desktop development with C++**, the Windows SDK, and CMake 3.20+.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The release executable is `build/Release/TakeABreak.exe`. The C/C++ runtime is statically linked, so only this EXE needs to be distributed. The test executable is not a runtime dependency.

Automated tests cover scheduling boundaries, invalid configuration values, legacy configuration compatibility, countdowns, and pause behavior. UI smoke tests briefly show Settings and a two-second break, then exit automatically. **They do not save user settings or modify startup entries.**

CTest also runs Chinese and English UI tests that check settings and reminder text, label widths, and language fallback. These tests override only the test process's language; they do not change Windows language settings. Language is not stored in the INI, and existing configurations remain compatible.

```powershell
.\build\Release\TakeABreak.exe --smoke-test
```

For a longer development preview, use `--preview`: it shows Settings for about 22.5 seconds, followed by a 20-second break, then exits automatically. It likewise leaves settings and startup entries unchanged.

Lock/unlock, sleep, multiple monitors, and behavior on Windows 11 hardware require interactive acceptance testing. Passing automated tests does not mean all of these scenarios have been tested on real systems.

## Resource targets and scope

Initial targets are an EXE smaller than 1 MB, an idle working set below 10 MB, and idle CPU usage near 0%. Early measurements on Windows 10 x64 on 2026-09-17 recorded a 187.5 KiB Release EXE, an idle working set of about 11.31 MiB, about 2.13 MiB of private bytes, and 0 ms of additional CPU time over 30 seconds. **The working set has not met the 10 MB target.** Results depend on the machine, permissions, and desktop environment. See [VERIFICATION.md](VERIFICATION.md) (Chinese) for measurement details and untested scenarios.

When idle, the app waits on messages and timers; reminder windows are created only when needed. The figures above are from an early version, not fresh measurements of the current release.

Reminders support multiple monitors, negative screen coordinates, and DPI scaling. Display layout changes refit the windows without resetting the remaining break time. The app does not provide machine-wide policy enforcement or a background service. See [DESIGN.md](DESIGN.md) for the design and [AGENTS.md](AGENTS.md) for development rules; both are in Chinese.

## License

[MIT](LICENSE). Take a Break is a working name. Other projects already use this name; no claim of uniqueness is made.
