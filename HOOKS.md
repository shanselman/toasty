# AI CLI Hooks Reference

Toasty can be triggered by any hook event from Claude Code, Gemini CLI, or GitHub Copilot CLI. This guide documents all available hooks so you can customize when you get notifications.

## Quick Reference

| Use Case | Claude Code | Gemini CLI | GitHub Copilot |
|----------|-------------|------------|----------------|
| Task finished | `Stop` | `AfterAgent` | `sessionEnd` |
| Needs approval | `PermissionRequest` | — | — |
| Error occurred | — | — | `errorOccurred` |
| Session ended | `SessionEnd` | `SessionEnd` | `sessionEnd` |

## Claude Code Hooks

Location: `~/.claude/settings.json` (user) or `.claude/settings.json` (project)

| Event | Description | Notification Ideas |
|-------|-------------|-------------------|
| `SessionStart` | Session begins or resumes | — |
| `UserPromptSubmit` | User submits a prompt | — |
| `PreToolUse` | Before any tool runs (Bash, Edit, etc.) | — |
| `PostToolUse` | After a tool completes successfully | — |
| `PostToolUseFailure` | After a tool fails | "Claude tool failed" |
| `PermissionRequest` | When Claude asks for permission | "Claude needs approval" |
| `Stop` | Claude finishes responding | "Claude finished" ✓ |
| `SubagentStart` | Subagent spawned | — |
| `SubagentStop` | Subagent finished | — |
| `SessionEnd` | Session terminates | "Claude session ended" |
| `Notification` | System notifications | Varies |
| `PreCompact` | Before context compaction | — |

### Example: Multiple Hooks

```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "toasty.exe \"Claude finished\"",
            "timeout": 5000
          }
        ]
      }
    ],
    "PermissionRequest": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "toasty.exe \"Claude needs approval\" -t \"Action Required\"",
            "timeout": 5000
          }
        ]
      }
    ]
  }
}
```

### Using Matchers

For tool-specific hooks, use `matcher` to filter by tool name:

```json
{
  "hooks": {
    "PostToolUse": [
      {
        "matcher": "Bash",
        "hooks": [
          {
            "type": "command",
            "command": "toasty.exe \"Command finished\""
          }
        ]
      }
    ]
  }
}
```

Tool names: `Bash`, `Edit`, `MultiEdit`, `Write`, `Read`, `Glob`, `Grep`, `LS`, `WebFetch`, etc.

---

## Gemini CLI Hooks

Location: `~/.gemini/settings.json`

**Important:** You must enable hooks in settings:

```json
{
  "hooks": {
    "enabled": true
  }
}
```

| Event | Description | Notification Ideas |
|-------|-------------|-------------------|
| `SessionStart` | Session begins | — |
| `SessionEnd` | Session ends | "Gemini session ended" |
| `BeforeAgent` | After prompt, before planning | — |
| `AfterAgent` | Agent loop finishes | "Gemini finished" ✓ |
| `BeforeModel` | Before LLM call | — |
| `AfterModel` | After LLM response | — |
| `BeforeToolSelection` | Before tool choice | — |
| `Notification` | System notifications | Varies |
| `PreCompress` | Before history compression | — |

> ⚠️ **Note:** `AfterAgent` does not fire if the response is text-only (no tool calls were made).

### Example: Multiple Hooks

```json
{
  "hooks": {
    "enabled": true,
    "AfterAgent": [
      {
        "matcher": "*",
        "hooks": [
          {
            "name": "toasty-finished",
            "type": "command",
            "command": "toasty.exe \"Gemini finished\""
          }
        ]
      }
    ],
    "SessionEnd": [
      {
        "matcher": "*",
        "hooks": [
          {
            "name": "toasty-session-end",
            "type": "command",
            "command": "toasty.exe \"Gemini session ended\""
          }
        ]
      }
    ]
  }
}
```

---

## GitHub Copilot CLI Hooks

Location: `.github/hooks/*.json` (per-repo)

| Event | Description | Notification Ideas |
|-------|-------------|-------------------|
| `sessionStart` | Session begins or resumes | — |
| `sessionEnd` | Session ends | "Copilot finished" ✓ |
| `userPromptSubmitted` | User submits a prompt | — |
| `preToolUse` | Before any tool runs | — |
| `postToolUse` | After a tool completes | — |
| `errorOccurred` | On errors | "Copilot error!" |

### Example: Multiple Hooks

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
    ],
    "errorOccurred": [
      {
        "type": "command",
        "bash": "toasty 'Copilot error!' -t 'Error'",
        "powershell": "toasty.exe 'Copilot error!' -t 'Error'",
        "timeoutSec": 5
      }
    ]
  }
}
```

### Hook Payloads

Copilot passes JSON data via stdin to your hooks:

**sessionStart:**
```json
{
  "timestamp": 1704614400000,
  "cwd": "/path/to/project",
  "source": "new",
  "initialPrompt": "Create a new feature"
}
```

**sessionEnd:**
```json
{
  "timestamp": 1704618000000,
  "cwd": "/path/to/project",
  "reason": "complete"
}
```

---

## Custom Notification Ideas

Since toasty accepts any message and title, you can create contextual notifications:

```bash
# Different urgency levels
toasty "Task complete" -t "Info"
toasty "Needs your attention" -t "Action Required"  
toasty "Something went wrong" -t "Error"

# With app presets (auto-icon)
toasty "Build passed" --app claude
toasty "Tests failed" --app copilot
toasty "Query done" --app gemini
```

## Manual Installation

If `toasty --install` doesn't fit your needs, copy the examples above and customize:

1. Find the config file for your agent (paths listed above)
2. Add the hook configuration
3. Adjust the message text and events to your preference

## Resources

- [Claude Code Hooks Docs](https://docs.anthropic.com/en/docs/claude-code/hooks)
- [Gemini CLI Hooks Docs](https://geminicli.com/docs/hooks/)
- [GitHub Copilot Hooks Docs](https://docs.github.com/en/copilot/reference/hooks-configuration)
