#!/bin/bash
# Test script for toasty macOS build
# Run from repository root: ./tests/test_mac.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOASTY="${REPO_ROOT}/build/toasty"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

PASSED=0
FAILED=0

# Test helper functions
pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    PASSED=$((PASSED + 1))
}

fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    FAILED=$((FAILED + 1))
}

skip() {
    echo -e "${YELLOW}○ SKIP${NC}: $1"
}

# Check if binary exists
check_binary() {
    if [[ ! -x "$TOASTY" ]]; then
        echo -e "${RED}Error: toasty binary not found at $TOASTY${NC}"
        echo "Build first with: cmake -S . -B build && cmake --build build"
        exit 1
    fi
    pass "Binary exists and is executable"
}

# Test: Help flag shows usage
test_help() {
    local output
    output=$("$TOASTY" --help 2>&1)
    
    if echo "$output" | grep -q "Usage:"; then
        pass "--help shows usage"
    else
        fail "--help should show usage"
    fi
    
    if echo "$output" | grep -q "toasty <message>"; then
        pass "--help shows message syntax"
    else
        fail "--help should show message syntax"
    fi
    
    if echo "$output" | grep -q "\-t, --title"; then
        pass "--help shows title option"
    else
        fail "--help should show title option"
    fi
    
    if echo "$output" | grep -q "\--install"; then
        pass "--help shows install option"
    else
        fail "--help should show install option"
    fi
}

# Test: No args shows help
test_no_args() {
    local output
    output=$("$TOASTY" 2>&1)
    
    if echo "$output" | grep -q "Usage:"; then
        pass "No args shows usage"
    else
        fail "No args should show usage"
    fi
}

# Test: Status command runs
test_status() {
    local output
    local exit_code
    
    output=$("$TOASTY" --status 2>&1) && exit_code=0 || exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        pass "--status exits with code 0"
    else
        fail "--status should exit with code 0"
    fi
    
    if echo "$output" | grep -q "Claude Code"; then
        pass "--status shows Claude Code"
    else
        fail "--status should show Claude Code"
    fi
    
    if echo "$output" | grep -q "Gemini CLI"; then
        pass "--status shows Gemini CLI"
    else
        fail "--status should show Gemini CLI"
    fi
    
    if echo "$output" | grep -q "GitHub Copilot"; then
        pass "--status shows GitHub Copilot"
    else
        fail "--status should show GitHub Copilot"
    fi
}

# Test: Debug flag works
test_debug() {
    local output
    output=$("$TOASTY" "test" --debug 2>&1)
    
    if echo "$output" | grep -q "\[DEBUG\]"; then
        pass "--debug shows debug output"
    else
        fail "--debug should show debug output"
    fi
}

# Test: Notification with message (visual test - just check it doesn't crash)
test_notification() {
    local exit_code
    
    "$TOASTY" "Test notification from toasty" -t "Toasty Test" >/dev/null 2>&1 && exit_code=0 || exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        pass "Notification command exits cleanly"
        echo "  (Check your notification center for the test notification)"
    else
        fail "Notification command should exit with code 0"
    fi
}

# Test: App preset flag
test_app_preset() {
    local exit_code
    
    # Test with valid preset
    "$TOASTY" "Testing Claude preset" --app claude >/dev/null 2>&1 && exit_code=0 || exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        pass "--app claude works"
    else
        fail "--app claude should work"
    fi
    
    # Test with invalid preset (should warn but still work)
    "$TOASTY" "Testing invalid preset" --app invalid 2>&1 | grep -q "Warning" && exit_code=0 || exit_code=1
    
    if [[ $exit_code -eq 0 ]]; then
        pass "--app with invalid preset shows warning"
    else
        fail "--app with invalid preset should show warning"
    fi
}

# Test: Title option
test_title_option() {
    local exit_code
    
    "$TOASTY" "Message with custom title" -t "Custom Title" >/dev/null 2>&1 && exit_code=0 || exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        pass "-t option works"
    else
        fail "-t option should work"
    fi
    
    "$TOASTY" "Message with long title" --title "Long Form Title" >/dev/null 2>&1 && exit_code=0 || exit_code=$?
    
    if [[ $exit_code -eq 0 ]]; then
        pass "--title option works"
    else
        fail "--title option should work"
    fi
}

# Run all tests
main() {
    echo "========================================"
    echo "Toasty macOS Test Suite"
    echo "========================================"
    echo ""
    
    check_binary
    echo ""
    
    echo "--- Help & Usage Tests ---"
    test_help
    test_no_args
    echo ""
    
    echo "--- Status Command Tests ---"
    test_status
    echo ""
    
    echo "--- Debug Flag Tests ---"
    test_debug
    echo ""
    
    echo "--- Notification Tests ---"
    test_notification
    test_app_preset
    test_title_option
    echo ""
    
    echo "========================================"
    printf "Results: ${GREEN}%d passed${NC}, ${RED}%d failed${NC}\n" "$PASSED" "$FAILED"
    echo "========================================"
    
    if [[ $FAILED -gt 0 ]]; then
        exit 1
    fi
}

main "$@"
