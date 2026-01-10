# Toasty

A tiny Windows toast notification CLI. 229 KB, no dependencies.

## Usage

```
toasty <message> [options]

Options:
  -t, --title <text>   Set notification title (default: "Notification")
  -h, --help           Show this help
  --register           Register app for notifications (run once)
```

## Quick Start

```cmd
toasty --register
toasty "Hello World" -t "Toasty"
```

## Claude Code Integration

Add to `~/.claude/settings.json`:

```json
{
  "hooks": {
    "Stop": [{
      "type": "command",
      "command": "C:\\path\\to\\toasty.exe \"Claude finished\" -t \"Claude Code\""
    }]
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
