# XML Output Examples

This document shows the XML that toasty generates for various command-line options.

## Basic Toast

Command:
```cmd
toasty "Hello World" -t "Test"
```

Generated XML:
```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>Test</text>
      <text>Hello World</text>
    </binding>
  </visual>
</toast>
```

## With Hero Image

Command:
```cmd
toasty "Deploy complete" --hero-image C:\images\success.jpg
```

Generated XML:
```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>Notification</text>
      <text>Deploy complete</text>
      <image placement="hero" src="file:///C:/images/success.jpg"/>
    </binding>
  </visual>
</toast>
```

## With Scenario

Command:
```cmd
toasty "Meeting soon" --scenario reminder
```

Generated XML:
```xml
<toast scenario="reminder">
  <visual>
    <binding template="ToastGeneric">
      <text>Notification</text>
      <text>Meeting soon</text>
    </binding>
  </visual>
</toast>
```

## With Attribution

Command:
```cmd
toasty "Build done" --attribution "via CI/CD"
```

Generated XML:
```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>Notification</text>
      <text>Build done</text>
      <text placement="attribution">via CI/CD</text>
    </binding>
  </visual>
</toast>
```

## With Progress Bar

Command:
```cmd
toasty "Building..." --progress "Build:0.75:75%"
```

Generated XML:
```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>Notification</text>
      <text>Building...</text>
      <progress title="Build" value="0.75" status="75%"/>
    </binding>
  </visual>
</toast>
```

## With Audio

Command:
```cmd
toasty "New message" --audio im
```

Generated XML:
```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>Notification</text>
      <text>New message</text>
    </binding>
  </visual>
  <audio src="ms-winsoundevent:Notification.IM"/>
</toast>
```

## With Silent Audio

Command:
```cmd
toasty "Silent update" --audio silent
```

Generated XML:
```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>Notification</text>
      <text>Silent update</text>
    </binding>
  </visual>
  <audio silent="true"/>
</toast>
```

## All Features Combined

Command:
```cmd
toasty "Task complete" -t "MyApp" --scenario urgent --hero-image C:\icons\app.png --attribution "via Automation" --progress "Task:1.0:Done" --audio default
```

Generated XML:
```xml
<toast scenario="urgent">
  <visual>
    <binding template="ToastGeneric">
      <text>MyApp</text>
      <text>Task complete</text>
      <image placement="hero" src="file:///C:/icons/app.png"/>
      <text placement="attribution">via Automation</text>
      <progress title="Task" value="1.0" status="Done"/>
    </binding>
  </visual>
  <audio src="ms-winsoundevent:Notification.Default"/>
</toast>
```

## XML Escaping

Command with special characters:
```cmd
toasty "5 > 3 & 2 < 4" -t "Math & Logic"
```

Generated XML:
```xml
<toast>
  <visual>
    <binding template="ToastGeneric">
      <text>Math &amp; Logic</text>
      <text>5 &gt; 3 &amp; 2 &lt; 4</text>
    </binding>
  </visual>
</toast>
```

## Audio Options

| Command Option | Audio Source |
|---------------|--------------|
| `--audio default` | `ms-winsoundevent:Notification.Default` |
| `--audio im` | `ms-winsoundevent:Notification.IM` |
| `--audio mail` | `ms-winsoundevent:Notification.Mail` |
| `--audio reminder` | `ms-winsoundevent:Notification.Reminder` |
| `--audio sms` | `ms-winsoundevent:Notification.SMS` |
| `--audio loopingAlarm` | `ms-winsoundevent:Notification.Looping.Alarm` |
| `--audio loopingCall` | `ms-winsoundevent:Notification.Looping.Call` |
| `--audio silent` | `<audio silent="true"/>` |

## Scenario Options

| Scenario | Behavior |
|----------|----------|
| `reminder` | For reminders and calendar events |
| `alarm` | Persistent, looping notification |
| `incomingCall` | Full-screen on mobile, designed for calls |
| `urgent` | High priority, stays on screen longer |

## Notes

- File paths are automatically converted to `file:///` URIs with forward slashes
- All text content is XML-escaped to handle special characters
- Features can be combined in any combination
- Order of elements in XML follows the specification for proper rendering
