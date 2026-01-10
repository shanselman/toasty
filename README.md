# Toasty

A tiny Windows toast notification CLI. 229 KB, no dependencies.

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
  -h, --help           Show this help
  --install [agent]    Install hooks for AI CLI agents (claude, gemini, copilot, or all)
  --uninstall          Remove hooks from all AI CLI agents
  --status             Show installation status
```

## AI CLI Agent Integration

Toasty can automatically configure AI CLI agents to show notifications when tasks complete.

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
            "command": "C:\\path\\to\\toasty.exe \"Claude finished\" -t \"Claude Code\"",
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
            "command": "C:\\path\\to\\toasty.exe \"Gemini finished\" -t \"Gemini\"",
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
        "bash": "toasty 'Copilot finished' -t 'GitHub Copilot'",
        "powershell": "toasty.exe 'Copilot finished' -t 'GitHub Copilot'",
        "timeoutSec": 5
      }
    ]
  }
}
```

## Building

Requires Visual Studio 2022 with C++ workload.

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\Release\toasty.exe`

## License

MIT
