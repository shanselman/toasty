# Implementation Plan: --install Feature

## Overview
Add `toasty --install` to auto-detect AI CLI agents and configure their hooks to show toast notifications.

## Supported Agents

| Agent | Config Path | Hook Event | Scope |
|-------|-------------|------------|-------|
| Claude Code | `~/.claude/settings.json` | `Stop` | User |
| Gemini CLI | `~/.gemini/settings.json` | `AfterAgent` | User |
| GitHub Copilot | `.github/hooks/toasty.json` | `sessionEnd` | Repo |
| OpenCode | `~/.config/opencode/plugin/toasty.js` | `session.idle` | User |

---

## Phase 1: Detection

### 1.1 Detect Claude Code
- Check if `%USERPROFILE%\.claude` folder exists
- Or check if `claude` command is in PATH

### 1.2 Detect Gemini CLI
- Check if `%USERPROFILE%\.gemini` folder exists
- Or check if `gemini` command is in PATH

### 1.3 Detect GitHub Copilot CLI
- Check if `.github` folder exists in current directory
- Check if `gh copilot` extension is installed (`gh extension list`)

### 1.4 Detect OpenCode
- Check if `%USERPROFILE%\.config\opencode` folder exists
- Or check if `opencode` command is in PATH

---

## Phase 2: Config Manipulation

### 2.1 JSON Reading
- Use a JSON library (nlohmann/json or similar)
- Or use Windows JSON APIs (JsonObject from WinRT)
- Read existing config, preserve other settings

### 2.2 JSON Writing
- Merge our hook config with existing hooks
- Don't duplicate if already installed
- Pretty-print output

### 2.3 Config Formats

**Claude Code** (`~/.claude/settings.json`):
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

**Gemini CLI** (`~/.gemini/settings.json`):
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

**GitHub Copilot** (`.github/hooks/toasty.json`):
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

**OpenCode** (`~/.config/opencode/plugin/toasty.js`):
```javascript
export const ToastyPlugin = async ({ $ }) => {
  const toastyPath = `C:/path/to/toasty.exe`

  return {
    event: async ({ event }) => {
      if (event.type === "session.idle") {
        await $`${toastyPath} "OpenCode finished" -t "OpenCode"`
      }
    },
  }
}
```

---

## Phase 3: CLI Interface

### 3.1 Commands
```
toasty --install              # Auto-detect and install all found
toasty --install claude       # Install only for Claude
toasty --install gemini       # Install only for Gemini
toasty --install copilot      # Install only for Copilot
toasty --install opencode     # Install only for OpenCode
toasty --uninstall            # Remove hooks from all
toasty --status               # Show what's installed
```

### 3.2 Output
```
Detecting AI CLI agents...
  ✓ Claude Code found
  ✓ Gemini CLI found
  ✗ GitHub Copilot CLI not found (not in a git repo)
  ✓ OpenCode found

Installing toasty hooks...
  ✓ Claude Code: Added Stop hook to ~/.claude/settings.json
  ✓ Gemini CLI: Added AfterAgent hook to ~/.gemini/settings.json
  ✓ OpenCode: Added session.idle event hook to ~/.config/opencode/plugin/toasty.js

Done! You'll get notifications when AI agents finish.
```

---

## Phase 4: Build System

### 4.1 JSON Library
Options:
1. **nlohmann/json** - Header-only, easy to integrate
2. **WinRT JsonObject** - Already using WinRT, no extra deps
3. **RapidJSON** - Fast, header-only

Recommendation: Use WinRT `Windows.Data.Json` since we're already using WinRT.

### 4.2 CMake Changes
- May need to add JSON library if not using WinRT
- Update build for new source files if splitting

---

## Phase 5: Testing

### 5.1 Test Cases
- [ ] Fresh install (no existing config)
- [ ] Existing config with other settings (preserve them)
- [ ] Existing config with other hooks (merge, don't overwrite)
- [ ] Already installed (don't duplicate)
- [ ] Uninstall removes only our hook
- [ ] Invalid JSON in existing config (error gracefully)

### 5.2 Manual Testing
- Install for Claude, verify notification works
- Install for Gemini, verify notification works
- Install for Copilot, verify notification works

---

## Implementation Order

1. **Add JSON support** - WinRT JsonObject or nlohmann/json
2. **Implement detection** - Check for each agent's config folder
3. **Implement Claude install** - Simplest, we know it works
4. **Implement Gemini install** - Same format as Claude
5. **Implement Copilot install** - Different format, repo-level
6. **Add --uninstall** - Remove our hooks
7. **Add --status** - Show current state
8. **Update help text** - Document new commands
9. **Test all combinations**
10. **Update README**

---

## Open Questions

1. **Self-path detection**: How does toasty know its own exe path for the hook command?
   - Use `GetModuleFileName()` to get current exe path
   - Store absolute path in hook config

2. **Copilot repo-level**: Should we warn user this only affects current repo?
   - Yes, print clear message about scope

3. **Backup configs?**: Should we backup before modifying?
   - Good idea: copy to `.json.bak` before first modification

4. **Cross-platform paths**: Copilot needs both bash and powershell paths
   - Use forward slashes or escape backslashes appropriately
