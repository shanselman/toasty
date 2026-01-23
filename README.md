# Toasty

<img src="icons/toasty.png" alt="Toasty mascot" width="128" align="right">

A tiny toast notification CLI for **Windows** and **macOS** that hooks into Coding Agents so you get notified when their long running tasks are finished.

- **Windows**: 229 KB, no dependencies
- **macOS**: Requires `terminal-notifier` (installable via Homebrew)

## Quick Start

```bash
toasty "Hello World" -t "Toasty"
```

That's it. On Windows, Toasty auto-registers on first run.

## Installation

### Windows

Download `toasty-x64.exe` or `toasty-arm64.exe` from [Releases](../../releases).

### macOS

1. **Download toasty** from [Releases](../../releases):
   - `toasty-macos` - Universal binary (Intel + Apple Silicon)
   - `toasty-macos-x64` - Intel Macs only
   - `toasty-macos-arm64` - Apple Silicon only

2. **Make it executable and move to PATH**:
   ```bash
   chmod +x toasty-macos
   sudo mv toasty-macos /usr/local/bin/toasty
   ```

3. **Install terminal-notifier** (required for notifications):
   ```bash
   brew install terminal-notifier
   ```

4. **Enable notifications**:
   - Open **System Settings → Notifications**
   - Find **terminal-notifier** in the list
   - Enable **Allow Notifications**
   - Set notification style to **Banners** or **Alerts**

   > **Note**: If terminal-notifier doesn't appear in the list, run it once:
   > ```bash
   > open /opt/homebrew/Cellar/terminal-notifier/*/terminal-notifier.app
   > ```

## Usage

```
toasty <message> [options]
toasty --install [agent]
toasty --uninstall
toasty --status

Options:
  -t, --title <text>   Set notification title (default: "Notification")
  --app <name>         Use AI CLI preset (claude, copilot, gemini)
  -h, --help           Show this help
  --install [agent]    Install hooks for AI CLI agents (claude, gemini, copilot, or all)
  --uninstall          Remove hooks from all AI CLI agents
  --status             Show installation status
```

## AI CLI Auto-Detection

Toasty automatically detects when it's called from a known AI CLI tool and applies the appropriate icon and title. No flags needed!

**Auto-detected tools:**
- Claude Code
- GitHub Copilot
- Google Gemini CLI

```bash
# Called from Claude - automatically uses Claude preset
toasty "Analysis complete"

# Called from Copilot - automatically uses Copilot preset
toasty "Code review done"
```

### Manual Preset Selection

Override auto-detection with `--app`:

```bash
toasty "Processing finished" --app claude
toasty "Build succeeded" --app copilot
toasty "Query done" --app gemini
```

## One-Click Hook Installation

Toasty can automatically configure AI CLI agents to show notifications when tasks complete.

### Supported Agents

| Agent | Config Path | Hook Event | Scope |
|-------|-------------|------------|-------|
| Claude Code | `~/.claude/settings.json` | `Stop` | User |
| Gemini CLI | `~/.gemini/settings.json` | `AfterAgent` | User |
| GitHub Copilot | `.github/hooks/toasty.json` | `sessionEnd` | Repo |

### Auto-Install

```bash
# Install for all detected agents
toasty --install

# Install for specific agent
toasty --install claude
toasty --install gemini
toasty --install copilot

# Check what's installed
toasty --status

# Remove all hooks
toasty --uninstall
```

### Example Output

```
Detecting AI CLI agents...
  [x] Claude Code found
  [x] Gemini CLI found
  [ ] GitHub Copilot (in current repo)

Installing toasty hooks...
  [x] Claude Code: Added Stop hook
  [x] Gemini CLI: Added AfterAgent hook

Done! You'll get notifications when AI agents finish.
```

## Manual Integration

If you prefer to configure hooks manually:

### Claude Code

Add to `~/.claude/settings.json`:

**Windows:**
```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "C:\\path\\to\\toasty.exe \"Claude finished\"",
            "timeout": 5000
          }
        ]
      }
    ]
  }
}
```

**macOS:**
```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "/path/to/toasty \"Claude finished\"",
            "timeout": 5000
          }
        ]
      }
    ]
  }
}
```

### Gemini CLI

Add to `~/.gemini/settings.json`:

**Windows:**
```json
{
  "hooks": {
    "AfterAgent": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "C:\\path\\to\\toasty.exe \"Gemini finished\"",
            "timeout": 5000
          }
        ]
      }
    ]
  }
}
```

**macOS:**
```json
{
  "hooks": {
    "AfterAgent": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "/path/to/toasty \"Gemini finished\"",
            "timeout": 5000
          }
        ]
      }
    ]
  }
}
```

### GitHub Copilot

Add to `.github/hooks/toasty.json`:

```json
{
  "version": 1,
  "hooks": {
    "sessionEnd": [
      {
        "type": "command",
        "bash": "toasty 'Copilot finished'",
        "powershell": "toasty.exe 'Copilot finished'",
        "timeoutSec": 5
      }
    ]
  }
}
```

## Building

### Windows

Requires Visual Studio 2022 with C++ workload.

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\Release\toasty.exe`

### macOS

Requires Xcode Command Line Tools.

```bash
clang++ -std=c++20 -x objective-c++ -framework Foundation -framework AppKit -o build/toasty main_mac.mm
```

Or with CMake:
```bash
cmake -S . -B build
cmake --build build --config Release
```

Output: `build/toasty`

## License

MIT
