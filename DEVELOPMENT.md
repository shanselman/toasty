# Development Guide

## Architecture

Toasty uses C++/WinRT to call the `Windows.UI.Notifications` API directly. No Windows App SDK runtime required.

### Key Components

- **C++/WinRT**: Modern C++ projection for Windows Runtime APIs
- **AUMID Registration**: Creates a Start Menu shortcut with `System.AppUserModel.ID` property
- **ToastNotificationManager**: Windows built-in toast notification system
- **Protocol Handler**: `toasty://focus` URL scheme for click-to-focus functionality
- **Embedded Icons**: PNG icons for AI agents compiled into the executable

### Why Not Windows App SDK?

Windows App SDK requires the WindowsAppRuntime to be installed on the user's machine. For a simple CLI tool, this is unnecessary overhead. The raw `Windows.UI.Notifications` API works on any Windows 10/11 system.

### Why Not .NET?

The `csharp` branch has a .NET 10 Native AOT version that works, but produces a 3.4 MB binary. The C++ version is ~250 KB - 14x smaller.

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

Toasty automatically creates this shortcut on first use at:
```
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Toasty.lnk
```

## Click-to-Focus Feature

When you click a toast notification, toasty brings your terminal window to the foreground.

### How It Works

1. **On toast display**: Walk the process tree to find the parent terminal window (Windows Terminal, VS Code, etc.)
2. **Save HWND**: Store the window handle in registry (`HKCU\Software\Toasty\LastConsoleWindow`)
3. **Toast activation**: Toast XML includes `activationType="protocol" launch="toasty://focus"`
4. **On click**: Windows launches `toasty.exe --focus` via protocol handler
5. **Focus window**: Read HWND from registry and bring window to foreground

### Focus Restrictions

Protocol handlers run in a restricted context - Windows prevents arbitrary apps from stealing focus. Toasty uses multiple techniques:

- `AttachThreadInput()` to borrow focus permission from foreground thread
- `SendInput()` with empty mouse event to simulate user activity
- `SwitchToThisWindow()` works better than `SetForegroundWindow()` for protocol handlers
- `keybd_event()` with ALT key press/release to help with focus

### Protocol Handler Registration

The `toasty://` protocol is registered in `HKCU\Software\Classes\toasty` pointing to:
```
"C:\path\to\toasty.exe" --focus "%1"
```

## Auto-Detection of AI Agents

Toasty walks the process tree looking for known parent processes:

| Process | Detection Method |
|---------|-----------------|
| Claude Code | `claude.exe` or `@anthropic` in command line |
| Gemini CLI | `gemini-cli`, `@google/gemini` in command line |
| Cursor | `cursor` in process name |
| Copilot/Codex | Process name match |

When detected, toasty automatically uses the appropriate icon and title.

## Code Structure

```
main.cpp
├── Utilities
│   ├── HandleGuard           - RAII wrapper for Windows handles
│   ├── to_lower()            - Case-insensitive string helper
│   ├── escape_xml()          - XML entity escaping
│   └── escape_json_string()  - JSON string escaping
│
├── Icon Extraction
│   └── extract_icon_to_temp() - Extract embedded PNG to temp file
│
├── Process Tree Walking
│   ├── get_process_command_line()     - Read process command line via NtQueryInformationProcess
│   ├── detect_preset_from_ancestors() - Walk tree to find AI agent parent
│   └── find_ancestor_window()         - Walk tree to find terminal window
│
├── Focus Management
│   ├── save_console_window_handle()   - Save HWND to registry
│   ├── get_saved_console_window_handle() - Read HWND from registry
│   ├── force_foreground_window()      - Aggressive focus with thread attachment
│   └── focus_console_window()         - Main focus logic with fallbacks
│
├── Hook Installation
│   ├── install_claude() / uninstall_claude()
│   ├── install_gemini() / uninstall_gemini()
│   ├── install_copilot() / uninstall_copilot()
│   ├── is_*_installed() - Check functions
│   └── has_toasty_hook() - JSON array scanner
│
├── Registration
│   ├── create_shortcut()    - AUMID registration via Start Menu shortcut
│   ├── register_protocol()  - Register toasty:// URL handler
│   └── ensure_registered()  - Auto-register on first use
│
└── wmain() - Entry point, argument parsing, toast display
```

## Toast XML Format

Basic toast:
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

With click-to-focus and icon:
```xml
<toast activationType="protocol" launch="toasty://focus">
  <visual>
    <binding template="ToastGeneric">
      <image placement="appLogoOverride" src="C:\path\to\icon.png"/>
      <text>Title</text>
      <text>Message</text>
    </binding>
  </visual>
</toast>
```

## Hook Formats for AI Agents

### Claude Code (`~/.claude/settings.json`)
```json
{
  "hooks": {
    "Stop": [{
      "hooks": [{
        "type": "command",
        "command": "D:\\path\\to\\toasty.exe \"Task complete\" -t \"Claude Code\"",
        "timeout": 5000
      }]
    }]
  }
}
```

### Gemini CLI (`~/.gemini/settings.json`)
```json
{
  "hooks": {
    "AfterAgent": [{
      "hooks": [{
        "type": "command",
        "command": "D:\\path\\to\\toasty.exe \"Gemini finished\" -t \"Gemini\"",
        "timeout": 5000
      }]
    }]
  }
}
```

### GitHub Copilot (`.github/hooks/toasty.json` in repo)
```json
{
  "version": 1,
  "hooks": {
    "sessionStart": [{
      "type": "command",
      "bash": "toasty 'Copilot started' -t 'GitHub Copilot'",
      "powershell": "D:\\path\\to\\toasty.exe 'Copilot started' -t 'GitHub Copilot'",
      "timeoutSec": 5
    }],
    "sessionEnd": [{
      "type": "command",
      "bash": "toasty 'Copilot finished' -t 'GitHub Copilot'",
      "powershell": "D:\\path\\to\\toasty.exe 'Copilot finished' -t 'GitHub Copilot'",
      "timeoutSec": 5
    }]
  }
}
```

**Important**: 
- Claude and Gemini require the nested `hooks` array structure!
- GitHub Copilot: `sessionStart` works with `--resume`, but `sessionEnd` does not (known Copilot CLI bug)

## Embedded Resources

Icons are embedded as PNG resources in the executable:

| Resource ID | Icon |
|-------------|------|
| IDI_CLAUDE | Claude Code |
| IDI_COPILOT | GitHub Copilot |
| IDI_GEMINI | Gemini |
| IDI_CODEX | OpenAI Codex |
| IDI_CURSOR | Cursor |
| IDI_TOASTY | Default mascot |

Defined in `resource.h`, linked via `resources.rc`.

## Troubleshooting

### Notifications not appearing

1. Toasty auto-registers on first use
2. Check Windows Settings > System > Notifications > Toasty is enabled
3. Check Focus Assist / Do Not Disturb is off
4. Run `toasty --register` to re-register

### Click-to-focus not working

1. Ensure protocol handler is registered: check `HKCU\Software\Classes\toasty`
2. Try `toasty --register` to re-register protocol
3. Make sure you're clicking the toast body, not dismissing it

### Hooks not firing

1. **Restart the AI agent** after changing settings (settings only load at startup)
2. Check the hook JSON format - nested `hooks` array is required for Claude/Gemini
3. Test toasty directly first: `toasty.exe "Test" -t "Test"`

### Build errors about missing headers

Ensure Windows 10 SDK is installed via Visual Studio Installer.
