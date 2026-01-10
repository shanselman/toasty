#include <gtest/gtest.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>
#include "toasty_utils.h"

using namespace winrt;
using namespace Windows::Data::Json;

// Helper to convert std::string to winrt::hstring
hstring to_hstring(const std::string& str) {
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return hstring(wstr);
}

// Test has_toasty_hook with direct command format
TEST(JsonHooksTest, HasToastyHookDirect) {
    init_apartment();
    
    std::string json = R"([
        {
            "type": "command",
            "command": "C:\\path\\to\\toasty.exe \"Message\" -t \"Title\"",
            "timeout": 5000
        }
    ])";
    
    JsonArray hooks = JsonArray::Parse(to_hstring(json));
    EXPECT_TRUE(has_toasty_hook(hooks));
}

TEST(JsonHooksTest, HasToastyHookNested) {
    init_apartment();
    
    std::string json = R"([
        {
            "hooks": [
                {
                    "type": "command",
                    "command": "toasty \"Done\"",
                    "timeout": 5000
                }
            ]
        }
    ])";
    
    JsonArray hooks = JsonArray::Parse(to_hstring(json));
    EXPECT_TRUE(has_toasty_hook(hooks));
}

TEST(JsonHooksTest, HasToastyHookNotFound) {
    init_apartment();
    
    std::string json = R"([
        {
            "type": "command",
            "command": "echo \"Hello\"",
            "timeout": 5000
        }
    ])";
    
    JsonArray hooks = JsonArray::Parse(to_hstring(json));
    EXPECT_FALSE(has_toasty_hook(hooks));
}

TEST(JsonHooksTest, HasToastyHookEmpty) {
    init_apartment();
    
    std::string json = R"([])";
    
    JsonArray hooks = JsonArray::Parse(to_hstring(json));
    EXPECT_FALSE(has_toasty_hook(hooks));
}

TEST(JsonHooksTest, HasToastyHookMultipleCommands) {
    init_apartment();
    
    std::string json = R"([
        {
            "type": "command",
            "command": "echo \"First\"",
            "timeout": 5000
        },
        {
            "type": "command",
            "command": "toasty \"Second\"",
            "timeout": 5000
        }
    ])";
    
    JsonArray hooks = JsonArray::Parse(to_hstring(json));
    EXPECT_TRUE(has_toasty_hook(hooks));
}

TEST(JsonHooksTest, HasToastyHookCaseInsensitive) {
    init_apartment();
    
    std::string json = R"([
        {
            "type": "command",
            "command": "TOASTY.EXE \"Message\"",
            "timeout": 5000
        }
    ])";
    
    JsonArray hooks = JsonArray::Parse(to_hstring(json));
    EXPECT_TRUE(has_toasty_hook(hooks));
}

TEST(JsonHooksTest, HasToastyHookClaudeFormat) {
    init_apartment();
    
    // Claude Code format with nested hooks array
    std::string json = R"([
        {
            "hooks": [
                {
                    "type": "command",
                    "command": "D:\\tools\\toasty.exe \"Task complete\" -t \"Claude Code\"",
                    "timeout": 5000
                }
            ]
        }
    ])";
    
    JsonArray hooks = JsonArray::Parse(to_hstring(json));
    EXPECT_TRUE(has_toasty_hook(hooks));
}
