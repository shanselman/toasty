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

Options:
  -t, --title <text>    Set notification title (default: "Notification")
  -s, --style <type>    Set visual style: success, error, warning
  -h, --help            Show this help
```

## Visual Styles

The `--style` flag adds visual distinction to your notifications:

```cmd
toasty "Build passed" --style success    # ✅ Green checkmark + default scenario
toasty "Tests failed" --style error      # ❌ Red X + urgent scenario (stays longer)
toasty "Deprecation notice" --style warning  # ⚠️ Warning icon + default scenario
```

Styles affect:
- **Emoji**: Each style adds a distinctive emoji to the title
- **Scenario**: Error notifications use the "urgent" scenario (longer display time)
- **Visual clarity**: Makes it easy to spot success/failure at a glance

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
