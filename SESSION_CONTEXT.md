# Toasty - Session Context for Future Claude Sessions

## What is Toasty?
A Windows CLI tool that shows toast notifications when AI coding agents (Claude Code, Gemini CLI, GitHub Copilot) finish tasks. Key feature: **click the toast to bring your terminal back to foreground**.

## Why It Exists
When using AI CLI agents, you alt-tab away while waiting. The agent finishes but you don't notice. Toasty solves this with:
1. Native Windows toast notification when agent stops
2. Click-to-focus: clicking the toast brings your terminal window back

## Architecture

### Core Components
- `main.cpp` - Single-file C++ using WinRT for toast notifications
- Embedded PNG icons for each AI agent (Claude, Gemini, Copilot, Codex, Cursor)
- `toasty://focus` protocol handler for click-to-focus

### How Click-to-Focus Works
1. When showing a toast, we walk the process tree to find the parent terminal window (Windows Terminal, VS Code, etc.)
2. Save that HWND to registry (`HKCU\Software\Toasty\LastConsoleWindow`)
3. Toast XML includes `activationType="protocol" launch="toasty://focus"`
4. When user clicks toast, Windows launches `toasty.exe --focus`
5. `--focus` reads HWND from registry and brings window to foreground

### Focus Challenges (Windows Security)
Protocol handlers run in a restricted context - Windows blocks random apps from stealing focus. We use multiple techniques:
- `AttachThreadInput()` to borrow focus permission
- `SendInput()` with empty mouse event to simulate user activity
- `SwitchToThisWindow()` works better than `SetForegroundWindow()` for protocol handlers
- `keybd_event()` with ALT key to help with focus

## Hook Formats (Important!)

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
Note: Nested `hooks` array is required!

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
Note: Same nested structure as Claude. Uses `AfterAgent` not `Stop`.

### GitHub Copilot (`.github/hooks/toasty.json` in repo)
```json
{
  "version": 1,
  "hooks": {
    "sessionStart": [{
      "type": "command",
      "bash": "toasty 'Copilot task complete' -t 'GitHub Copilot'",
      "powershell": "D:\\path\\to\\toasty.exe 'Copilot task complete' -t 'GitHub Copilot'",
      "timeoutSec": 5
    }],
    "sessionEnd": [{
      "type": "command",
      "bash": "toasty 'Copilot session ended' -t 'GitHub Copilot'",
      "powershell": "D:\\path\\to\\toasty.exe 'Copilot session ended' -t 'GitHub Copilot'",
      "timeoutSec": 5
    }]
  }
}
```

**Note:** `sessionStart` works with `copilot --resume`, `sessionEnd` does not (known Copilot CLI limitation).

## Auto-Detection
Toasty walks the process tree looking for known parent processes:
- If launched by `claude.exe` -> uses Claude icon/title
- If launched by node with `@google/gemini` in cmdline -> uses Gemini icon/title
- etc.

This means you can just run `toasty "Done"` and it picks the right branding.

## Key Issues We Hit & Fixed

### 1. Hook Settings Not Reloading
**Problem:** Changed hook settings but nothing happened.
**Solution:** Must restart Claude Code to reload `settings.json`. Settings are only read at startup.

### 2. Gemini Hook Format
**Problem:** Gemini CLI wasn't triggering toasty.
**Solution:** Gemini needs the nested `hooks` array structure (same as Claude), not flat format.

### 3. Click-to-Focus Not Working from Hooks
**Problem:** Clicking toast worked when testing manually, but not when triggered from Claude Code hook.
**Cause:** We thought it was Windows security restrictions on protocol handlers, but it was actually just that hook settings weren't reloaded.
**Solution:** Restart Claude Code. The aggressive focus techniques we added (SendInput, SwitchToThisWindow) may help in edge cases anyway.

### 4. Finding the Right Window
**Problem:** `GetConsoleWindow()` returns NULL for GUI subsystem apps, and process tree walking sometimes fails.
**Solution:** Multiple fallbacks:
1. Walk process tree with `CreateToolhelp32Snapshot` to find parent with visible window
2. If that fails, enumerate all windows looking for `CASCADIA_HOSTING_WINDOW_CLASS` (Windows Terminal) or `ConsoleWindowClass`

### 5. GitHub Copilot --resume Not Firing Hooks
**Problem:** `copilot --resume` does not trigger sessionEnd hooks.
**Cause:** Known limitation in GitHub Copilot CLI - sessionEnd hooks don't fire when using --resume or --continue flags.
**Solution:** Install both sessionStart and sessionEnd hooks. sessionStart DOES fire when resuming (including with --resume), ensuring notifications work in all scenarios.

## Version History
- **v0.1** - Basic toast notifications
- **v0.2** - Auto-detection of parent AI agent, embedded icons
- **v0.3** - Click-to-focus feature (protocol handler)
- **v0.4** - Code cleanup (HandleGuard, to_lower), fixed Gemini hook format
- **v0.5** - Improved click-to-focus reliability, removed debug logging
- **v0.6** - Added sessionStart hook for GitHub Copilot --resume support

## Build
```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
# Output: build/Release/toasty.exe
```

## Commands
```bash
toasty "message"                    # Show toast (auto-detects agent)
toasty "message" -t "Title"         # Custom title
toasty "message" --app claude       # Force specific agent branding
toasty --install                    # Install hooks for detected agents
toasty --install claude             # Install hook for specific agent
toasty --uninstall                  # Remove all hooks
toasty --status                     # Show detection/installation status
toasty --register                   # Re-register app (troubleshooting)
toasty --focus                      # Internal: called by protocol handler
```

## Files
- `main.cpp` - All the code
- `resource.h` / `resources.rc` - Icon resources
- `icons/*.png` - Source icons (embedded at compile time)
- `CMakeLists.txt` - Build config

## Testing Tips
1. After changing hook settings, **restart Claude Code**
2. Test toasty directly first: `.\build\Release\toasty.exe "Test" -t "Test"`
3. Click the toast to verify focus works
4. Then test via hook by completing a task

## Owner
Scott Hanselman (@shanselman)
