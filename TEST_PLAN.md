# Toasty Test Plan

This document outlines the comprehensive testing strategy for Toasty, including both Unit Tests and Integration Tests as requested in the issue.

## Executive Summary

Toasty is a Windows toast notification CLI tool. This test plan provides:
1. **Unit Test Suite** - Testing individual functions and components in isolation
2. **Integration Test Suite** - Testing complete end-to-end workflows
3. **CI/CD Integration** - Automated testing via GitHub Actions

## Testing Philosophy

### Goals
- Ensure core functionality works correctly across different scenarios
- Catch regressions early in development
- Validate edge cases and error handling
- Enable confident refactoring and feature additions

### Scope
- **In Scope**: Utility functions, preset detection, JSON parsing, command-line parsing, hook installation logic
- **Out of Scope**: Windows API internals, actual toast display (requires GUI), network operations

## Unit Test Suite

### Framework
- **Google Test (gtest)** - Industry-standard C++ testing framework
- Automatically downloaded via CMake FetchContent
- Fast, parallel execution
- Rich assertion library

### Test Coverage

#### 1. Utility Functions (`test_utilities.cpp`)

**to_lower() function**
- Test case: Basic lowercase conversion
- Test case: Already lowercase strings
- Test case: Mixed case strings
- Test case: Empty strings
- Test case: Strings with numbers and special characters

**escape_xml() function**
- Test case: Basic text (no escaping needed)
- Test case: Ampersand escaping (`&` → `&amp;`)
- Test case: All XML entities (`<`, `>`, `"`, `'`, `&`)
- Test case: Empty strings
- Test case: Multiple consecutive entities

**escape_json_string() function**
- Test case: Basic text (no escaping needed)
- Test case: Backslash escaping (`\` → `\\`)
- Test case: Quote escaping (`"` → `\"`)
- Test case: Control characters (`\n`, `\r`, `\t`)
- Test case: Empty strings
- Test case: Combined escaping scenarios

**expand_env() function**
- Test case: Plain paths (no variables)
- Test case: Windows environment variables (`%TEMP%`, `%USERPROFILE%`)
- Test case: Empty strings

**path_exists() function**
- Test case: Existing paths (e.g., temp directory)
- Test case: Non-existent paths

#### 2. Preset Detection (`test_presets.cpp`)

**find_preset() function**
- Test case: Exact name match (lowercase)
- Test case: Case-insensitive matching (`CLAUDE` → `claude`)
- Test case: All valid presets (claude, copilot, gemini, codex, cursor)
- Test case: Unknown preset names
- Test case: Empty preset name
- Test case: Verify icon resource IDs are correct

#### 3. JSON Hook Handling (`test_json_hooks.cpp`)

**has_toasty_hook() function**
- Test case: Direct command format
- Test case: Nested hooks array (Claude/Gemini format)
- Test case: No toasty in hooks
- Test case: Empty hooks array
- Test case: Multiple commands with toasty in one
- Test case: Case-insensitive detection

### Running Unit Tests

```cmd
# Build tests
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target toasty_tests

# Run tests
cd build
ctest -C Release --output-on-failure
```

## Integration Test Suite

### Framework
- **PowerShell** - Native Windows scripting
- Custom test runner with colored output
- Comprehensive error reporting

### Test Coverage

#### 1. Command-Line Interface
- **Help Command** - Verify `--help` displays usage information
- **Status Command** - Verify `--status` shows installation status
- **Invalid Arguments** - Verify proper error handling

#### 2. Preset Validation
- **All Presets** - Test each of 5 presets (claude, copilot, gemini, codex, cursor)
- **Invalid Preset** - Verify error message for unknown presets
- **Case Sensitivity** - Verify presets work regardless of case

#### 3. Hook Installation (Future Enhancement)
- Install hooks for individual agents
- Install hooks for all agents
- Verify JSON file creation
- Verify JSON structure and content
- Verify backup file creation

#### 4. Hook Uninstallation (Future Enhancement)
- Remove hooks from individual agents
- Remove all hooks
- Verify cleanup and restoration

#### 5. Registration (Future Enhancement)
- Verify shortcut creation
- Verify protocol handler registration
- Verify re-registration works

### Running Integration Tests

```powershell
# Build toasty first
cmake --build build --config Release

# Run integration tests
cd tests\integration
.\run_integration_tests.ps1

# Or with custom path
.\run_integration_tests.ps1 -ToastyPath "C:\path\to\toasty.exe"
```

## Test Data Management

### Mock Data
- Test configurations stored in `tests/integration/test_data/`
- Sample hook JSON files
- Mock agent configuration directories

### Cleanup Strategy
- Integration tests use temporary directories (`%TEMP%`)
- Automatic cleanup after successful test run
- Manual cleanup instructions for failed tests
- Backup files (`.bak`) created before modifications

## Continuous Integration

### GitHub Actions Workflow

The `.github/workflows/test.yml` file defines automated testing:

```yaml
on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main, develop ]
```

**Build Matrix:**
- Windows Latest
- Visual Studio 2022
- x64 architecture

**Test Stages:**
1. Configure with CMake
2. Build toasty.exe
3. Build test suite
4. Run unit tests
5. Run integration tests
6. Upload artifacts

### Artifacts
- Test results (XML reports from CTest)
- Built executable (toasty.exe)
- Test logs

## Test Execution Strategy

### During Development
1. Run relevant unit tests after code changes
2. Run full unit test suite before committing
3. Run integration tests before creating PR

### Before Release
1. Full unit test suite (all configurations)
2. Full integration test suite
3. Manual testing of key workflows
4. Verification on clean Windows installation

## Code Coverage Goals

While not implemented yet, future coverage goals:
- **Unit Tests**: 80%+ coverage of testable functions
- **Integration Tests**: 100% coverage of user-facing commands

## Edge Cases and Error Conditions

### Handled in Tests
- Empty strings / null values
- Special characters (XML entities, JSON escaping)
- Very long strings
- Invalid file paths
- Missing configuration files
- Malformed JSON
- Non-existent presets
- Registry access failures (graceful degradation)

### Known Limitations
- Toast display requires GUI environment (tested manually)
- Protocol handler activation requires user interaction
- Some Windows APIs can't be easily mocked

## Future Enhancements

### Potential Additions
1. **Performance Tests** - Measure startup time, toast display latency
2. **Stress Tests** - Rapid-fire notifications, large JSON files
3. **Regression Test Suite** - Automated tests for previously fixed bugs
4. **Code Coverage Analysis** - Integration with coverage tools
5. **Cross-Version Testing** - Test on Windows 10 vs 11

### Tools to Consider
- **OpenCppCoverage** - Code coverage for C++
- **AddressSanitizer** - Memory error detection
- **valgrind (Wine)** - Memory leak detection

## Maintenance

### When to Update Tests
- Adding new features → Add corresponding tests first (TDD)
- Fixing bugs → Add regression test
- Refactoring → Ensure existing tests still pass
- Changing command-line interface → Update integration tests

### Test Review Process
- All new tests reviewed in PR
- Tests must pass before merge
- Failing tests block merges
- Test failures investigated promptly

## Success Metrics

### Unit Tests
- ✅ All tests pass
- ✅ Fast execution (< 5 seconds total)
- ✅ No flaky tests
- ✅ Clear failure messages

### Integration Tests
- ✅ All tests pass
- ✅ Reasonable execution time (< 30 seconds)
- ✅ Environment cleanup successful
- ✅ Clear test output

### CI/CD
- ✅ Automated on every PR
- ✅ Fast feedback (< 5 minutes)
- ✅ Reliable (no false failures)

## Conclusion

This test plan provides a solid foundation for ensuring Toasty's quality and reliability. The combination of fast unit tests and comprehensive integration tests gives confidence that the application works correctly across various scenarios while maintaining quick feedback loops for developers.

The testing infrastructure is designed to grow with the project - new tests can be easily added as features are developed, and the CI/CD integration ensures consistent quality over time.
