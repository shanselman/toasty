# Toasty

A tiny Windows toast notification CLI. 229 KB, no dependencies.

## Quick Start

```cmd
toasty "Hello World" -t "Toasty"
```

That's it. Toasty auto-registers on first run.

## Action Buttons

Add clickable buttons to your notifications:

```cmd
toasty "Build complete" --button-path "Open Folder" "C:\build"
toasty "Deploy done" --button-url "View Logs" "https://example.com/logs"
```

Buttons use Windows protocol handlers (http/https for URLs, file:/// for paths), so they work without complex COM activation.

## Usage

```
toasty <message> [options]

Options:
  -t, --title <text>              Set notification title (default: "Notification")
  --button-url <label> <url>      Add button that opens URL
  --button-path <label> <path>    Add button that opens folder/file
  -h, --help                      Show this help
```

### Examples

Basic notification:
```cmd
toasty "Hello World" -t "Toasty"
```

With action buttons:
```cmd
toasty "Build complete" --button-path "Open Folder" "C:\build"
toasty "Deploy done" --button-url "View Docs" "https://example.com"
toasty "Task done" -t "CI/CD" --button-url "GitHub" "https://github.com" --button-path "Logs" "C:\logs"
```

## Claude Code Integration

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
            "timeout": 5
          }
        ]
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
