#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Storage.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Data::Json;
using namespace Windows::UI::Notifications;
namespace fs = std::filesystem;

const wchar_t* APP_ID = L"Toasty.CLI.Notification";
const wchar_t* APP_NAME = L"Toasty";

void print_usage() {
    std::wcout << L"toasty - Windows toast notification CLI\n\n"
               << L"Usage:\n"
               << L"  toasty <message> [options]\n"
               << L"  toasty --install [agent]\n"
               << L"  toasty --uninstall\n"
               << L"  toasty --status\n\n"
               << L"Options:\n"
               << L"  -t, --title <text>   Set notification title (default: \"Notification\")\n"
               << L"  -h, --help           Show this help\n"
               << L"  --install [agent]    Install hooks for AI CLI agents (claude, gemini, copilot, or all)\n"
               << L"  --uninstall          Remove hooks from all AI CLI agents\n"
               << L"  --status             Show installation status\n\n"
               << L"Examples:\n"
               << L"  toasty \"Build completed\"\n"
               << L"  toasty \"Task done\" -t \"Claude Code\"\n"
               << L"  toasty --install\n"
               << L"  toasty --install claude\n"
               << L"  toasty --status\n";
}

std::wstring escape_xml(const std::wstring& text) {
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

// Escape backslashes for JSON strings
std::wstring escape_json_string(const std::wstring& str) {
    std::wstring result = str;
    size_t pos = 0;
    while ((pos = result.find(L"\\", pos)) != std::wstring::npos) {
        result.replace(pos, 1, L"\\\\");
        pos += 2;
    }
    return result;
}

// Get the full path to the current executable
std::wstring get_exe_path() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::wstring(exePath);
}

// Expand environment variables in a path
std::wstring expand_env(const std::wstring& path) {
    wchar_t expanded[MAX_PATH];
    ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH);
    return std::wstring(expanded);
}

// Check if a path exists
bool path_exists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES);
}

// Agent detection functions
bool detect_claude() {
    std::wstring claudePath = expand_env(L"%USERPROFILE%\\.claude");
    return path_exists(claudePath);
}

bool detect_gemini() {
    std::wstring geminiPath = expand_env(L"%USERPROFILE%\\.gemini");
    return path_exists(geminiPath);
}

bool detect_copilot() {
    return path_exists(L".github");
}

// Read file content as string
std::string read_file(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Write string to file
bool write_file(const std::wstring& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file << content;
    return file.good();
}

// Backup a file
bool backup_file(const std::wstring& path) {
    std::wstring backupPath = path + L".bak";
    try {
        if (path_exists(path)) {
            fs::copy_file(path, backupPath, fs::copy_options::overwrite_existing);
        }
        return true;
    } catch (const std::exception& e) {
        std::wcerr << L"Warning: Failed to create backup: " << e.what() << L"\n";
        return false;
    }
}

// Convert std::string to winrt::hstring
hstring to_hstring(const std::string& str) {
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return hstring(wstr);
}

// Convert winrt::hstring to std::string
std::string from_hstring(const hstring& hstr) {
    std::wstring wstr(hstr.c_str());
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    return str;
}

// Check if a JSON hook array contains toasty
bool has_toasty_hook(const JsonArray& hooks) {
    for (const auto& hookItem : hooks) {
        if (hookItem.ValueType() == JsonValueType::Object) {
            auto hookObj = hookItem.GetObject();
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

// Install hook for Claude Code
bool install_claude(const std::wstring& exePath) {
    std::wstring configPath = expand_env(L"%USERPROFILE%\\.claude\\settings.json");
    
    JsonObject rootObj;
    std::string existingContent = read_file(configPath);
    
    if (!existingContent.empty()) {
        try {
            backup_file(configPath);
            rootObj = JsonObject::Parse(to_hstring(existingContent));
        } catch (const hresult_error& ex) {
            std::wcerr << L"Warning: Failed to parse existing config, starting fresh: " << ex.message().c_str() << L"\n";
            rootObj = JsonObject();
        }
    }
    
    // Build hook structure
    JsonObject hookItem;
    JsonArray innerHooks;
    JsonObject innerHook;
    innerHook.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
    
    std::wstring escapedPath = escape_json_string(exePath);
    std::wstring command = escapedPath + L" \"Claude needs attention\" -t \"Claude Code\"";
    innerHook.SetNamedValue(L"command", JsonValue::CreateStringValue(command));
    innerHook.SetNamedValue(L"timeout", JsonValue::CreateNumberValue(5000));
    
    innerHooks.Append(innerHook);
    hookItem.SetNamedValue(L"hooks", innerHooks);
    
    // Get or create hooks object
    JsonObject hooksObj;
    if (rootObj.HasKey(L"hooks")) {
        hooksObj = rootObj.GetNamedObject(L"hooks");
    }
    
    // Get or create Stop array
    JsonArray stopArray;
    if (hooksObj.HasKey(L"Stop")) {
        stopArray = hooksObj.GetNamedArray(L"Stop");
        if (has_toasty_hook(stopArray)) {
            return true; // Already installed
        }
    }
    
    stopArray.Append(hookItem);
    hooksObj.SetNamedValue(L"Stop", stopArray);
    rootObj.SetNamedValue(L"hooks", hooksObj);
    
    // Write to file
    std::string jsonStr = from_hstring(rootObj.Stringify());
    return write_file(configPath, jsonStr);
}

// Install hook for Gemini CLI
bool install_gemini(const std::wstring& exePath) {
    std::wstring configPath = expand_env(L"%USERPROFILE%\\.gemini\\settings.json");
    
    JsonObject rootObj;
    std::string existingContent = read_file(configPath);
    
    if (!existingContent.empty()) {
        try {
            backup_file(configPath);
            rootObj = JsonObject::Parse(to_hstring(existingContent));
        } catch (const hresult_error& ex) {
            std::wcerr << L"Warning: Failed to parse existing config, starting fresh: " << ex.message().c_str() << L"\n";
            rootObj = JsonObject();
        }
    }
    
    // Build hook structure
    JsonObject hookItem;
    JsonArray innerHooks;
    JsonObject innerHook;
    innerHook.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
    
    std::wstring escapedPath = escape_json_string(exePath);
    std::wstring command = escapedPath + L" \"Gemini finished\" -t \"Gemini\"";
    innerHook.SetNamedValue(L"command", JsonValue::CreateStringValue(command));
    innerHook.SetNamedValue(L"timeout", JsonValue::CreateNumberValue(5000));
    
    innerHooks.Append(innerHook);
    hookItem.SetNamedValue(L"hooks", innerHooks);
    
    // Get or create hooks object
    JsonObject hooksObj;
    if (rootObj.HasKey(L"hooks")) {
        hooksObj = rootObj.GetNamedObject(L"hooks");
    }
    
    // Get or create AfterAgent array
    JsonArray afterAgentArray;
    if (hooksObj.HasKey(L"AfterAgent")) {
        afterAgentArray = hooksObj.GetNamedArray(L"AfterAgent");
        if (has_toasty_hook(afterAgentArray)) {
            return true; // Already installed
        }
    }
    
    afterAgentArray.Append(hookItem);
    hooksObj.SetNamedValue(L"AfterAgent", afterAgentArray);
    rootObj.SetNamedValue(L"hooks", hooksObj);
    
    // Write to file
    std::string jsonStr = from_hstring(rootObj.Stringify());
    return write_file(configPath, jsonStr);
}

// Install hook for GitHub Copilot
bool install_copilot(const std::wstring& exePath) {
    std::wstring hooksDir = L".github\\hooks";
    std::wstring configPath = hooksDir + L"\\toasty.json";
    
    // Create .github/hooks directory if it doesn't exist
    fs::create_directories(hooksDir);
    
    JsonObject rootObj;
    std::string existingContent = read_file(configPath);
    
    if (!existingContent.empty()) {
        try {
            backup_file(configPath);
            rootObj = JsonObject::Parse(to_hstring(existingContent));
        } catch (const hresult_error& ex) {
            std::wcerr << L"Warning: Failed to parse existing config, starting fresh: " << ex.message().c_str() << L"\n";
            rootObj = JsonObject();
        }
    }
    
    // Set version
    rootObj.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));
    
    // Build hook structure
    JsonObject hookObj;
    hookObj.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
    
    // For bash, use forward slashes and toasty (assuming it's in PATH)
    hookObj.SetNamedValue(L"bash", JsonValue::CreateStringValue(L"toasty 'Copilot finished' -t 'GitHub Copilot'"));
    
    // For PowerShell, use the full exe path with escaped backslashes
    std::wstring escapedPath = escape_json_string(exePath);
    std::wstring psCommand = escapedPath + L" 'Copilot finished' -t 'GitHub Copilot'";
    hookObj.SetNamedValue(L"powershell", JsonValue::CreateStringValue(psCommand));
    
    hookObj.SetNamedValue(L"timeoutSec", JsonValue::CreateNumberValue(5));
    
    // Get or create hooks object
    JsonObject hooksObj;
    if (rootObj.HasKey(L"hooks")) {
        hooksObj = rootObj.GetNamedObject(L"hooks");
    }
    
    // Get or create sessionEnd array
    JsonArray sessionEndArray;
    if (hooksObj.HasKey(L"sessionEnd")) {
        sessionEndArray = hooksObj.GetNamedArray(L"sessionEnd");
        // Check if toasty is already there
        for (const auto& item : sessionEndArray) {
            if (item.ValueType() == JsonValueType::Object) {
                auto obj = item.GetObject();
                if (obj.HasKey(L"bash")) {
                    std::wstring bash = obj.GetNamedString(L"bash").c_str();
                    if (bash.find(L"toasty") != std::wstring::npos) {
                        return true; // Already installed
                    }
                }
            }
        }
    }
    
    sessionEndArray.Append(hookObj);
    hooksObj.SetNamedValue(L"sessionEnd", sessionEndArray);
    rootObj.SetNamedValue(L"hooks", hooksObj);
    
    // Write to file
    std::string jsonStr = from_hstring(rootObj.Stringify());
    return write_file(configPath, jsonStr);
}

// Check if Claude hook is installed
bool is_claude_installed() {
    std::wstring configPath = expand_env(L"%USERPROFILE%\\.claude\\settings.json");
    std::string content = read_file(configPath);
    if (content.empty()) return false;
    
    try {
        auto rootObj = JsonObject::Parse(to_hstring(content));
        if (rootObj.HasKey(L"hooks")) {
            auto hooksObj = rootObj.GetNamedObject(L"hooks");
            if (hooksObj.HasKey(L"Stop")) {
                auto stopArray = hooksObj.GetNamedArray(L"Stop");
                return has_toasty_hook(stopArray);
            }
        }
    } catch (const hresult_error&) {
        // Config exists but couldn't be parsed
        return false;
    }
    
    return false;
}

// Check if Gemini hook is installed
bool is_gemini_installed() {
    std::wstring configPath = expand_env(L"%USERPROFILE%\\.gemini\\settings.json");
    std::string content = read_file(configPath);
    if (content.empty()) return false;
    
    try {
        auto rootObj = JsonObject::Parse(to_hstring(content));
        if (rootObj.HasKey(L"hooks")) {
            auto hooksObj = rootObj.GetNamedObject(L"hooks");
            if (hooksObj.HasKey(L"AfterAgent")) {
                auto array = hooksObj.GetNamedArray(L"AfterAgent");
                return has_toasty_hook(array);
            }
        }
    } catch (const hresult_error&) {
        // Config exists but couldn't be parsed
        return false;
    }
    
    return false;
}

// Check if Copilot hook is installed
bool is_copilot_installed() {
    std::wstring configPath = L".github\\hooks\\toasty.json";
    std::string content = read_file(configPath);
    if (content.empty()) return false;
    
    try {
        auto rootObj = JsonObject::Parse(to_hstring(content));
        if (rootObj.HasKey(L"hooks")) {
            auto hooksObj = rootObj.GetNamedObject(L"hooks");
            if (hooksObj.HasKey(L"sessionEnd")) {
                auto array = hooksObj.GetNamedArray(L"sessionEnd");
                for (const auto& item : array) {
                    if (item.ValueType() == JsonValueType::Object) {
                        auto obj = item.GetObject();
                        if (obj.HasKey(L"bash")) {
                            std::wstring bash = obj.GetNamedString(L"bash").c_str();
                            if (bash.find(L"toasty") != std::wstring::npos) {
                                return true;
                            }
                        }
                    }
                }
            }
        }
    } catch (const hresult_error&) {
        // Config exists but couldn't be parsed
        return false;
    }
    
    return false;
}

// Remove toasty hooks from a JSON array
JsonArray remove_toasty_hooks(const JsonArray& hooks) {
    JsonArray newArray;
    for (const auto& hookItem : hooks) {
        if (hookItem.ValueType() == JsonValueType::Object) {
            auto hookObj = hookItem.GetObject();
            bool isToasty = false;
            
            if (hookObj.HasKey(L"hooks")) {
                auto innerHooks = hookObj.GetNamedArray(L"hooks");
                for (const auto& innerHook : innerHooks) {
                    if (innerHook.ValueType() == JsonValueType::Object) {
                        auto innerObj = innerHook.GetObject();
                        if (innerObj.HasKey(L"command")) {
                            std::wstring cmd = innerObj.GetNamedString(L"command").c_str();
                            if (cmd.find(L"toasty") != std::wstring::npos) {
                                isToasty = true;
                                break;
                            }
                        }
                    }
                }
            } else if (hookObj.HasKey(L"bash")) {
                std::wstring bash = hookObj.GetNamedString(L"bash").c_str();
                if (bash.find(L"toasty") != std::wstring::npos) {
                    isToasty = true;
                }
            }
            
            if (!isToasty) {
                newArray.Append(hookItem);
            }
        }
    }
    return newArray;
}

// Uninstall hook for Claude Code
bool uninstall_claude() {
    std::wstring configPath = expand_env(L"%USERPROFILE%\\.claude\\settings.json");
    std::string existingContent = read_file(configPath);
    
    if (existingContent.empty()) {
        return true; // Nothing to uninstall
    }
    
    try {
        backup_file(configPath);
        auto rootObj = JsonObject::Parse(to_hstring(existingContent));
        
        if (rootObj.HasKey(L"hooks")) {
            auto hooksObj = rootObj.GetNamedObject(L"hooks");
            if (hooksObj.HasKey(L"Stop")) {
                auto stopArray = hooksObj.GetNamedArray(L"Stop");
                auto newArray = remove_toasty_hooks(stopArray);
                hooksObj.SetNamedValue(L"Stop", newArray);
                rootObj.SetNamedValue(L"hooks", hooksObj);
                
                std::string jsonStr = from_hstring(rootObj.Stringify());
                return write_file(configPath, jsonStr);
            }
        }
    } catch (const hresult_error& ex) {
        std::wcerr << L"Error uninstalling Claude hook: " << ex.message().c_str() << L"\n";
        return false;
    }
    
    return true;
}

// Uninstall hook for Gemini CLI
bool uninstall_gemini() {
    std::wstring configPath = expand_env(L"%USERPROFILE%\\.gemini\\settings.json");
    std::string existingContent = read_file(configPath);
    
    if (existingContent.empty()) {
        return true; // Nothing to uninstall
    }
    
    try {
        backup_file(configPath);
        auto rootObj = JsonObject::Parse(to_hstring(existingContent));
        
        if (rootObj.HasKey(L"hooks")) {
            auto hooksObj = rootObj.GetNamedObject(L"hooks");
            if (hooksObj.HasKey(L"AfterAgent")) {
                auto array = hooksObj.GetNamedArray(L"AfterAgent");
                auto newArray = remove_toasty_hooks(array);
                hooksObj.SetNamedValue(L"AfterAgent", newArray);
                rootObj.SetNamedValue(L"hooks", hooksObj);
                
                std::string jsonStr = from_hstring(rootObj.Stringify());
                return write_file(configPath, jsonStr);
            }
        }
    } catch (const hresult_error& ex) {
        std::wcerr << L"Error uninstalling Gemini hook: " << ex.message().c_str() << L"\n";
        return false;
    }
    
    return true;
}

// Uninstall hook for GitHub Copilot
bool uninstall_copilot() {
    std::wstring configPath = L".github\\hooks\\toasty.json";
    
    try {
        if (path_exists(configPath)) {
            backup_file(configPath);
            fs::remove(configPath);
        }
    } catch (const std::exception& ex) {
        std::wcerr << L"Error uninstalling Copilot hook: " << ex.what() << L"\n";
        return false;
    }
    
    return true;
}

// Show installation status
void show_status() {
    std::wcout << L"Installation status:\n\n";
    
    bool claudeDetected = detect_claude();
    bool geminiDetected = detect_gemini();
    bool copilotDetected = detect_copilot();
    
    std::wcout << L"Detected agents:\n";
    std::wcout << L"  " << (claudeDetected ? L"[x]" : L"[ ]") << L" Claude Code\n";
    std::wcout << L"  " << (geminiDetected ? L"[x]" : L"[ ]") << L" Gemini CLI\n";
    std::wcout << L"  " << (copilotDetected ? L"[x]" : L"[ ]") << L" GitHub Copilot (in current repo)\n";
    std::wcout << L"\n";
    
    std::wcout << L"Installed hooks:\n";
    std::wcout << L"  " << (is_claude_installed() ? L"[x]" : L"[ ]") << L" Claude Code\n";
    std::wcout << L"  " << (is_gemini_installed() ? L"[x]" : L"[ ]") << L" Gemini CLI\n";
    std::wcout << L"  " << (is_copilot_installed() ? L"[x]" : L"[ ]") << L" GitHub Copilot\n";
}

// Handle --install command
void handle_install(const std::wstring& agent) {
    std::wstring exePath = get_exe_path();
    
    bool installAll = agent.empty() || agent == L"all";
    bool installClaude = installAll || agent == L"claude";
    bool installGemini = installAll || agent == L"gemini";
    bool installCopilot = installAll || agent == L"copilot";
    
    std::wcout << L"Detecting AI CLI agents...\n";
    
    bool claudeDetected = detect_claude();
    bool geminiDetected = detect_gemini();
    bool copilotDetected = detect_copilot();
    
    std::wcout << L"  " << (claudeDetected ? L"[x]" : L"[ ]") << L" Claude Code found\n";
    std::wcout << L"  " << (geminiDetected ? L"[x]" : L"[ ]") << L" Gemini CLI found\n";
    std::wcout << L"  " << (copilotDetected ? L"[x]" : L"[ ]") << L" GitHub Copilot (in current repo)\n";
    std::wcout << L"\n";
    
    std::wcout << L"Installing toasty hooks...\n";
    
    bool anyInstalled = false;
    
    if (installClaude && claudeDetected) {
        if (install_claude(exePath)) {
            std::wcout << L"  [x] Claude Code: Added Stop hook\n";
            anyInstalled = true;
        } else {
            std::wcout << L"  [ ] Claude Code: Failed to install\n";
        }
    }
    
    if (installGemini && geminiDetected) {
        if (install_gemini(exePath)) {
            std::wcout << L"  [x] Gemini CLI: Added AfterAgent hook\n";
            anyInstalled = true;
        } else {
            std::wcout << L"  [ ] Gemini CLI: Failed to install\n";
        }
    }
    
    if (installCopilot && copilotDetected) {
        if (install_copilot(exePath)) {
            std::wcout << L"  [x] GitHub Copilot: Added sessionEnd hook\n";
            std::wcout << L"      Note: This is repo-level only, not global\n";
            anyInstalled = true;
        } else {
            std::wcout << L"  [ ] GitHub Copilot: Failed to install\n";
        }
    }
    
    if (anyInstalled) {
        std::wcout << L"\nDone! You'll get notifications when AI agents finish.\n";
    } else {
        std::wcout << L"\nNo agents were installed. Check detection status above.\n";
    }
}

// Handle --uninstall command
void handle_uninstall() {
    std::wcout << L"Removing toasty hooks...\n";
    
    bool anyUninstalled = false;
    
    if (is_claude_installed()) {
        if (uninstall_claude()) {
            std::wcout << L"  [x] Claude Code: Removed hooks\n";
            anyUninstalled = true;
        } else {
            std::wcout << L"  [ ] Claude Code: Failed to remove\n";
        }
    }
    
    if (is_gemini_installed()) {
        if (uninstall_gemini()) {
            std::wcout << L"  [x] Gemini CLI: Removed hooks\n";
            anyUninstalled = true;
        } else {
            std::wcout << L"  [ ] Gemini CLI: Failed to remove\n";
        }
    }
    
    if (is_copilot_installed()) {
        if (uninstall_copilot()) {
            std::wcout << L"  [x] GitHub Copilot: Removed hooks\n";
            anyUninstalled = true;
        } else {
            std::wcout << L"  [ ] GitHub Copilot: Failed to remove\n";
        }
    }
    
    if (anyUninstalled) {
        std::wcout << L"\nDone! Hooks have been removed.\n";
    } else {
        std::wcout << L"\nNo hooks were installed.\n";
    }
}

// Create a Start Menu shortcut with our AppUserModelId
bool create_shortcut() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    wchar_t startMenuPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startMenuPath))) {
        return false;
    }

    std::wstring shortcutPath = std::wstring(startMenuPath) + L"\\Toasty.lnk";

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, (void**)&shellLink);
    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }

    shellLink->SetPath(exePath);
    shellLink->SetDescription(L"Toasty - Toast Notification CLI");

    IPropertyStore* propStore = nullptr;
    hr = shellLink->QueryInterface(IID_IPropertyStore, (void**)&propStore);
    if (SUCCEEDED(hr)) {
        PROPVARIANT pv;
        hr = InitPropVariantFromString(APP_ID, &pv);
        if (SUCCEEDED(hr)) {
            propStore->SetValue(PKEY_AppUserModel_ID, pv);
            PropVariantClear(&pv);
        }
        propStore->Commit();
        propStore->Release();
    }

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, (void**)&persistFile);
    if (SUCCEEDED(hr)) {
        hr = persistFile->Save(shortcutPath.c_str(), TRUE);
        persistFile->Release();
    }

    shellLink->Release();
    CoUninitialize();

    if (SUCCEEDED(hr)) {
        std::wcout << L"Registered! Shortcut created at:\n" << shortcutPath << L"\n";
        return true;
    }
    return false;
}

bool is_registered() {
    wchar_t startMenuPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startMenuPath))) {
        return false;
    }
    std::wstring shortcutPath = std::wstring(startMenuPath) + L"\\Toasty.lnk";
    return GetFileAttributesW(shortcutPath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ensure_registered() {
    if (is_registered()) {
        return true;
    }
    // Silently self-register
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    wchar_t startMenuPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startMenuPath))) {
        return false;
    }

    std::wstring shortcutPath = std::wstring(startMenuPath) + L"\\Toasty.lnk";

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, (void**)&shellLink);
    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }

    shellLink->SetPath(exePath);
    shellLink->SetDescription(L"Toasty - Toast Notification CLI");

    IPropertyStore* propStore = nullptr;
    hr = shellLink->QueryInterface(IID_IPropertyStore, (void**)&propStore);
    if (SUCCEEDED(hr)) {
        PROPVARIANT pv;
        hr = InitPropVariantFromString(APP_ID, &pv);
        if (SUCCEEDED(hr)) {
            propStore->SetValue(PKEY_AppUserModel_ID, pv);
            PropVariantClear(&pv);
        }
        propStore->Commit();
        propStore->Release();
    }

    IPersistFile* persistFile = nullptr;
    hr = shellLink->QueryInterface(IID_IPersistFile, (void**)&persistFile);
    if (SUCCEEDED(hr)) {
        hr = persistFile->Save(shortcutPath.c_str(), TRUE);
        persistFile->Release();
    }

    shellLink->Release();
    CoUninitialize();

    return SUCCEEDED(hr);
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::wstring message;
    std::wstring title = L"Notification";
    bool doRegister = false;
    bool doInstall = false;
    bool doUninstall = false;
    bool doStatus = false;
    std::wstring installAgent;

    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];

        if (arg == L"-h" || arg == L"--help") {
            print_usage();
            return 0;
        }
        else if (arg == L"--register") {
            doRegister = true;
        }
        else if (arg == L"--install") {
            doInstall = true;
            // Check if next arg is an agent name
            if (i + 1 < argc && argv[i + 1][0] != L'-') {
                installAgent = argv[++i];
            }
        }
        else if (arg == L"--uninstall") {
            doUninstall = true;
        }
        else if (arg == L"--status") {
            doStatus = true;
        }
        else if (arg == L"-t" || arg == L"--title") {
            if (i + 1 < argc) {
                title = argv[++i];
            }
        }
        else if (arg[0] != L'-' && message.empty()) {
            message = arg;
        }
    }

    if (doRegister) {
        if (create_shortcut()) {
            std::wcout << L"App registered for notifications.\n";
            return 0;
        } else {
            std::wcerr << L"Failed to register app.\n";
            return 1;
        }
    }

    if (doStatus) {
        init_apartment();
        show_status();
        return 0;
    }

    if (doInstall) {
        init_apartment();
        handle_install(installAgent);
        return 0;
    }

    if (doUninstall) {
        init_apartment();
        handle_uninstall();
        return 0;
    }

    if (message.empty()) {
        std::wcerr << L"Error: Message is required.\n";
        print_usage();
        return 1;
    }

    try {
        // Auto-register if needed
        ensure_registered();

        init_apartment();

        // Set our AppUserModelId for this process
        SetCurrentProcessExplicitAppUserModelID(APP_ID);

        std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\">"
                          L"<text>" + escape_xml(title) + L"</text>"
                          L"<text>" + escape_xml(message) + L"</text>"
                          L"</binding></visual></toast>";

        XmlDocument doc;
        doc.LoadXml(xml);

        ToastNotification toast(doc);

        auto notifier = ToastNotificationManager::CreateToastNotifier(APP_ID);
        notifier.Show(toast);

        return 0;
    }
    catch (const hresult_error& ex) {
        std::wcerr << L"Error: " << ex.message().c_str() << L"\n";
        return 1;
    }
}
