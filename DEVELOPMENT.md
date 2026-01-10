# Development Guide

## Architecture

Toasty uses C++/WinRT to call the `Windows.UI.Notifications` API directly. No Windows App SDK runtime required.

### Key Components

- **C++/WinRT**: Modern C++ projection for Windows Runtime APIs
- **AUMID Registration**: Creates a Start Menu shortcut with `System.AppUserModel.ID` property
- **ToastNotificationManager**: Windows built-in toast notification system

### Why Not Windows App SDK?

Windows App SDK requires the WindowsAppRuntime to be installed on the user's machine. For a simple CLI tool, this is unnecessary overhead. The raw `Windows.UI.Notifications` API works on any Windows 10/11 system.

### Why Not .NET?

The `csharp` branch has a .NET 10 Native AOT version that works, but produces a 3.4 MB binary. The C++ version is 229 KB - 15x smaller.

## Building

### Prerequisites

- Visual Studio 2022 with "Desktop development with C++" workload
- CMake 3.20+
- Windows 10 SDK

### Build Commands

```cmd
# Configure (from Developer Command Prompt)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build Release
cmake --build build --config Release

# Build Debug
cmake --build build --config Debug
```

### Build for ARM64

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A ARM64
cmake --build build --config Release
```

## How AUMID Registration Works

Windows toast notifications require an AppUserModelId (AUMID) to identify the sending application. For unpackaged desktop apps, this requires:

1. A Start Menu shortcut (.lnk file)
2. The shortcut must have the `System.AppUserModel.ID` property set
3. The AUMID used in code must match the shortcut's property

The `--register` flag creates this shortcut at:
```
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Toasty.lnk
```

## Code Structure

```
main.cpp
├── print_usage()      - CLI help text
├── escape_xml()       - XML entity escaping for toast content
├── create_shortcut()  - AUMID registration via Start Menu shortcut
└── wmain()            - Entry point, argument parsing, toast display
```

## Toast XML Format

```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>Title</text>
      <text>Message</text>
    </binding>
  </visual>
</toast>
```

## Branches

| Branch | Description |
|--------|-------------|
| `cplusplus` | C++/WinRT implementation (229 KB) |
| `csharp` | .NET 10 Native AOT implementation (3.4 MB) |

## Troubleshooting

### Notifications not appearing

1. Run `toasty --register` first
2. Check Windows Settings > System > Notifications > Toasty is enabled
3. Check Focus Assist / Do Not Disturb is off

### Build errors about missing headers

Ensure Windows 10 SDK is installed via Visual Studio Installer.

### vswhere.exe not found during publish (C# branch)

Run from Developer Command Prompt for VS 2022.
