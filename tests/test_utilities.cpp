#include <gtest/gtest.h>
#include <winrt/Windows.Foundation.h>
#include "toasty_utils.h"

using namespace winrt;

// Test to_lower function
TEST(UtilitiesTest, ToLowerBasic) {
    EXPECT_EQ(to_lower(L"HELLO"), L"hello");
    EXPECT_EQ(to_lower(L"HeLLo"), L"hello");
    EXPECT_EQ(to_lower(L"hello"), L"hello");
}

TEST(UtilitiesTest, ToLowerEmpty) {
    EXPECT_EQ(to_lower(L""), L"");
}

TEST(UtilitiesTest, ToLowerMixed) {
    EXPECT_EQ(to_lower(L"Claude123"), L"claude123");
    EXPECT_EQ(to_lower(L"GitHub-Copilot"), L"github-copilot");
}

// Test escape_xml function
TEST(UtilitiesTest, EscapeXmlBasic) {
    EXPECT_EQ(escape_xml(L"Hello World"), L"Hello World");
}

TEST(UtilitiesTest, EscapeXmlAmpersand) {
    EXPECT_EQ(escape_xml(L"A & B"), L"A &amp; B");
}

TEST(UtilitiesTest, EscapeXmlAllEntities) {
    EXPECT_EQ(escape_xml(L"<tag attr=\"value\" & 'quote'>"), 
              L"&lt;tag attr=&quot;value&quot; &amp; &apos;quote&apos;&gt;");
}

TEST(UtilitiesTest, EscapeXmlEmpty) {
    EXPECT_EQ(escape_xml(L""), L"");
}

TEST(UtilitiesTest, EscapeXmlMultiple) {
    EXPECT_EQ(escape_xml(L"&&<<>>"), L"&amp;&amp;&lt;&lt;&gt;&gt;");
}

// Test escape_json_string function
TEST(UtilitiesTest, EscapeJsonBasic) {
    EXPECT_EQ(escape_json_string(L"Hello World"), L"Hello World");
}

TEST(UtilitiesTest, EscapeJsonBackslash) {
    EXPECT_EQ(escape_json_string(L"C:\\path\\to\\file"), L"C:\\\\path\\\\to\\\\file");
}

TEST(UtilitiesTest, EscapeJsonQuotes) {
    EXPECT_EQ(escape_json_string(L"He said \"Hello\""), L"He said \\\"Hello\\\"");
}

TEST(UtilitiesTest, EscapeJsonControlChars) {
    EXPECT_EQ(escape_json_string(L"Line1\nLine2"), L"Line1\\nLine2");
    EXPECT_EQ(escape_json_string(L"Tab\there"), L"Tab\\there");
    EXPECT_EQ(escape_json_string(L"Return\r"), L"Return\\r");
}

TEST(UtilitiesTest, EscapeJsonEmpty) {
    EXPECT_EQ(escape_json_string(L""), L"");
}

TEST(UtilitiesTest, EscapeJsonCombined) {
    EXPECT_EQ(escape_json_string(L"Path: \"C:\\test\"\nDone"), 
              L"Path: \\\"C:\\\\test\\\"\\nDone");
}

// Test expand_env function
TEST(UtilitiesTest, ExpandEnvNoVariables) {
    EXPECT_EQ(expand_env(L"C:\\plain\\path"), L"C:\\plain\\path");
}

TEST(UtilitiesTest, ExpandEnvWithVariable) {
    // This should expand USERPROFILE or similar
    std::wstring result = expand_env(L"%TEMP%");
    // Just verify it's not the same (should be expanded)
    EXPECT_NE(result, L"%TEMP%");
    EXPECT_GT(result.length(), 0);
}

TEST(UtilitiesTest, ExpandEnvEmpty) {
    EXPECT_EQ(expand_env(L""), L"");
}

// Test path_exists function
TEST(UtilitiesTest, PathExistsTemp) {
    // Temp directory should always exist
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    EXPECT_TRUE(path_exists(tempPath));
}

TEST(UtilitiesTest, PathExistsNonExistent) {
    EXPECT_FALSE(path_exists(L"C:\\this\\path\\should\\not\\exist\\12345"));
}
