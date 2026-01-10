# Integration Tests for Toasty

This directory contains integration tests that test the full toasty application end-to-end.

## Prerequisites

- Built toasty.exe (in build/Release or build/Debug)
- PowerShell 5.1 or later
- Windows 10/11

## Running Integration Tests

```powershell
# From the repository root
cd tests/integration
.\run_integration_tests.ps1
```

## Test Coverage

The integration tests cover:

1. **Basic Notification Tests**
   - Simple notification display
   - Notification with custom title
   - Notification with custom icon

2. **Preset Tests**
   - Auto-detection of AI agents
   - Manual preset selection (--app flag)
   - All preset variations (claude, copilot, gemini, codex, cursor)

3. **Hook Installation Tests**
   - Install hooks for individual agents
   - Install hooks for all agents
   - Verify hook file creation
   - Check hook JSON structure

4. **Hook Uninstall Tests**
   - Remove hooks from individual agents
   - Remove all hooks
   - Verify cleanup

5. **Status Command Tests**
   - Status output format
   - Detection reporting
   - Installation status reporting

6. **Registration Tests**
   - App registration (Start Menu shortcut)
   - Protocol handler registration
   - Re-registration

## Test Data

The `test_data/` directory contains:
- Sample hook configuration files
- Test icons
- Mock agent configuration directories

## Notes

- Some tests require administrator privileges (registry access)
- Tests create temporary files in %TEMP%
- Hook installation tests create backup files (.bak)
- Tests clean up after themselves but manual cleanup may be needed if tests fail
