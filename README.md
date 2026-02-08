# Toasty

<img src="icons/toasty.png" alt="Toasty mascot" width="128" align="right">

A tiny Windows toast notification CLI that knows how to hook into Coding Agents so you get notified when their long running tasks are finished. 229 KB, no dependencies.

## Quick Start

```cmd
toasty "Hello World" -t "Toasty"
```

That's it. Toasty auto-registers on first run.

## Usage

```
toasty <message> [options]
toasty --install [agent]
toasty --uninstall
toasty --status

Options:
  -t, --title <text>   Set notification title (default: "Notification")
  --app <name>         Use AI CLI preset (claude, copilot, gemini, codex, cursor)
  -v, --version        Show version and exit
  -h, --help           Show this help
  --install [agent]    Install hooks for AI CLI agents (claude, gemini, copilot, or all)
  --uninstall          Remove hooks from all AI CLI agents
  --status             Show installation status
  --dry-run            Show what would happen without executing side effects
```

## AI CLI Auto-Detection

Toasty automatically detects when it's called from a known AI CLI tool and applies the appropriate icon and title. No flags needed!

**Auto-detected tools:**
- Claude Code
- GitHub Copilot
- Google Gemini CLI

```cmd
# Called from Claude - automatically uses Claude preset
toasty "Analysis complete"

# Called from Copilot - automatically uses Copilot preset
toasty "Code review done"
```

### Manual Preset Selection

Override auto-detection with `--app`:

```cmd
toasty "Processing finished" --app claude
toasty "Build succeeded" --app copilot
toasty "Query done" --app gemini
```

## One-Click Hook Installation

Toasty can automatically configure AI CLI agents to show notifications when tasks complete.

> 📖 **Want more control?** See [HOOKS.md](HOOKS.md) for all available hook events (permission requests, errors, session lifecycle, etc.)

### Supported Agents

| Agent | Config Path | Hook Event | Scope |
|-------|-------------|------------|-------|
| Claude Code | `~/.claude/settings.json` | `Stop` | User |
| Gemini CLI | `~/.gemini/settings.json` | `AfterAgent` | User |
| GitHub Copilot | `.github/hooks/toasty.json` | `sessionEnd` | Repo |

### Auto-Install

```cmd
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

### Gemini CLI

Add to `~/.gemini/settings.json`:

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

## Push Notifications (ntfy)

Get push notifications on your phone when AI agents finish — even when you're away from your desk. Uses [ntfy.sh](https://ntfy.sh), a free, open-source notification service. No account or API key required.

### Setup

1. Install the [ntfy app](https://ntfy.sh) on your phone (iOS/Android)
2. Subscribe to a topic of your choice (use something unique and hard to guess)
3. Set the environment variable:

```cmd
set TOASTY_NTFY_TOPIC=my-secret-toasty-topic
```

That's it. Now every toast notification also sends a push to your phone.

### Self-Hosted Server

If you run your own [ntfy server](https://docs.ntfy.sh/install/), point toasty at it:

```cmd
set TOASTY_NTFY_SERVER=ntfy.example.com
```

Default server is `ntfy.sh` if not set.

### How It Works

- Toasty checks for `TOASTY_NTFY_TOPIC` on each run
- If set, it sends an HTTPS POST to `ntfy.sh/<topic>` (or your custom server) with the notification title and message
- The request has a 5-second timeout — if the service is down or the network is slow, toasty won't hang
- If anything goes wrong with the push notification, the local toast still shows normally

### Example

```cmd
# Set once (add to your shell profile)
set TOASTY_NTFY_TOPIC=scotts-coding-notifications

# Now every toasty notification also goes to your phone
toasty "Claude finished analysis" --app claude
```

## Update Notifications

Toasty automatically checks for new versions once per day. If an update is available, you'll see:

- A message in your terminal: `Update available: v0.3 → v0.4 (https://github.com/shanselman/toasty/releases)`
- A clickable toast notification that opens the releases page

The check is non-blocking (5-second timeout), throttled to once every 24 hours, and silently skips if offline. It never auto-updates — you download the new version yourself.

Check your current version anytime:

```cmd
toasty --version
```

## Building

Requires Visual Studio 2022 with C++ workload.

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\Release\toasty.exe`

## Testing

Run the test suite after building:

```cmd
.\tests\test-toasty.ps1 -ExePath .\build\Release\toasty.exe
```

Tests use `--dry-run` to validate argument parsing, preset icons, toast XML generation, install/uninstall logic, and ntfy configuration without showing actual notifications or modifying any config files.

## License

MIT
