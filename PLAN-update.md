# Implementation Plan: --update Feature

## Overview
Added `toasty --update` to check for new versions on GitHub and automatically download and install updates.

## Features Implemented

### 1. Version Management
- **Current Version**: Defined as constant `VERSION = "0.3.0"` in main.cpp
- **Version Parsing**: Semantic version parser that handles "v0.3.0" and "0.3.0" formats
- **Version Comparison**: Compares major.minor.patch to determine if update is available

### 2. GitHub Integration
- **API Query**: Uses WinINet to query GitHub Releases API
- **Endpoint**: `https://api.github.com/repos/shanselman/toasty/releases/latest`
- **JSON Parsing**: Uses WinRT JsonObject to parse response and extract tag_name

### 3. Architecture Detection
- **Auto-Detection**: Determines if running on x64 or ARM64
- **Download Selection**: Downloads appropriate binary (toasty-x64.exe or toasty-arm64.exe)

### 4. Download & Installation
- **Download**: Uses WinINet to download binary from GitHub releases
- **Temp Storage**: Downloads to temp directory first
- **Self-Replace**: Uses batch script to replace running executable after exit

### 5. User Interface
- `toasty --version` - Shows current version
- `toasty --update` - Checks for updates and installs if available

## Implementation Details

### Version Comparison Logic
```cpp
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    
    bool operator>(const Version& other) const {
        if (major != other.major) return major > other.major;
        if (minor != other.minor) return minor > other.minor;
        return patch > other.patch;
    }
};
```

### Update Flow
1. User runs `toasty --update`
2. Query GitHub API for latest release
3. Parse version from tag_name
4. Compare with current version
5. If newer version available:
   - Download appropriate binary for architecture
   - Save to temp directory
   - Create batch script to replace exe after exit
   - Launch batch script and exit

### Self-Update Mechanism
Uses a batch file that:
1. Waits 1 second for toasty.exe to exit
2. Moves downloaded file to replace current exe
3. Deletes itself

```batch
@echo off
timeout /t 1 /nobreak >nul
move /y "temp_file" "current_exe"
del "%~f0"
```

## Testing Checklist

### Version Checking
- [ ] `toasty --version` shows current version correctly
- [ ] Version parsing handles "v0.3.0" format
- [ ] Version parsing handles "0.3.0" format
- [ ] Version comparison works correctly (0.3.0 < 0.4.0)
- [ ] Version comparison handles equal versions
- [ ] Version comparison handles current > latest

### Network Operations
- [ ] API query works with internet connection
- [ ] API query fails gracefully without internet
- [ ] JSON parsing handles valid response
- [ ] JSON parsing handles invalid/malformed response
- [ ] Download works for valid URLs
- [ ] Download fails gracefully for invalid URLs

### Architecture Detection
- [ ] Correctly detects x64 architecture
- [ ] Correctly detects ARM64 architecture
- [ ] Downloads correct binary for architecture

### Update Process
- [ ] Update check displays current and latest version
- [ ] Update check shows "already up to date" when current
- [ ] Update downloads correct file
- [ ] Batch script is created correctly
- [ ] Batch script executes after exit
- [ ] Exe is replaced successfully
- [ ] Old exe is not left behind

### Error Handling
- [ ] Network errors display helpful message
- [ ] Download failures are reported
- [ ] File write errors are handled
- [ ] Permissions errors are handled

## Known Limitations

1. **Windows Only**: Uses WinINet and Windows-specific APIs
2. **Requires Internet**: No offline update capability
3. **GitHub Dependency**: Relies on GitHub API and releases
4. **No Rollback**: Can't undo an update
5. **No Verification**: Doesn't verify downloaded file integrity
6. **Batch Script Dependency**: Update relies on batch script execution

## Future Enhancements

1. **Digital Signature Verification**: Verify downloaded exe is signed
2. **SHA256 Checksum**: Verify file integrity
3. **Progress Bar**: Show download progress
4. **Automatic Updates**: Option to check for updates on startup
5. **Update Notifications**: Toast notification when update available
6. **Rollback**: Keep backup of previous version
7. **Silent Updates**: Background update option

## Version History

- v0.3.0 - Added --update and --version options
