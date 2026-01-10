# Toasty Examples

This document provides examples of using Toasty with Windows 11 toast notification features.

## Basic Usage

Simple notification:
```cmd
toasty "Build completed"
```

With custom title:
```cmd
toasty "Build completed" -t "CI/CD Pipeline"
```

## Hero Images

Add a large banner image at the top of the notification:

```cmd
toasty "Deploy successful!" --hero-image C:\images\success.jpg
```

```cmd
toasty "New message from Alice" --hero-image C:\avatars\alice.png -t "Chat"
```

**Note**: Images must be accessible via file path. The path is automatically converted to a `file:///` URI.

## Scenarios

Control how the notification behaves:

### Reminder
Suitable for calendar events and reminders:
```cmd
toasty "Meeting in 5 minutes" --scenario reminder --audio reminder
```

### Urgent
High priority notification that stays on screen longer:
```cmd
toasty "Critical error detected!" --scenario urgent -t "System Alert"
```

### Alarm
For alarms with looping sound:
```cmd
toasty "Wake up!" --scenario alarm --audio loopingAlarm
```

### Incoming Call
For VoIP or phone calls:
```cmd
toasty "Call from John Doe" --scenario incomingCall --audio loopingCall
```

## Attribution Text

Add small gray text at the bottom to show the source:

```cmd
toasty "Build completed" --attribution "via GitHub Actions"
```

```cmd
toasty "Task finished" -t "Claude Code" --attribution "AI Assistant"
```

## Progress Bars

Show progress with a visual bar. Format: `title:value:status`
- `title`: Description of the task
- `value`: Progress from 0.0 to 1.0
- `status`: Status text (e.g., percentage)

```cmd
toasty "Building project..." --progress "Build:0.5:50%"
```

```cmd
toasty "Downloading files" --progress "Download:0.75:75%" --attribution "via Download Manager"
```

```cmd
toasty "Processing data" --progress "Processing:0.33:Step 1 of 3"
```

## Audio

Choose different notification sounds:

### Default sound
```cmd
toasty "Notification" --audio default
```

### Instant Message sound
```cmd
toasty "New message!" --audio im
```

### Mail sound
```cmd
toasty "You've got mail" --audio mail
```

### Reminder sound
```cmd
toasty "Don't forget!" --audio reminder
```

### SMS sound
```cmd
toasty "Text message received" --audio sms
```

### Looping alarm (continues until dismissed)
```cmd
toasty "Wake up!" --audio loopingAlarm --scenario alarm
```

### Looping call (continues until dismissed)
```cmd
toasty "Incoming call" --audio loopingCall --scenario incomingCall
```

### Silent (no sound)
```cmd
toasty "Silent notification" --audio silent
```

## Combined Features

You can combine multiple features for rich notifications:

### Build notification with progress and attribution
```cmd
toasty "Compiling sources..." --progress "Build:0.60:60%" --attribution "via CI/CD" -t "MyProject"
```

### Urgent alert with hero image
```cmd
toasty "Server offline!" --scenario urgent --hero-image C:\icons\error.png --audio loopingAlarm -t "Monitoring"
```

### Reminder with all features
```cmd
toasty "Team standup" --scenario reminder --audio reminder --attribution "via Calendar" --hero-image C:\icons\calendar.png -t "Meeting Reminder"
```

### Download complete notification
```cmd
toasty "Download finished" --progress "Download:1.0:Complete" --attribution "via Browser" --hero-image C:\downloads\preview.jpg
```

## Integration Examples

### CI/CD Build Status
```cmd
toasty "Build succeeded" --progress "Build:1.0:Complete" --attribution "via Jenkins" -t "MyApp CI"
```

```cmd
toasty "Build failed!" --scenario urgent --audio loopingAlarm --attribution "via Jenkins" -t "MyApp CI"
```

### Development Workflow
```cmd
toasty "Tests passed" --progress "Tests:1.0:247 passed" --attribution "via npm test" -t "Development"
```

### Pomodoro Timer
```cmd
toasty "Break time!" --scenario alarm --audio loopingAlarm -t "Pomodoro"
```

### Download Manager
```cmd
toasty "Downloading..." --progress "Download:0.42:42%" --attribution "via DownloadManager"
```

## Claude Code Integration

Enhanced notification when Claude Code finishes:

```json
{
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "C:\\path\\to\\toasty.exe \"Task completed\" -t \"Claude Code\" --attribution \"AI Assistant\" --audio default",
            "timeout": 5
          }
        ]
      }
    ]
  }
}
```

## Tips

1. **File Paths**: Use absolute paths or paths relative to the current directory
2. **Quotes**: Wrap arguments with spaces in double quotes
3. **Escaping**: XML special characters are automatically escaped
4. **Scenarios**: Choose scenarios that match your use case for best behavior
5. **Audio**: Looping audio requires user interaction to dismiss
6. **Progress**: Value should be between 0.0 and 1.0 (e.g., 0.5 = 50%)
7. **Compatibility**: All features work on Windows 10 Creators Update (build 15063) or later

## Troubleshooting

If notifications don't appear:
1. Run `toasty --register` first
2. Check Windows Settings > System > Notifications
3. Ensure "Toasty" is enabled in notification settings
4. Check that Focus Assist / Do Not Disturb is off
5. Verify image paths are correct and accessible
