# AI CLI Presets Implementation Notes

## Overview

This implementation adds support for AI CLI tool presets with bundled icons to the Toasty notification system. Toasty can automatically detect the parent process and apply the appropriate preset, or users can manually specify a preset.

## Implementation Details

### Icon Resources
- 5 PNG icons (48x48px) embedded as RCDATA resources in the executable
- Icons stored in `/icons` directory and compiled into the exe via resource.rc
- Each icon has a unique resource ID defined in resource.h

### Presets Available
1. **Claude** (IDI_CLAUDE) - Warm brown/gold with "CL" text
2. **Copilot** (IDI_COPILOT) - Blue with "CP" text  
3. **Gemini** (IDI_GEMINI) - Purple with "GM" text
4. **Codex** (IDI_CODEX) - Green with "CX" text
5. **Cursor** (IDI_CURSOR) - Black with "CR" text

### Auto-Detection Feature

Toasty uses the Windows Toolhelp API to detect the parent process that called it:

1. **Process Detection**: `CreateToolhelp32Snapshot()` gets a snapshot of all running processes
2. **Parent Lookup**: Finds the current process PID, then looks up its parent process PID
3. **Name Extraction**: Retrieves the parent process executable name (e.g., "claude.exe")
4. **Preset Matching**: Strips the extension and matches against known preset names (case-insensitive)
5. **Auto-Apply**: If a match is found, automatically applies that preset's icon and title

This means when Claude Code calls `toasty.exe`, it automatically uses the Claude preset without needing `--app claude`.

### Override Behavior

- **`--app` flag**: Explicitly selects a preset, overriding auto-detection
- **`-t` flag**: Overrides the title (whether from auto-detection, `--app`, or default)

### How Icon Embedding Works

1. **Resource Compilation**: The resource.rc file references PNG files which get embedded into the exe at build time
2. **Runtime Extraction**: When a preset is used:
   - `FindResourceW()` locates the embedded PNG data
   - `LoadResource()` and `LockResource()` access the raw bytes
   - Data is written to a temp file: `%TEMP%\toasty_icon_<id>.png`
   - Toast notification XML references this temp file path

### Toast XML Format with Icons

```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <image placement="appLogoOverride" src="C:\Users\...\Temp\toasty_icon_101.png"/>
      <text>Title</text>
      <text>Message</text>
    </binding>
  </visual>
</toast>
```

### CLI Usage

```cmd
# Auto-detection (when called from Claude, Copilot, etc.)
toasty "Task complete"

# Manual preset selection
toasty "Task complete" --app claude

# Custom icon
toasty "Build done" -i "path/to/icon.png"

# Override preset title
toasty "Custom message" --app copilot -t "My Custom Title"
```

## Design Decisions

### Why Temp Files?
Windows Toast API doesn't support embedded resources (res:// protocol) directly. Icons must be:
- File paths on disk
- HTTP/HTTPS URLs  
- ms-appdata:// protocol paths

Extracting to temp directory is the simplest approach for a portable exe.

### Why PNG?
- PNG supports transparency (important for icons)
- Widely supported by Windows Toast API
- Smaller file sizes than BMP
- ICO format not recommended for toast notifications

### Icon Licensing
All icons are original creations (simple text-based designs) to avoid licensing issues.

## Future Enhancements

Potential improvements:
- Add more AI tool presets (ChatGPT, Bard, etc.)
- Support for hero images (larger banners)
- Icon caching to avoid re-extracting on every call
- Custom icon library with more professional designs
