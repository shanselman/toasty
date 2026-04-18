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

Location: `.github/hooks/*.json` (per-repo) or `~/.copilot/hooks/*.json` (user-global, loaded for every repo). On Windows the global path is `%USERPROFILE%\.copilot\hooks\toasty.json`.

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

### Rich notifications via `--copilot-hook`

`toasty --install copilot` installs three hooks (`sessionEnd`,
`userPromptSubmitted`, and `postToolUse`) that invoke toasty in a special mode
where it reads the JSON Copilot pipes to stdin and builds a richer toast:

- The user prompt is captured on `userPromptSubmitted` and cached per-cwd
  under `%LOCALAPPDATA%\toasty\copilot-prompts\`.
- `sessionEnd` reads `reason` + `cwd` from stdin, looks up the cached prompt,
  and shows e.g. `GitHub Copilot — "Refactor the auth module" - myrepo`. The
  title varies by `reason` (`complete`, `timeout`, `error`, `abort`,
  `user_exit`).
- `postToolUse` arms a debounced **idle watchdog**: every tool call refreshes
  a timer; when no new tool fires for `TOASTY_COPILOT_IDLE_SEC` seconds
  (default 6), a detached watchdog process emits a `GitHub Copilot - ready`
  toast. This is the workaround for Copilot CLI not emitting any per-turn
  completion hook (see [issue #1128](https://github.com/github/copilot-cli/issues/1128)).
  Submitting a new prompt or ending the session cancels the pending toast.
- The cache file is deleted after read, and stale entries (>24h) are swept on
  the next write.
- If stdin is empty (e.g. you ran the command manually) toasty falls back to a
  generic message instead of hanging.
- **Session name in the title.** When the hook payload includes a `sessionId`
  (newer Copilot CLI builds), toasty reads
  `%USERPROFILE%\.copilot\session-state\<id>\workspace.yaml` and uses the
  `name` field (falling back to `summary`, then `repository`) so toasts read
  e.g. `GitHub Copilot - my-cool-session` or
  `GitHub Copilot - my-cool-session (failed)`. If no `sessionId` is provided
  the title stays as `GitHub Copilot`.

### Per-repo vs. global install

Copilot CLI loads hooks from two places:

- `.github/hooks/*.json` — per-repo, only active in that repository.
- `~/.copilot/hooks/*.json` — user-global, active in every directory you run
  Copilot CLI from. On Windows this is `%USERPROFILE%\.copilot\hooks\toasty.json`.

`toasty --install copilot` writes the per-repo file. Add `--global` to write
the user-global file instead:

```powershell
toasty --install copilot --global
```

Both are independent — you can install one or both. `toasty --uninstall`
removes hooks from both locations and `toasty --status` reports each one
separately.

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
