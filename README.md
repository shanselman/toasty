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
  --global             With --install copilot: install user-global hook (~/.copilot/hooks/) instead of per-repo
  --uninstall          Remove hooks from all AI CLI agents
  --status             Show installation status
  --dry-run            Show what would happen without executing side effects
```

## Supported AI Agents

| Agent | Icon | Auto-Detect | `--install` | Hook Type | Config Location |
|-------|:----:|:-----------:|:-----------:|-----------|-----------------|
| Claude Code | ✅ | ✅ | ✅ | `Stop` | `~/.claude/settings.json` |
| GitHub Copilot | ✅ | ✅ | ✅ | `sessionEnd` | `.github/hooks/toasty.json` (repo) or `~/.copilot/hooks/toasty.json` (global, via `--global`) |
| Gemini CLI | ✅ | ✅ | ✅ | `AfterAgent` | `~/.gemini/settings.json` |
| OpenAI Codex | ✅ | ✅ | ✅ | `notify` | `~/.codex/config.toml` |

- **Icon**: Built-in icon for toast notifications
- **Auto-Detect**: Toasty recognizes the agent's process and applies the preset automatically
- **`--install`**: `toasty --install` can automatically configure the agent's hook

> Don't see your agent? Any CLI tool with a hook/notification mechanism can call toasty directly.

## Auto-Detection

Toasty automatically detects when it's called from a known AI CLI tool and applies the appropriate icon and title. No flags needed!

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

### Auto-Install

```cmd
# Install for all detected agents
toasty --install

# Install for specific agent
toasty --install claude
toasty --install gemini
toasty --install copilot
toasty --install copilot --global   # install once for all repos (~/.copilot/hooks/)

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

Add to `.github/hooks/toasty.json` (per-repo) **or** `~/.copilot/hooks/toasty.json`
(user-global, applies in every directory you run Copilot CLI from):

```json
{
  "version": 1,
  "hooks": {
    "sessionEnd": [
      {
        "type": "command",
        "bash": "toasty --copilot-hook end",
        "powershell": "toasty.exe --copilot-hook end",
        "timeoutSec": 5
      }
    ],
    "userPromptSubmitted": [
      {
        "type": "command",
        "bash": "toasty --copilot-hook prompt",
        "powershell": "toasty.exe --copilot-hook prompt",
        "timeoutSec": 5
      }
    ]
  }
}
```

When installed via `toasty --install copilot`, three hooks are configured:
`sessionEnd`, `userPromptSubmitted`, **and `postToolUse`** at the repo level
(`.github/hooks/toasty.json`). Pass `--global` to install once for your entire
user account (`~/.copilot/hooks/toasty.json`, which on Windows is
`%USERPROFILE%\.copilot\hooks\toasty.json`) so you get notifications regardless
of which directory you launch Copilot CLI from. `toasty --uninstall` removes
both locations.

Pass `--no-session-end` if you don't want a toast when sessions end (`/exit`
or Ctrl+C) — only the per-turn idle watchdog and the silent prompt-cache
hooks will be installed. Re-running with the flag also strips a previously
installed `sessionEnd` entry; re-running without it restores it.

### Per-turn idle notifications (workaround)

Copilot CLI does **not** emit a hook when a single turn completes within an
interactive session — `sessionEnd` only fires on `/exit` or Ctrl+C. As a
workaround, toasty installs a `postToolUse` hook that arms a debounced **idle
watchdog**: every tool call refreshes a "fire toast in N seconds" timer. If
Copilot stops calling tools for N seconds, you get a toast like:

> *GitHub Copilot - my-cool-session (ready) - "Refactor the auth module" - myrepo*

Tunable via `TOASTY_COPILOT_IDLE_SEC` (default 6 seconds, range 1-3600).
The watchdog auto-cancels when you submit your next prompt or exit the
session. Caveat: turns where Copilot replies without using any tool won't
trigger it. Track upstream issue
[`github/copilot-cli#1128`](https://github.com/github/copilot-cli/issues/1128)
for a proper `awaitingUserInput` hook.

When the hook payload includes a `sessionId` (newer Copilot CLI builds),
toasty reads `~/.copilot/session-state/<id>/workspace.yaml` and weaves the
session `name` (or `summary`) into the toast title — handy for
distinguishing notifications from multiple concurrent Copilot sessions.

**Click to focus the terminal.** Toasts emitted by toasty embed the
terminal window's HWND in the activation URL
(`toasty://focus?hwnd=<n>`). Clicking the toast brings that exact window
to the foreground — so even with several Copilot sessions running across
different windows, each notification routes you back to the right one.
For the per-turn idle watchdog, the HWND is captured at hook time (while
Copilot is still your direct child) and stashed in the pending record so
the detached watchdog can pass it to the toast.

> Caveat for Windows Terminal users with multiple tabs in one window:
> only the *window* can be focused, not the specific tab inside it. WT
> doesn't expose a public API for cross-process tab targeting. If you
> switch tabs after starting a Copilot session, the click will land on
> whatever tab is currently active in that window.

The `--copilot-hook` modes read the JSON payload Copilot pipes to
stdin (`cwd`, `reason`, `prompt`, `sessionId`), so the toast that fires at
session end shows your task and folder, e.g.:

- *GitHub Copilot - my-cool-session — "Refactor the auth module" - myrepo*
- *GitHub Copilot - my-cool-session (timed out)*
- *GitHub Copilot - failed* (when no `sessionId` present)

The `userPromptSubmitted` hook silently caches the latest prompt per-cwd
under `%LOCALAPPDATA%\toasty\copilot-prompts\` so `sessionEnd` can show it.
Cached prompts are deleted on read and any leftovers older than 24 hours are
swept on the next prompt.

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
