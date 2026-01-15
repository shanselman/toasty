# Update Feature Implementation Summary

## Overview
This PR implements a self-update mechanism for Toasty, allowing users to check for new versions and automatically download and install updates directly from GitHub releases.

## New Features

### 1. Version Display
```cmd
toasty --version
```
Shows the current version of Toasty (e.g., "toasty version 0.3.0")

### 2. Update Checking and Installation
```cmd
toasty --update
```
- Checks GitHub for the latest release
- Compares it with the current version
- If a newer version is available, downloads and installs it automatically
- Detects the system architecture (x64 or ARM64) and downloads the appropriate binary

## Implementation Details

### Architecture
- **Version Management**: Semantic versioning (major.minor.patch)
- **Update Source**: GitHub Releases API
- **Download**: WinINet HTTP client
- **Installation**: Batch script that replaces the executable after exit

### Key Components

#### 1. Version Constants
```cpp
const wchar_t* VERSION = L"0.3.0";
const wchar_t* GITHUB_REPO_OWNER = L"shanselman";
const wchar_t* GITHUB_REPO_NAME = L"toasty";
```

#### 2. Version Parsing
Parses semantic version strings like "v0.3.0" or "0.3.0":
```cpp
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    
    bool operator>(const Version& other) const;
    bool operator==(const Version& other) const;
};
```

#### 3. GitHub API Integration
Queries the GitHub Releases API:
```
GET https://api.github.com/repos/shanselman/toasty/releases/latest
```

Parses the JSON response to extract the `tag_name` field.

#### 4. Architecture Detection
```cpp
std::wstring get_architecture() {
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    // Returns "x64" or "arm64"
}
```

#### 5. Download and Installation
1. Downloads from: `https://github.com/shanselman/toasty/releases/download/{version}/toasty-{arch}.exe`
2. Saves to temp directory
3. Creates a batch script to replace the running executable
4. Launches the batch script and exits

### Batch Script
```batch
@echo off
echo Waiting for toasty to exit...
timeout /t 2 /nobreak >nul
echo Installing update...
move /y "temp_file" "current_exe" >nul 2>&1
if errorlevel 1 (
  echo Failed to install update. The file may be in use.
  pause
) else (
  echo Update installed successfully!
)
del "%~f0"
```

## User Experience

### Checking Current Version
```
> toasty --version
toasty version 0.3.0
```

### Checking for Updates
```
> toasty --update
Current version: 0.3.0
Checking for updates...
Latest version: v0.4.0
A new version is available!

Downloading update...
Download complete. Installing update...

Update ready to install.
A script will run to complete the installation.
Press any key to continue...
```

After pressing a key, a console window appears briefly showing:
```
Waiting for toasty to exit...
Installing update...
Update installed successfully!
```

### Already Up to Date
```
> toasty --update
Current version: 0.3.0
Checking for updates...
Latest version: v0.3.0
You are already running the latest version.
```

### Network Error
```
> toasty --update
Current version: 0.3.0
Checking for updates...
Error: Could not check for updates. Please check your internet connection.
```

## Code Changes

### Modified Files
1. **main.cpp**
   - Added version constants
   - Added version parsing and comparison functions
   - Added GitHub API query function
   - Added download functionality
   - Added update handling function
   - Updated wmain() to handle --update and --version flags
   - Updated print_usage() to document new options

2. **CMakeLists.txt**
   - Added wininet library dependency

3. **README.md**
   - Added --update and --version to usage documentation

### New Files
1. **PLAN-update.md** - Detailed implementation plan and testing checklist
2. **UPDATE-FEATURE-SUMMARY.md** - This file

## Dependencies
- **WinINet**: For HTTP operations (downloading files and API queries)
- **WinRT JsonObject**: For parsing GitHub API JSON responses
- **Windows Batch Script**: For self-replacement after download

## Limitations and Considerations

### Current Limitations
1. **Windows Only**: Uses Windows-specific APIs (WinINet, batch scripts)
2. **No Verification**: Downloaded files are not verified (no signature or checksum validation)
3. **No Rollback**: Cannot undo an update once installed
4. **Requires Restart**: User must manually run toasty again after update
5. **Network Required**: Cannot update without internet connection

### Security Considerations
- Downloads from official GitHub releases only
- Uses HTTPS for all network operations
- However, lacks digital signature verification (future enhancement)

### Potential Issues
1. **File Locking**: If toasty.exe is locked by another process, the update will fail
2. **Permissions**: May fail if user doesn't have write permissions to the installation directory
3. **Network Timeouts**: Long downloads might timeout (WinINet default timeout applies)

## Future Enhancements

1. **Digital Signature Verification**: Verify downloaded exe is properly signed
2. **SHA256 Checksum**: Download and verify checksums from releases
3. **Progress Indicator**: Show download progress
4. **Background Updates**: Optionally check for updates on startup
5. **Update Notifications**: Show toast notification when update is available
6. **Rollback Support**: Keep backup of previous version
7. **Delta Updates**: Download only changed parts (for large binaries)
8. **Auto-Update**: Silent update option that installs automatically

## Testing

Since this project requires Visual Studio 2022 and Windows to build, the implementation has been done with best practices but requires testing on Windows:

### Test Checklist
- [ ] Build successfully on Windows with VS2022
- [ ] `toasty --version` displays correct version
- [ ] `toasty --update` checks GitHub API successfully
- [ ] Update process detects correct architecture
- [ ] Download works for both x64 and ARM64
- [ ] Batch script successfully replaces executable
- [ ] Error handling works for network failures
- [ ] Error handling works for download failures
- [ ] Works correctly when already up to date
- [ ] Works correctly when running newer version than released

## Maintenance Notes

### Updating the Version
When creating a new release, update the VERSION constant in main.cpp:
```cpp
const wchar_t* VERSION = L"0.4.0";  // Change this
```

This should be done before tagging the release so that built binaries report the correct version.

### Release Process
1. Update VERSION in main.cpp
2. Commit changes
3. Create and push git tag (e.g., `v0.4.0`)
4. GitHub Actions will build and create release
5. Users can then update via `toasty --update`

## Conclusion

This implementation provides a solid foundation for self-updating functionality in Toasty. While it has some limitations, it significantly improves the user experience by making updates easy and automatic. Future enhancements can add additional security and reliability features.
