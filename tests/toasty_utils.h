#pragma once
#include <string>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Json.h>

using namespace winrt;
using namespace Windows::Data::Json;

// Utility: Convert string to lowercase
inline std::wstring to_lower(std::wstring str) {
    for (auto& c : str) c = towlower(c);
    return str;
}

// Escape XML entities
inline std::wstring escape_xml(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    for (wchar_t c : text) {
        switch (c) {
            case L'&':  result += L"&amp;"; break;
            case L'<':  result += L"&lt;"; break;
            case L'>':  result += L"&gt;"; break;
            case L'"':  result += L"&quot;"; break;
            case L'\'': result += L"&apos;"; break;
            default:    result += c; break;
        }
    }
    return result;
}

// Escape backslashes and quotes for JSON strings
inline std::wstring escape_json_string(const std::wstring& str) {
    std::wstring result;
    result.reserve(str.size());
    for (wchar_t c : str) {
        switch (c) {
            case L'\\': result += L"\\\\"; break;
            case L'"':  result += L"\\\""; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default:    result += c; break;
        }
    }
    return result;
}

// App preset structure
struct AppPreset {
    std::wstring name;
    std::wstring title;
    int iconResourceId;
};

// Find preset by name (case-insensitive)
inline const AppPreset* find_preset(const std::wstring& name, const AppPreset* presets, size_t count) {
    auto lowerName = to_lower(name);
    for (size_t i = 0; i < count; i++) {
        if (to_lower(presets[i].name) == lowerName) {
            return &presets[i];
        }
    }
    return nullptr;
}

// Check if a JSON hook array contains toasty
inline bool has_toasty_hook(const JsonArray& hooks) {
    for (const auto& hookItem : hooks) {
        if (hookItem.ValueType() == JsonValueType::Object) {
            auto hookObj = hookItem.GetObject();
            // Check direct command field (correct format)
            if (hookObj.HasKey(L"command")) {
                std::wstring cmd = hookObj.GetNamedString(L"command").c_str();
                if (cmd.find(L"toasty") != std::wstring::npos) {
                    return true;
                }
            }
            // Also check nested hooks array (legacy format)
            if (hookObj.HasKey(L"hooks")) {
                auto innerHooks = hookObj.GetNamedArray(L"hooks");
                for (const auto& innerHook : innerHooks) {
                    if (innerHook.ValueType() == JsonValueType::Object) {
                        auto innerObj = innerHook.GetObject();
                        if (innerObj.HasKey(L"command")) {
                            std::wstring cmd = innerObj.GetNamedString(L"command").c_str();
                            if (cmd.find(L"toasty") != std::wstring::npos) {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}

// Expand environment variables in a path
inline std::wstring expand_env(const std::wstring& path) {
    wchar_t expanded[MAX_PATH];
    DWORD result = ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH);
    if (result == 0 || result > MAX_PATH) {
        // Failed to expand, return original path
        return path;
    }
    return std::wstring(expanded);
}

// Check if a path exists
inline bool path_exists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES);
}
