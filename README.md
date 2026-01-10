# Toasty

<img src="toasty.png" alt="Toasty Icon" width="128" height="128">

A tiny Windows toast notification CLI. 229 KB, no dependencies.

## Quick Start

```cmd
toasty "Hello World" -t "Toasty"
```

That's it. Toasty auto-registers on first run.

## Usage

```
toasty <message> [options]

Options:
  -t, --title <text>   Set notification title (default: "Notification")
  -h, --help           Show this help
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
