# Google Test Configuration
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Add test executable
add_executable(
  toasty_tests
  test_utilities.cpp
  test_presets.cpp
  test_json_hooks.cpp
  test_main.cpp
)

# Link against gtest
target_link_libraries(
  toasty_tests
  GTest::gtest_main
  windowsapp
  runtimeobject
  shlwapi
  shell32
  ole32
  propsys
)

# Set C++ standard
target_compile_features(toasty_tests PRIVATE cxx_std_20)

# Include parent directory for resource.h
target_include_directories(toasty_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Use static runtime for standalone exe
set_property(TARGET toasty_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Discover tests for CTest
include(GoogleTest)
gtest_discover_tests(toasty_tests)
