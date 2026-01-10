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

Toasty automatically detects when it's called from a known AI tool and applies the appropriate preset! No need to specify `--app` manually.

**Auto-detected tools:**
- Claude Code / Claude CLI
- GitHub Copilot CLI
- Google Gemini CLI
- OpenAI Codex CLI
- Cursor IDE

When called from these tools, Toasty will automatically use the matching icon and title.

```cmd
# Called from Claude - automatically uses Claude preset
toasty "Analysis complete"

# Called from Copilot - automatically uses Copilot preset
toasty "Code review done"
```

### Manual Preset Selection

You can still manually specify a preset with `--app`:

```cmd
# Explicitly use Claude preset
toasty "Processing finished" --app claude

# Explicitly use Copilot preset
toasty "Build succeeded" --app copilot
```

### Override Title

The `-t` flag overrides any preset title (auto-detected or manual):

```cmd
# Auto-detected preset with custom title
toasty "Task done" -t "My Custom Title"

# Manual preset with custom title
toasty "Task done" --app gemini -t "My Custom Title"
```

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
            "command": "C:\\path\\to\\toasty.exe \"Claude finished\"",
            "timeout": 5
          }
        ]
      }
    ]
  }
}
```

Toasty will automatically detect it's being called from Claude and apply the Claude preset (icon + title). You can still override the title if desired:

```json
"command": "C:\\path\\to\\toasty.exe \"Task complete\" -t \"Custom Title\""
```

## Building

Requires Visual Studio 2022 with C++ workload.

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\Release\toasty.exe`

## Why C++ and Not Rust?

We evaluated implementing Toasty in Rust. While Rust offers excellent memory safety and modern language features, the C++ implementation is optimal for this project's goals:

- **Smaller Binary**: 229 KB vs. estimated 500 KB - 2 MB in Rust
- **Zero Dependencies**: Static linking with no runtime requirements
- **Native Windows API**: C++/WinRT is the first-class way to access Windows Runtime

See [RUST-EVALUATION.md](RUST-EVALUATION.md) for the complete analysis.

## License

MIT
