# Testing Guide for Toasty

This document describes the testing infrastructure for Toasty, including how to build and run both unit tests and integration tests.

## Overview

Toasty has two types of tests:

1. **Unit Tests** - Test individual functions and components in isolation using Google Test (gtest)
2. **Integration Tests** - Test the complete application end-to-end using PowerShell scripts

## Prerequisites

- Visual Studio 2022 with C++ workload
- CMake 3.20 or later
- Windows 10/11
- PowerShell 5.1 or later (for integration tests)

## Building and Running Unit Tests

### Building Tests

The unit tests are built using CMake and Google Test framework. Tests are automatically downloaded and configured by CMake.

```cmd
# Configure the project with tests enabled (default)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build the tests
cmake --build build --config Debug --target toasty_tests

# Or build in Release mode
cmake --build build --config Release --target toasty_tests
```

To disable building tests:

```cmd
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=OFF
```

### Running Unit Tests

After building, you can run the tests using CTest or directly:

```cmd
# Using CTest (recommended)
cd build
ctest -C Debug --output-on-failure

# Or run the test executable directly
.\build\Debug\toasty_tests.exe

# For Release builds
ctest -C Release --output-on-failure
.\build\Release\toasty_tests.exe
```

### Unit Test Coverage

The unit tests cover the following components:

#### Utility Functions (`tests/test_utilities.cpp`)
- `to_lower()` - String case conversion
- `escape_xml()` - XML entity escaping
- `escape_json_string()` - JSON string escaping
- `expand_env()` - Environment variable expansion
- `path_exists()` - File/directory existence checking

#### Preset Detection (`tests/test_presets.cpp`)
- `find_preset()` - Finding AI agent presets by name
- Case-insensitive preset matching
- Icon resource ID validation

#### JSON Hook Handling (`tests/test_json_hooks.cpp`)
- `has_toasty_hook()` - Detecting toasty in hook configurations
- Direct command format detection
- Nested hooks array format (Claude/Gemini)
- Multiple command handling

## Running Integration Tests

Integration tests validate the complete application behavior, including:
- Command-line argument parsing
- Help and status commands
- Preset validation
- Error handling

### Running Integration Tests

```powershell
# Build toasty first
cmake --build build --config Release

# Run integration tests
cd tests\integration
.\run_integration_tests.ps1

# Or specify a custom path to toasty.exe
.\run_integration_tests.ps1 -ToastyPath "C:\custom\path\toasty.exe"
```

### Integration Test Coverage

See `tests/integration/README.md` for detailed information about integration test coverage.

The integration tests include:
- Basic command-line parsing
- Help text validation
- Status command output
- Preset validation (all 5 presets)
- Invalid preset error handling

## Test Structure

```
tests/
├── CMakeLists.txt              # Test build configuration
├── test_main.cpp               # Test entry point
├── toasty_utils.h              # Shared utility functions for testing
├── test_utilities.cpp          # Utility function tests
├── test_presets.cpp            # Preset detection tests
├── test_json_hooks.cpp         # JSON hook parsing tests
└── integration/
    ├── README.md               # Integration test documentation
    └── run_integration_tests.ps1  # PowerShell test runner
```

## Continuous Integration

The test suite is designed to run in CI environments:

```yaml
# Example GitHub Actions workflow
- name: Build and Test
  run: |
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    cmake --build build --config Release --target toasty_tests
    cd build
    ctest -C Release --output-on-failure
```

## Writing New Tests

### Adding Unit Tests

1. Create a new test file in `tests/` directory (e.g., `test_myfeature.cpp`)
2. Include necessary headers:
   ```cpp
   #include <gtest/gtest.h>
   #include "toasty_utils.h"
   ```
3. Write tests using Google Test macros:
   ```cpp
   TEST(MyFeatureTest, TestCase) {
       EXPECT_EQ(my_function(), expected_value);
   }
   ```
4. Add the new file to `tests/CMakeLists.txt`:
   ```cmake
   add_executable(
     toasty_tests
     test_utilities.cpp
     test_presets.cpp
     test_json_hooks.cpp
     test_myfeature.cpp  # Add your new test file
     test_main.cpp
   )
   ```

### Adding Integration Tests

Add new test functions to `tests/integration/run_integration_tests.ps1`:

```powershell
function Test-MyNewFeature {
    Write-TestHeader "Testing My New Feature"
    
    try {
        $output = & $ToastyPath --myfeature 2>&1
        if ($output -match "Expected pattern") {
            Test-Pass "My new feature works"
        } else {
            Test-Fail "My new feature" "Unexpected output"
        }
    } catch {
        Test-Fail "My new feature" $_.Exception.Message
    }
}
```

Then call it in the main execution section.

## Troubleshooting

### Tests Won't Build

- Ensure you have Visual Studio 2022 with C++ workload installed
- Check that CMake is version 3.20 or later
- Verify you have an internet connection (Google Test is downloaded during configuration)

### Tests Fail in CI

- Some tests may require a GUI environment (toast notifications)
- Integration tests are designed to be resilient to headless environments
- Unit tests should run anywhere

### WinRT Initialization Errors

Some tests require Windows Runtime initialization. The test utilities handle this with `init_apartment()` calls where necessary.

## Best Practices

1. **Keep unit tests fast** - Each test should run in milliseconds
2. **Test edge cases** - Empty strings, null values, special characters
3. **Use descriptive test names** - `TEST(Component, SpecificBehavior)`
4. **Clean up after tests** - Integration tests should restore state
5. **Don't test Windows APIs directly** - Focus on toasty's logic
6. **Mock external dependencies** - Tests should be self-contained

## References

- [Google Test Documentation](https://google.github.io/googletest/)
- [CMake CTest](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
- [Windows Toast Notifications](https://docs.microsoft.com/en-us/windows/apps/design/shell/tiles-and-notifications/)
