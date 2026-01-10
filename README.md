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
  -t, --title <text>   Set notification title (default: "Notification")
  --app <name>         Use AI CLI preset (claude, copilot, gemini, codex, cursor)
  -i, --icon <path>    Custom icon path (PNG recommended, 48x48px)
  -h, --help           Show this help
```

## AI CLI Presets

Toasty includes built-in presets for popular AI tools with custom icons:

```cmd
# Claude Code
toasty "Analysis complete" --app claude

# GitHub Copilot
toasty "Code review done" --app copilot

# Google Gemini
toasty "Processing finished" --app gemini

# OpenAI Codex
toasty "Generation complete" --app codex

# Cursor IDE
toasty "Build succeeded" --app cursor
```

Each preset includes a distinctive icon and default title.

## Custom Icons

You can also use your own icons:

```cmd
toasty "Task complete" -i "C:\path\to\icon.png"
```

Icons should be 48x48 pixels for best results. PNG format recommended.

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
