#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Storage.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <tlhelp32.h>
#include "resource.h"

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
const wchar_t* PROTOCOL_NAME = L"toasty";

// RAII wrapper for Windows handles
struct HandleGuard {
    HANDLE h;
    HandleGuard(HANDLE handle) : h(handle) {}
    ~HandleGuard() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    operator HANDLE() const { return h; }
    bool valid() const { return h && h != INVALID_HANDLE_VALUE; }
};

struct AppPreset {
    std::wstring name;
    std::wstring title;
    int iconResourceId;
};

const AppPreset APP_PRESETS[] = {
    { L"claude", L"Claude", IDI_CLAUDE },
    { L"copilot", L"GitHub Copilot", IDI_COPILOT },
    { L"gemini", L"Gemini", IDI_GEMINI },
    { L"codex", L"Codex", IDI_CODEX },
    { L"cursor", L"Cursor", IDI_CURSOR }
};

// Extract embedded PNG resource to temp file and return path
std::wstring extract_icon_to_temp(int resourceId) {
    HRSRC hResource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (!hResource) return L"";
    
    HGLOBAL hLoadedResource = LoadResource(nullptr, hResource);
    if (!hLoadedResource) return L"";
    
    LPVOID pLockedResource = LockResource(hLoadedResource);
    if (!pLockedResource) return L"";
    
    DWORD resourceSize = SizeofResource(nullptr, hResource);
    if (resourceSize == 0) return L"";
    
    // Create temp file path using std::filesystem
    wchar_t tempPathBuffer[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPathBuffer);
    
    std::filesystem::path tempPath(tempPathBuffer);
    std::wstring fileName = L"toasty_icon_" + std::to_wstring(resourceId) + L".png";
    tempPath /= fileName;
    
    // Write resource data to temp file
    try {
        std::ofstream file(tempPath, std::ios::binary);
        if (!file) return L"";
        
        file.write(static_cast<const char*>(pLockedResource), resourceSize);
        file.close();
        
        if (file.fail()) return L"";
        
        return tempPath.wstring();
    } catch (...) {
        // Failed to write icon file
        return L"";
    }
}

// Utility: Convert string to lowercase
std::wstring to_lower(std::wstring str) {
    for (auto& c : str) c = towlower(c);
    return str;
}

// Find preset by name (case-insensitive)
const AppPreset* find_preset(const std::wstring& name) {
    auto lowerName = to_lower(name);
    for (const auto& preset : APP_PRESETS) {
        if (to_lower(preset.name) == lowerName) {
            return &preset;
        }
    }
    return nullptr;
}

// Get command line of a process using NtQueryInformationProcess with ProcessCommandLineInformation
typedef NTSTATUS(NTAPI* NtQueryInformationProcessFn)(HANDLE, ULONG, PVOID, ULONG, PULONG);

std::wstring get_process_command_line(DWORD pid) {
    std::wstring cmdLine;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return L"";
    }

    // Get NtQueryInformationProcess
    static NtQueryInformationProcessFn NtQueryInformationProcess = nullptr;
    if (!NtQueryInformationProcess) {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            NtQueryInformationProcess = (NtQueryInformationProcessFn)GetProcAddress(ntdll, "NtQueryInformationProcess");
        }
    }

    if (!NtQueryInformationProcess) {
        CloseHandle(hProcess);
        return L"";
    }

    // Use ProcessCommandLineInformation (60) - available on Windows 8.1+
    const ULONG ProcessCommandLineInformation = 60;

    struct UNICODE_STRING {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR Buffer;
    };

    // First call to get required size
    ULONG returnLength = 0;
    NtQueryInformationProcess(hProcess, ProcessCommandLineInformation, nullptr, 0, &returnLength);

    if (returnLength == 0) {
        CloseHandle(hProcess);
        return L"";
    }

    // Allocate buffer and get command line
    std::vector<BYTE> buffer(returnLength);
    NTSTATUS status = NtQueryInformationProcess(hProcess, ProcessCommandLineInformation,
                                                 buffer.data(), returnLength, &returnLength);

    if (status == 0) {
        UNICODE_STRING* unicodeString = reinterpret_cast<UNICODE_STRING*>(buffer.data());
        if (unicodeString->Length > 0 && unicodeString->Buffer) {
            cmdLine.assign(unicodeString->Buffer, unicodeString->Length / sizeof(wchar_t));
        }
    }

    CloseHandle(hProcess);
    return cmdLine;
}

// Check if command line contains a known CLI pattern
const AppPreset* check_command_line_for_preset(const std::wstring& cmdLine) {
    auto lowerCmd = to_lower(cmdLine);

    // Check for Gemini CLI (multiple patterns)
    if (lowerCmd.find(L"gemini-cli") != std::wstring::npos ||
        lowerCmd.find(L"gemini\\cli") != std::wstring::npos ||
        lowerCmd.find(L"gemini/cli") != std::wstring::npos ||
        lowerCmd.find(L"@google\\gemini") != std::wstring::npos ||
        lowerCmd.find(L"@google/gemini") != std::wstring::npos) {
        return find_preset(L"gemini");
    }

    // Check for Claude Code (in case it runs via Node too)
    if (lowerCmd.find(L"claude-code") != std::wstring::npos ||
        lowerCmd.find(L"@anthropic") != std::wstring::npos) {
        return find_preset(L"claude");
    }

    // Check for Cursor
    if (lowerCmd.find(L"cursor") != std::wstring::npos) {
        return find_preset(L"cursor");
    }

    return nullptr;
}

// Walk up process tree to find a matching AI CLI preset
const AppPreset* detect_preset_from_ancestors(bool debug = false) {
    HandleGuard snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        return nullptr;
    }

    DWORD currentPid = GetCurrentProcessId();

    if (debug) {
        std::wcerr << L"[DEBUG] Starting from PID: " << currentPid << L"\n";
    }

    // Walk up the process tree (max 20 levels to avoid infinite loops)
    for (int depth = 0; depth < 20; depth++) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        DWORD parentPid = 0;

        // Find current process to get parent PID
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == currentPid) {
                    parentPid = pe32.th32ParentProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe32));
        }

        if (parentPid == 0 || parentPid == currentPid) {
            break;  // Reached root or loop
        }

        // Find parent process name
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == parentPid) {
                    // Extract just the filename without extension
                    std::wstring exeName = pe32.szExeFile;
                    size_t dotPos = exeName.find_last_of(L'.');
                    if (dotPos != std::wstring::npos) {
                        exeName = exeName.substr(0, dotPos);
                    }

                    // Convert to lowercase for matching
                    auto lowerExeName = to_lower(exeName);

                    // Get command line for this process
                    std::wstring cmdLine = get_process_command_line(parentPid);

                    if (debug) {
                        std::wcerr << L"[DEBUG] Level " << depth << L": PID=" << parentPid
                                   << L" Name=" << exeName << L"\n";
                        std::wcerr << L"[DEBUG]   CmdLine: " << (cmdLine.empty() ? L"(empty)" : cmdLine.substr(0, 100)) << L"\n";
                    }

                    // Check if this matches a preset by name
                    const AppPreset* preset = find_preset(lowerExeName);
                    if (preset) {
                        if (debug) std::wcerr << L"[DEBUG] MATCH by name: " << lowerExeName << L"\n";
                        return preset;
                    }

                    // Check command line for CLI patterns (handles node.exe, etc.)
                    preset = check_command_line_for_preset(cmdLine);
                    if (preset) {
                        if (debug) std::wcerr << L"[DEBUG] MATCH by cmdline\n";
                        return preset;
                    }

                    break;
                }
            } while (Process32NextW(snapshot, &pe32));
        }

        // Move up to parent
        currentPid = parentPid;
    }

    return nullptr;
}

void print_usage() {
    std::wcout << L"toasty - Windows toast notification CLI\n\n"
               << L"Usage:\n"
               << L"  toasty <message> [options]\n"
               << L"  toasty --install [agent]\n"
               << L"  toasty --uninstall\n"
               << L"  toasty --status\n\n"
               << L"Options:\n"
               << L"  -t, --title <text>   Set notification title (default: \"Notification\")\n"
               << L"  --app <name>         Use AI CLI preset (claude, copilot, gemini, codex, cursor)\n"
               << L"  -i, --icon <path>    Custom icon path (PNG recommended, 48x48px)\n"
               << L"  -h, --help           Show this help\n"
               << L"  --install [agent]    Install hooks for AI CLI agents (claude, gemini, copilot, or all)\n"
               << L"  --uninstall          Remove hooks from all AI CLI agents\n"
               << L"  --status             Show installation status\n"
               << L"  --register           Re-register app for notifications (troubleshooting)\n\n"
               << L"Note: Toasty auto-detects known parent processes (Claude, Copilot, etc.)\n"
               << L"      and applies the appropriate preset automatically. Use --app to override.\n\n"
               << L"Examples:\n"
               << L"  toasty \"Build completed\"\n"
               << L"  toasty \"Task done\" -t \"Custom Title\"\n"
               << L"  toasty \"Analysis complete\" --app claude\n"
               << L"  toasty --install\n"
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

// Register protocol handler for toast activation (click-to-focus)
bool register_protocol() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HKEY hKey;
    std::wstring protocolKey = std::wstring(L"Software\\Classes\\") + PROTOCOL_NAME;

    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, protocolKey.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    RegSetValueExW(hKey, L"URL Protocol", 0, REG_SZ, nullptr, 0);

    std::wstring description = L"URL:Toasty Protocol";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)description.c_str(),
                   (DWORD)((description.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    std::wstring commandKey = protocolKey + L"\\shell\\open\\command";
    result = RegCreateKeyExW(HKEY_CURRENT_USER, commandKey.c_str(), 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    std::wstring command = L"\"" + std::wstring(exePath) + L"\" --focus \"%1\"";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)command.c_str(),
                   (DWORD)((command.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    return true;
}

// Save the console window handle to registry for later retrieval
bool save_console_window_handle(HWND hwnd) {
    HKEY hKey;
    std::wstring regKey = std::wstring(L"Software\\") + APP_NAME;

    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, regKey.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) {
        return false;
    }

    ULONG_PTR handleValue = (ULONG_PTR)hwnd;
    RegSetValueExW(hKey, L"LastConsoleWindow", 0, REG_QWORD, (BYTE*)&handleValue, sizeof(ULONG_PTR));
    RegCloseKey(hKey);
    return true;
}

// Retrieve the saved console window handle from registry
HWND get_saved_console_window_handle() {
    HKEY hKey;
    std::wstring regKey = std::wstring(L"Software\\") + APP_NAME;

    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, regKey.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        return nullptr;
    }

    ULONG_PTR handleValue = 0;
    DWORD size = sizeof(ULONG_PTR);
    result = RegQueryValueExW(hKey, L"LastConsoleWindow", nullptr, nullptr, (BYTE*)&handleValue, &size);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        return nullptr;
    }

    HWND hwnd = (HWND)(ULONG_PTR)handleValue;
    if (IsWindow(hwnd)) {
        return hwnd;
    }

    return nullptr;
}

// Structure to pass data to EnumWindows callback
struct FindWindowData {
    DWORD targetPid;
    HWND foundWindow;
};

// Find window belonging to a specific process
HWND find_window_for_process(DWORD pid) {
    FindWindowData data = { pid, nullptr };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        FindWindowData* pData = (FindWindowData*)lParam;

        DWORD windowPid = 0;
        GetWindowThreadProcessId(hwnd, &windowPid);

        if (windowPid == pData->targetPid && IsWindowVisible(hwnd)) {
            // Check if it has a title (real window, not helper)
            wchar_t title[256];
            if (GetWindowTextW(hwnd, title, 256) > 0) {
                pData->foundWindow = hwnd;
                return FALSE; // Stop enumeration
            }
        }
        return TRUE;
    }, (LPARAM)&data);

    return data.foundWindow;
}

// Walk process tree to find the terminal/IDE window that launched us
HWND find_ancestor_window() {
    HandleGuard snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        return nullptr;
    }

    DWORD currentPid = GetCurrentProcessId();

    // Walk up the process tree (max 20 levels)
    for (int depth = 0; depth < 20; depth++) {
        PROCESSENTRY32W pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32W);
        DWORD parentPid = 0;

        // Find current process to get parent PID
        if (Process32FirstW(snapshot, &pe32)) {
            do {
                if (pe32.th32ProcessID == currentPid) {
                    parentPid = pe32.th32ParentProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &pe32));
        }

        if (parentPid == 0 || parentPid == currentPid) {
            break;
        }

        // Check if this parent has a visible window
        HWND hwnd = find_window_for_process(parentPid);
        if (hwnd) {
            return hwnd;
        }

        currentPid = parentPid;
    }

    return nullptr;
}

// Helper to forcefully bring a window to foreground (works around Windows restrictions)
bool force_foreground_window(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return false;
    }

    // Get the foreground window's thread
    DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD currentThread = GetCurrentThreadId();

    // Attach to the foreground thread to get input focus permission
    if (foregroundThread != currentThread) {
        AttachThreadInput(currentThread, foregroundThread, TRUE);
    }

    // Restore if minimized
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    // Try multiple methods to bring window to front
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);

    // Simulate a keypress to help with focus (empty ALT press)
    keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY | 0, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);

    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    // Detach from foreground thread
    if (foregroundThread != currentThread) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }

    return true;
}

bool focus_console_window() {
    // First try: saved console window handle from registry (most reliable)
    HWND savedWindow = get_saved_console_window_handle();
    if (savedWindow != nullptr) {
        return force_foreground_window(savedWindow);
    }

    // Second try: enumerate windows to find a console or Windows Terminal
    HWND foundWindow = nullptr;
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        wchar_t className[256];
        GetClassNameW(hwnd, className, 256);

        if (!IsWindowVisible(hwnd)) {
            return TRUE;
        }

        if (wcscmp(className, L"ConsoleWindowClass") == 0 ||
            wcscmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0) {
            HWND* pResult = (HWND*)lParam;
            *pResult = hwnd;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&foundWindow);

    if (foundWindow != nullptr) {
        return force_foreground_window(foundWindow);
    }

    return false;
}

// Escape backslashes and quotes for JSON strings
std::wstring escape_json_string(const std::wstring& str) {
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

// Get the full path to the current executable
std::wstring get_exe_path() {
    wchar_t exePath[MAX_PATH];
    DWORD result = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (result == 0 || result == MAX_PATH) {
        // Failed to get exe path, return empty string
        return L"";
    }
    return std::wstring(exePath);
}

// Expand environment variables in a path
std::wstring expand_env(const std::wstring& path) {
    wchar_t expanded[MAX_PATH];
    DWORD result = ExpandEnvironmentStringsW(path.c_str(), expanded, MAX_PATH);
    if (result == 0 || result > MAX_PATH) {
        // Failed to expand, return original path
        return path;
    }
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
    // Check for .github directory AND hooks subdirectory as more specific indicator
    return path_exists(L".github\\hooks") || path_exists(L".github");
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
        // Convert narrow string to wide string
        int size = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, nullptr, 0);
        if (size > 0) {
            std::wstring wideMsg(size - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, &wideMsg[0], size);
            std::wcerr << L"Warning: Failed to create backup: " << wideMsg << L"\n";
        } else {
            std::wcerr << L"Warning: Failed to create backup\n";
        }
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
    
    // Build hook structure with nested "hooks" array (required by Claude Code)
    JsonObject innerHook;
    innerHook.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));

    std::wstring escapedPath = escape_json_string(exePath);
    std::wstring command = escapedPath + L" \"Task complete\" -t \"Claude Code\"";
    innerHook.SetNamedValue(L"command", JsonValue::CreateStringValue(command));
    innerHook.SetNamedValue(L"timeout", JsonValue::CreateNumberValue(5000));

    JsonArray innerHooks;
    innerHooks.Append(innerHook);

    JsonObject hookItem;
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
    
    // Build hook structure with nested "hooks" array (required by Gemini CLI)
    // Structure: hooks -> AfterAgent -> [ { hooks: [ { command: ... } ] } ]
    JsonObject innerHook;
    innerHook.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));

    std::wstring escapedPath = escape_json_string(exePath);
    std::wstring command = escapedPath + L" \"Gemini finished\" -t \"Gemini\"";
    innerHook.SetNamedValue(L"command", JsonValue::CreateStringValue(command));
    innerHook.SetNamedValue(L"timeout", JsonValue::CreateNumberValue(5000));

    JsonArray innerHooks;
    innerHooks.Append(innerHook);

    JsonObject hookItem;
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
        // Check if our hook is already installed (check nested structure)
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
    
    // Get or create hooks object
    JsonObject hooksObj;
    if (rootObj.HasKey(L"hooks")) {
        hooksObj = rootObj.GetNamedObject(L"hooks");
    }
    
    // Helper lambda to add hook to an array if not already present
    auto addHookIfMissing = [&](const std::wstring& hookType, const std::wstring& message) -> bool {
        JsonArray hookArray;
        if (hooksObj.HasKey(hookType)) {
            hookArray = hooksObj.GetNamedArray(hookType);
            // Check if toasty is already there
            for (const auto& item : hookArray) {
                if (item.ValueType() == JsonValueType::Object) {
                    auto obj = item.GetObject();
                    if (obj.HasKey(L"bash")) {
                        std::wstring bash = obj.GetNamedString(L"bash").c_str();
                        if (bash.find(L"toasty") != std::wstring::npos) {
                            return false; // Already present
                        }
                    }
                }
            }
        }
        
        // Build hook structure for this hook type
        JsonObject hookObj;
        hookObj.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
        
        // For bash, use forward slashes and toasty (assuming it's in PATH)
        std::wstring bashCmd = L"toasty '" + message + L"' -t 'GitHub Copilot'";
        hookObj.SetNamedValue(L"bash", JsonValue::CreateStringValue(bashCmd));
        
        // For PowerShell, use the full exe path with escaped backslashes
        std::wstring escapedPath = escape_json_string(exePath);
        std::wstring psCommand = escapedPath + L" '" + message + L"' -t 'GitHub Copilot'";
        hookObj.SetNamedValue(L"powershell", JsonValue::CreateStringValue(psCommand));
        
        hookObj.SetNamedValue(L"timeoutSec", JsonValue::CreateNumberValue(5));
        
        hookArray.Append(hookObj);
        hooksObj.SetNamedValue(hookType, hookArray);
        return true; // Added
    };
    
    // Add to both sessionStart and sessionEnd
    // sessionStart fires on new sessions AND resumed sessions (works with --resume)
    // sessionEnd only fires when sessions end (known issue: doesn't fire with --resume)
    bool addedStart = addHookIfMissing(L"sessionStart", L"Copilot task complete");
    bool addedEnd = addHookIfMissing(L"sessionEnd", L"Copilot session ended");
    
    // If neither was added, already installed
    if (!addedStart && !addedEnd) {
        return true;
    }
    
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
            
            // Check both sessionStart and sessionEnd
            // sessionStart is needed for --resume support
            std::vector<std::wstring> hookTypes = {L"sessionStart", L"sessionEnd"};
            for (const auto& hookType : hookTypes) {
                if (hooksObj.HasKey(hookType)) {
                    auto array = hooksObj.GetNamedArray(hookType);
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

            // Check direct command field (correct format)
            if (hookObj.HasKey(L"command")) {
                std::wstring cmd = hookObj.GetNamedString(L"command").c_str();
                if (cmd.find(L"toasty") != std::wstring::npos) {
                    isToasty = true;
                }
            }
            // Check nested hooks array (legacy format)
            if (!isToasty && hookObj.HasKey(L"hooks")) {
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
            }
            // Check bash field (Copilot format)
            if (!isToasty && hookObj.HasKey(L"bash")) {
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
    } catch (const std::exception& e) {
        // Convert narrow string to wide string
        int size = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, nullptr, 0);
        if (size > 0) {
            std::wstring wideMsg(size - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, &wideMsg[0], size);
            std::wcerr << L"Error uninstalling Copilot hook: " << wideMsg << L"\n";
        } else {
            std::wcerr << L"Error uninstalling Copilot hook\n";
        }
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
    
    if (exePath.empty()) {
        std::wcerr << L"Error: Could not determine toasty.exe path\n";
        return;
    }
    
    bool installAll = agent.empty() || agent == L"all";
    bool explicitAgent = !installAll;  // User explicitly named an agent
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

    // If user explicitly named an agent, install even if not detected
    if (installClaude && (claudeDetected || explicitAgent)) {
        if (install_claude(exePath)) {
            std::wcout << L"  [x] Claude Code: Added Stop hook\n";
            anyInstalled = true;
        } else {
            std::wcout << L"  [ ] Claude Code: Failed to install\n";
        }
    }
    
    if (installGemini && (geminiDetected || explicitAgent)) {
        if (install_gemini(exePath)) {
            std::wcout << L"  [x] Gemini CLI: Added AfterAgent hook\n";
            anyInstalled = true;
        } else {
            std::wcout << L"  [ ] Gemini CLI: Failed to install\n";
        }
    }
    
    if (installCopilot && (copilotDetected || explicitAgent)) {
        if (install_copilot(exePath)) {
            std::wcout << L"  [x] GitHub Copilot: Added sessionStart and sessionEnd hooks\n";
            std::wcout << L"      Note: This is repo-level only, not global\n";
            std::wcout << L"      Note: sessionStart works with --resume, sessionEnd does not (GitHub Copilot limitation)\n";
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
        // Also register the protocol handler for click-to-focus
        register_protocol();
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
    // Always ensure protocol handler is registered for click-to-focus
    register_protocol();

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

    if (SUCCEEDED(hr)) {
        // Also register the protocol handler for click-to-focus
        register_protocol();
    }
    return SUCCEEDED(hr);
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    std::wstring message;
    std::wstring title = L"Notification";
    std::wstring iconPath;
    bool doInstall = false;
    bool doUninstall = false;
    bool doStatus = false;
    bool doFocus = false;
    bool doRegister = false;
    std::wstring installAgent;
    bool explicitApp = false;  // Track if user explicitly set --app
    bool explicitTitle = false; // Track if user explicitly set -t
    bool debug = false;

    // Quick scan for --debug flag
    for (int i = 1; i < argc; i++) {
        if (std::wstring(argv[i]) == L"--debug") {
            debug = true;
            break;
        }
    }

    // Auto-detect parent process and apply preset if found
    const AppPreset* autoPreset = detect_preset_from_ancestors(debug);
    if (autoPreset) {
        title = autoPreset->title;
        iconPath = extract_icon_to_temp(autoPreset->iconResourceId);
    } else {
        // No AI agent detected - use toasty mascot as default icon
        iconPath = extract_icon_to_temp(IDI_TOASTY);
    }

    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];

        if (arg == L"-h" || arg == L"--help") {
            print_usage();
            return 0;
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
        else if (arg == L"--focus") {
            doFocus = true;
        }
        else if (arg == L"--register") {
            doRegister = true;
        }
        else if (arg == L"-t" || arg == L"--title") {
            if (i + 1 < argc) {
                title = argv[++i];
                explicitTitle = true;
            } else {
                std::wcerr << L"Error: --title requires an argument\n";
                return 1;
            }
        }
        else if (arg == L"--app") {
            if (i + 1 < argc) {
                std::wstring appName = argv[++i];
                const AppPreset* preset = find_preset(appName);
                if (preset) {
                    // Only override title if user hasn't explicitly set it
                    if (!explicitTitle) {
                        title = preset->title;
                    }
                    iconPath = extract_icon_to_temp(preset->iconResourceId);
                    if (iconPath.empty()) {
                        std::wcerr << L"Warning: Failed to extract icon for preset '" << appName << L"'\n";
                    }
                    explicitApp = true;
                } else {
                    std::wcerr << L"Error: Unknown app preset '" << appName << L"'\n";
                    std::wcerr << L"Available presets: claude, copilot, gemini, codex, cursor\n";
                    return 1;
                }
            } else {
                std::wcerr << L"Error: --app requires an argument\n";
                return 1;
            }
        }
        else if (arg == L"-i" || arg == L"--icon") {
            if (i + 1 < argc) {
                iconPath = argv[++i];
                // Convert relative path to absolute
                try {
                    std::filesystem::path p(iconPath);
                    if (!p.is_absolute()) {
                        p = std::filesystem::absolute(p);
                    }
                    iconPath = p.wstring();
                } catch (const std::filesystem::filesystem_error& e) {
                    std::wcerr << L"Warning: Could not resolve icon path, using as-is\n";
                    // iconPath already set, continue with original path
                }
            } else {
                std::wcerr << L"Error: --icon requires an argument\n";
                return 1;
            }
        }
        else if (arg[0] != L'-' && message.empty()) {
            message = arg;
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

    if (doFocus) {
        // Called by protocol handler when toast is clicked
        // Detach from console entirely to prevent flash
        FreeConsole();

        // Protocol handlers have focus restrictions - use aggressive approach
        HWND targetWnd = get_saved_console_window_handle();
        if (!targetWnd) {
            // Fallback: find any terminal window
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                wchar_t className[256];
                GetClassNameW(hwnd, className, 256);
                if (IsWindowVisible(hwnd) &&
                    (wcscmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0 ||
                     wcscmp(className, L"ConsoleWindowClass") == 0)) {
                    *(HWND*)lParam = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, (LPARAM)&targetWnd);
        }

        if (targetWnd) {
            // Simulate user input to bypass focus restrictions
            INPUT input = {0};
            input.type = INPUT_MOUSE;
            SendInput(1, &input, sizeof(INPUT));

            // Restore if minimized
            if (IsIconic(targetWnd)) {
                ShowWindow(targetWnd, SW_RESTORE);
            }

            // Try SwitchToThisWindow (works better for protocol handlers)
            SwitchToThisWindow(targetWnd, TRUE);

            // Also try standard approach
            force_foreground_window(targetWnd);
            return 0;
        }

        return 1;
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

        // Save the terminal window handle for click-to-focus
        // Walk process tree to find the actual terminal/IDE window
        HWND terminalWnd = find_ancestor_window();

        // Fallback: if process tree didn't find a window, search for any terminal
        if (!terminalWnd) {
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                wchar_t className[256];
                GetClassNameW(hwnd, className, 256);
                if (IsWindowVisible(hwnd) &&
                    (wcscmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0 ||
                     wcscmp(className, L"ConsoleWindowClass") == 0)) {
                    *(HWND*)lParam = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, (LPARAM)&terminalWnd);
        }

        if (terminalWnd) {
            save_console_window_handle(terminalWnd);
        }

        // Build toast XML with protocol activation for click-to-focus
        std::wstring xml = L"<toast activationType=\"protocol\" launch=\"toasty://focus\"><visual><binding template=\"ToastGeneric\">";
        
        // Add icon if provided
        if (!iconPath.empty()) {
            xml += L"<image placement=\"appLogoOverride\" src=\"" + escape_xml(iconPath) + L"\"/>";
        }
        
        xml += L"<text>" + escape_xml(title) + L"</text>"
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
