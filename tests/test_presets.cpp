#include <gtest/gtest.h>
#include "toasty_utils.h"
#include "../resource.h"

// Mock preset array for testing
const AppPreset TEST_PRESETS[] = {
    { L"claude", L"Claude", IDI_CLAUDE },
    { L"copilot", L"GitHub Copilot", IDI_COPILOT },
    { L"gemini", L"Gemini", IDI_GEMINI },
    { L"codex", L"Codex", IDI_CODEX },
    { L"cursor", L"Cursor", IDI_CURSOR }
};
const size_t TEST_PRESET_COUNT = sizeof(TEST_PRESETS) / sizeof(TEST_PRESETS[0]);

// Test find_preset function
TEST(PresetsTest, FindPresetExact) {
    const AppPreset* preset = find_preset(L"claude", TEST_PRESETS, TEST_PRESET_COUNT);
    ASSERT_NE(preset, nullptr);
    EXPECT_EQ(preset->name, L"claude");
    EXPECT_EQ(preset->title, L"Claude");
}

TEST(PresetsTest, FindPresetCaseInsensitive) {
    const AppPreset* preset = find_preset(L"CLAUDE", TEST_PRESETS, TEST_PRESET_COUNT);
    ASSERT_NE(preset, nullptr);
    EXPECT_EQ(preset->name, L"claude");
}

TEST(PresetsTest, FindPresetMixedCase) {
    const AppPreset* preset = find_preset(L"CoPiLoT", TEST_PRESETS, TEST_PRESET_COUNT);
    ASSERT_NE(preset, nullptr);
    EXPECT_EQ(preset->name, L"copilot");
    EXPECT_EQ(preset->title, L"GitHub Copilot");
}

TEST(PresetsTest, FindPresetAllValid) {
    // Test all presets can be found
    EXPECT_NE(find_preset(L"claude", TEST_PRESETS, TEST_PRESET_COUNT), nullptr);
    EXPECT_NE(find_preset(L"copilot", TEST_PRESETS, TEST_PRESET_COUNT), nullptr);
    EXPECT_NE(find_preset(L"gemini", TEST_PRESETS, TEST_PRESET_COUNT), nullptr);
    EXPECT_NE(find_preset(L"codex", TEST_PRESETS, TEST_PRESET_COUNT), nullptr);
    EXPECT_NE(find_preset(L"cursor", TEST_PRESETS, TEST_PRESET_COUNT), nullptr);
}

TEST(PresetsTest, FindPresetNotFound) {
    const AppPreset* preset = find_preset(L"unknown", TEST_PRESETS, TEST_PRESET_COUNT);
    EXPECT_EQ(preset, nullptr);
}

TEST(PresetsTest, FindPresetEmpty) {
    const AppPreset* preset = find_preset(L"", TEST_PRESETS, TEST_PRESET_COUNT);
    EXPECT_EQ(preset, nullptr);
}

TEST(PresetsTest, PresetIconResourceIds) {
    const AppPreset* claude = find_preset(L"claude", TEST_PRESETS, TEST_PRESET_COUNT);
    ASSERT_NE(claude, nullptr);
    EXPECT_EQ(claude->iconResourceId, IDI_CLAUDE);
    
    const AppPreset* copilot = find_preset(L"copilot", TEST_PRESETS, TEST_PRESET_COUNT);
    ASSERT_NE(copilot, nullptr);
    EXPECT_EQ(copilot->iconResourceId, IDI_COPILOT);
}
