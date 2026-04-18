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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <tlhelp32.h>
#include <winhttp.h>
#include <knownfolders.h>
#include "resource.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")

using namespace winrt;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Data::Json;
using namespace Windows::UI::Notifications;
namespace fs = std::filesystem;

const wchar_t* APP_ID = L"Toasty.CLI.Notification";
const wchar_t* APP_NAME = L"Toasty";
const wchar_t* PROTOCOL_NAME = L"toasty";
const wchar_t* TOASTY_VERSION = L"0.6";

// Global flags
bool g_dryRun = false;

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
               << L"  -v, --version        Show version and exit\n"
               << L"  -h, --help           Show this help\n"
               << L"  --install [agent]    Install hooks for AI CLI agents (claude, gemini, copilot, codex, or all)\n"
               << L"  --global             With --install copilot: install user-global hook (~/.copilot/hooks/) instead of per-repo\n"
               << L"  --no-session-end     With --install copilot: skip the sessionEnd hook (no toast on /exit or Ctrl+C)\n"
               << L"  --uninstall          Remove hooks from all AI CLI agents\n"
               << L"  --status             Show installation status\n"
               << L"  --register           Re-register app for notifications (troubleshooting)\n"
               << L"  --dry-run            Show what would happen without executing side effects\n\n"
               << L"Push Notifications:\n"
               << L"  Set TOASTY_NTFY_TOPIC to send push notifications to your phone via ntfy.sh.\n"
               << L"  Set TOASTY_NTFY_SERVER to use a self-hosted ntfy server (default: ntfy.sh).\n\n"
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

// Send push notification via ntfy.sh (non-blocking, fire-and-forget)
// Only sends if TOASTY_NTFY_TOPIC env var is set.
// Uses TOASTY_NTFY_SERVER env var for custom server (default: ntfy.sh).
// Timeout is aggressive (5 seconds) so it never blocks the CLI.
void send_ntfy_notification(const std::wstring& title, const std::wstring& message) {
    // Check for topic
    wchar_t topicBuf[256] = {};
    if (!GetEnvironmentVariableW(L"TOASTY_NTFY_TOPIC", topicBuf, 256) || topicBuf[0] == L'\0') {
        return;  // ntfy not configured, silently skip
    }
    std::wstring topic(topicBuf);

    // Check for custom server (default: ntfy.sh)
    wchar_t serverBuf[256] = {};
    std::wstring server = L"ntfy.sh";
    if (GetEnvironmentVariableW(L"TOASTY_NTFY_SERVER", serverBuf, 256) && serverBuf[0] != L'\0') {
        server = serverBuf;
    }

    // Build the path: /<topic>
    std::wstring path = L"/" + topic;

    // Convert title and message to UTF-8 for HTTP body
    std::string bodyUtf8;
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            bodyUtf8.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, &bodyUtf8[0], size, nullptr, nullptr);
        }
    }
    std::string titleUtf8;
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, title.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            titleUtf8.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, title.c_str(), -1, &titleUtf8[0], size, nullptr, nullptr);
        }
    }

    // Fire-and-forget HTTP POST with aggressive timeout
    HINTERNET hSession = WinHttpOpen(L"Toasty/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;

    // Set timeouts: 3s resolve, 3s connect, 5s send, 5s receive
    WinHttpSetTimeouts(hSession, 3000, 3000, 5000, 5000);

    HINTERNET hConnect = WinHttpConnect(hSession, server.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    // Add title header
    std::wstring titleHeader = L"Title: " + title;
    WinHttpAddRequestHeaders(hRequest, titleHeader.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    // Send the request (body is the message)
    BOOL sent = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (LPVOID)bodyUtf8.c_str(), (DWORD)bodyUtf8.size(), (DWORD)bodyUtf8.size(), 0);

    if (sent) {
        WinHttpReceiveResponse(hRequest, nullptr);  // Wait for response (respects timeout)
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

// Check if we should check for updates (throttle to once per day)
bool should_check_for_updates() {
    HKEY hKey;
    std::wstring regKey = std::wstring(L"Software\\") + APP_NAME;

    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, regKey.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) return true;  // No key yet, check

    FILETIME lastCheck = {};
    DWORD size = sizeof(FILETIME);
    result = RegQueryValueExW(hKey, L"LastUpdateCheck", nullptr, nullptr, (BYTE*)&lastCheck, &size);
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) return true;  // No value yet, check

    // Get current time
    FILETIME now;
    GetSystemTimeAsFileTime(&now);

    // Convert to ULARGE_INTEGER for arithmetic (100-nanosecond intervals)
    ULARGE_INTEGER ulNow, ulLast;
    ulNow.LowPart = now.dwLowDateTime;
    ulNow.HighPart = now.dwHighDateTime;
    ulLast.LowPart = lastCheck.dwLowDateTime;
    ulLast.HighPart = lastCheck.dwHighDateTime;

    // 24 hours in 100-nanosecond intervals
    ULONGLONG dayInterval = 24ULL * 60 * 60 * 10000000;
    return (ulNow.QuadPart - ulLast.QuadPart) >= dayInterval;
}

// Save the last update check timestamp
void save_update_check_time() {
    HKEY hKey;
    std::wstring regKey = std::wstring(L"Software\\") + APP_NAME;

    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, regKey.c_str(), 0, nullptr,
                                   REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return;

    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    RegSetValueExW(hKey, L"LastUpdateCheck", 0, REG_BINARY, (BYTE*)&now, sizeof(FILETIME));
    RegCloseKey(hKey);
}

// Compare version strings (e.g., "0.3" vs "0.4")
// Returns true if remoteVersion is newer than localVersion
bool is_newer_version(const std::wstring& localVersion, const std::wstring& remoteVersion) {
    // Strip leading 'v' if present
    std::wstring local = localVersion;
    std::wstring remote = remoteVersion;
    if (!local.empty() && (local[0] == L'v' || local[0] == L'V')) local = local.substr(1);
    if (!remote.empty() && (remote[0] == L'v' || remote[0] == L'V')) remote = remote.substr(1);

    // Parse major.minor
    auto parse = [](const std::wstring& v, int& major, int& minor) {
        major = 0; minor = 0;
        size_t dot = v.find(L'.');
        if (dot != std::wstring::npos) {
            try {
                major = std::stoi(v.substr(0, dot));
                minor = std::stoi(v.substr(dot + 1));
            } catch (...) {}
        } else {
            try { major = std::stoi(v); } catch (...) {}
        }
    };

    int localMajor, localMinor, remoteMajor, remoteMinor;
    parse(local, localMajor, localMinor);
    parse(remote, remoteMajor, remoteMinor);

    if (remoteMajor > localMajor) return true;
    if (remoteMajor == localMajor && remoteMinor > localMinor) return true;
    return false;
}

// Check GitHub releases for a newer version (non-blocking, throttled)
// Returns true if an update toast was shown
bool check_for_updates() {
    if (!should_check_for_updates()) return false;

    save_update_check_time();  // Save now so we don't retry on failure

    // Hit GitHub API: GET /repos/shanselman/toasty/releases/latest
    HINTERNET hSession = WinHttpOpen(L"Toasty/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    WinHttpSetTimeouts(hSession, 3000, 3000, 5000, 5000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
        L"/repos/shanselman/toasty/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // GitHub API requires User-Agent
    WinHttpAddRequestHeaders(hRequest, L"Accept: application/vnd.github+json", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL sent = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    bool updateAvailable = false;

    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        // Read response body
        std::string responseBody;
        DWORD bytesRead = 0;
        DWORD bytesAvailable = 0;

        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
            std::vector<char> buffer(bytesAvailable);
            if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                responseBody.append(buffer.data(), bytesRead);
            }
            // Cap response size to prevent memory issues
            if (responseBody.size() > 32768) break;
        }

        // Parse tag_name from JSON response using WinRT JSON parser
        try {
            int wideSize = MultiByteToWideChar(CP_UTF8, 0, responseBody.c_str(), -1, nullptr, 0);
            if (wideSize > 0) {
                std::wstring wideResponse(wideSize - 1, 0);
                MultiByteToWideChar(CP_UTF8, 0, responseBody.c_str(), -1, &wideResponse[0], wideSize);

                JsonObject json;
                if (JsonObject::TryParse(wideResponse, json)) {
                    std::wstring tagName = json.GetNamedString(L"tag_name", L"").c_str();
                    std::wstring releaseName = json.GetNamedString(L"name", L"").c_str();
                    std::wstring htmlUrl = json.GetNamedString(L"html_url", L"").c_str();

                    if (!tagName.empty() && is_newer_version(TOASTY_VERSION, tagName)) {
                        // Print to stderr
                        std::wcerr << L"Update available: v" << TOASTY_VERSION
                                   << L" → " << tagName
                                   << L" (https://github.com/shanselman/toasty/releases)\n";

                        // Show a toast notification about the update
                        try {
                            std::wstring updateXml =
                                L"<toast activationType=\"protocol\" launch=\"https://github.com/shanselman/toasty/releases\">"
                                L"<visual><binding template=\"ToastGeneric\">"
                                L"<text>Toasty Update Available</text>"
                                L"<text>v" + std::wstring(TOASTY_VERSION) + L" → " + tagName + L" — click to download</text>"
                                L"</binding></visual></toast>";

                            XmlDocument updateDoc;
                            updateDoc.LoadXml(updateXml);
                            ToastNotification updateToast(updateDoc);
                            auto notifier = ToastNotificationManager::CreateToastNotifier(APP_ID);
                            notifier.Show(updateToast);
                        } catch (...) {
                            // Update toast failed — not critical
                        }

                        updateAvailable = true;
                    }
                }
            }
        } catch (...) {
            // JSON parse failed — silently ignore
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return updateAvailable;
}
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

bool detect_codex() {
    std::wstring codexPath = expand_env(L"%USERPROFILE%\\.codex");
    return path_exists(codexPath);
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
    // Structure: hooks -> AfterAgent -> [ { matcher: "*", hooks: [ { command: ... } ] } ]
    JsonObject innerHook;
    innerHook.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
    innerHook.SetNamedValue(L"name", JsonValue::CreateStringValue(L"toasty-notification"));

    std::wstring escapedPath = escape_json_string(exePath);
    std::wstring command = escapedPath + L" \"Gemini finished\" -t \"Gemini\"";
    innerHook.SetNamedValue(L"command", JsonValue::CreateStringValue(command));

    JsonArray innerHooks;
    innerHooks.Append(innerHook);

    JsonObject hookItem;
    hookItem.SetNamedValue(L"matcher", JsonValue::CreateStringValue(L"*"));
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
// Returns the path to the per-repo Copilot hook config (relative to cwd).
std::wstring copilot_repo_config_path() {
    return L".github\\hooks\\toasty.json";
}

// Returns the path to the user-global Copilot hook config, or empty on
// failure to resolve the home directory.
std::wstring copilot_global_config_path() {
    // Prefer %USERPROFILE% so test harnesses (and users) can override the
    // target via the standard Windows env var. Fall back to the canonical
    // shell path if it's unset.
    std::wstring home = expand_env(L"%USERPROFILE%");
    if (home.empty()) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &p))) {
            home = p;
            CoTaskMemFree(p);
        }
    }
    if (home.empty()) return L"";
    return home + L"\\.copilot\\hooks\\toasty.json";
}

bool install_copilot_at(const std::wstring& exePath, const std::wstring& configPath,
                        bool noSessionEnd) {
    if (configPath.empty()) return false;

    fs::path cfg(configPath);
    std::error_code ec;
    fs::create_directories(cfg.parent_path(), ec);
    if (ec) return false;

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

    rootObj.SetNamedValue(L"version", JsonValue::CreateNumberValue(1));

    JsonObject hooksObj;
    if (rootObj.HasKey(L"hooks")) {
        hooksObj = rootObj.GetNamedObject(L"hooks");
    }

    auto buildHook = [&](const std::wstring& event) {
        JsonObject h;
        h.SetNamedValue(L"type", JsonValue::CreateStringValue(L"command"));
        std::wstring bash = L"toasty --copilot-hook " + event;
        std::wstring ps = exePath + L" --copilot-hook " + event;
        h.SetNamedValue(L"bash", JsonValue::CreateStringValue(bash.c_str()));
        h.SetNamedValue(L"powershell", JsonValue::CreateStringValue(ps.c_str()));
        h.SetNamedValue(L"timeoutSec", JsonValue::CreateNumberValue(5));
        return h;
    };

    auto isToastyEntry = [](const JsonObject& o) {
        auto check = [&](const wchar_t* key) {
            if (!o.HasKey(key)) return false;
            try {
                std::wstring v = o.GetNamedString(key).c_str();
                return v.find(L"toasty") != std::wstring::npos;
            } catch (...) { return false; }
        };
        return check(L"bash") || check(L"powershell") || check(L"command");
    };

    auto upsertHook = [&](const wchar_t* eventName, JsonObject newEntry) {
        JsonArray arr;
        if (hooksObj.HasKey(eventName)) {
            try { arr = hooksObj.GetNamedArray(eventName); }
            catch (...) { arr = JsonArray(); }
        }
        JsonArray cleaned;
        for (const auto& item : arr) {
            if (item.ValueType() != JsonValueType::Object) {
                cleaned.Append(item);
                continue;
            }
            auto o = item.GetObject();
            if (!isToastyEntry(o)) cleaned.Append(item);
        }
        cleaned.Append(newEntry);
        hooksObj.SetNamedValue(eventName, cleaned);
    };

    // Remove any toasty entry for an event without re-adding one. If the
    // resulting array is empty, drop the event key entirely so the config
    // stays clean.
    auto removeToastyHook = [&](const wchar_t* eventName) {
        if (!hooksObj.HasKey(eventName)) return;
        JsonArray arr;
        try { arr = hooksObj.GetNamedArray(eventName); }
        catch (...) { return; }
        JsonArray cleaned;
        for (const auto& item : arr) {
            if (item.ValueType() != JsonValueType::Object) {
                cleaned.Append(item);
                continue;
            }
            auto o = item.GetObject();
            if (!isToastyEntry(o)) cleaned.Append(item);
        }
        if (cleaned.Size() == 0) hooksObj.Remove(eventName);
        else hooksObj.SetNamedValue(eventName, cleaned);
    };

    if (noSessionEnd) {
        removeToastyHook(L"sessionEnd");
    } else {
        upsertHook(L"sessionEnd", buildHook(L"end"));
    }
    upsertHook(L"userPromptSubmitted", buildHook(L"prompt"));
    upsertHook(L"postToolUse", buildHook(L"tool"));

    rootObj.SetNamedValue(L"hooks", hooksObj);

    std::string jsonStr = from_hstring(rootObj.Stringify());
    return write_file(configPath, jsonStr);
}

bool install_copilot(const std::wstring& exePath, bool noSessionEnd = false) {
    return install_copilot_at(exePath, copilot_repo_config_path(), noSessionEnd);
}

bool install_copilot_global(const std::wstring& exePath, bool noSessionEnd = false) {
    return install_copilot_at(exePath, copilot_global_config_path(), noSessionEnd);
}

// =============================================================
// Copilot rich notification helpers
// =============================================================

// Read all of stdin as UTF-8. Returns empty string if stdin is the console
// (avoids hanging when invoked manually) or on read failure.
std::string read_stdin_utf8() {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr) return "";
    DWORD type = GetFileType(h);
    // Only read from a pipe or redirected file. FILE_TYPE_CHAR == interactive
    // console: do not read (would block forever).
    if (type != FILE_TYPE_PIPE && type != FILE_TYPE_DISK) return "";

    std::string out;
    char buf[4096];
    DWORD bytesRead = 0;
    while (ReadFile(h, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        out.append(buf, buf + bytesRead);
    }
    return out;
}

// FNV-1a 64-bit hash of a wstring (case-folded ASCII), as 16-hex-char string.
std::wstring fnv1a_hex(const std::wstring& s) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (wchar_t c : s) {
        wchar_t lc = (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c;
        h ^= (uint64_t)lc;
        h *= 0x100000001b3ULL;
    }
    wchar_t buf[17];
    swprintf(buf, 17, L"%016llx", (unsigned long long)h);
    return std::wstring(buf);
}

// Get %LocalAppData% via SHGetKnownFolderPath. Empty on failure.
std::wstring get_local_appdata() {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        std::wstring r(path);
        CoTaskMemFree(path);
        return r;
    }
    return L"";
}

std::wstring copilot_cache_dir() {
    std::wstring base = get_local_appdata();
    if (base.empty()) return L"";
    return base + L"\\toasty\\copilot-prompts";
}

// =============================================================
// Idle watchdog: postToolUse debounce -> toast on N seconds of inactivity
// =============================================================

// Forward declarations for helpers defined further down.
std::wstring normalize_ws(const std::wstring& in);
std::wstring truncate_w(const std::wstring& s, size_t maxChars);
std::wstring folder_name_of(const std::wstring& cwd);

std::wstring copilot_pending_dir() {
    std::wstring base = get_local_appdata();
    if (base.empty()) return L"";
    return base + L"\\toasty\\copilot-pending";
}

std::wstring copilot_pending_path(const std::wstring& cwd) {
    std::wstring dir = copilot_pending_dir();
    if (dir.empty()) return L"";
    return dir + L"\\" + fnv1a_hex(cwd) + L".json";
}

std::wstring copilot_pending_path_by_hash(const std::wstring& hash) {
    std::wstring dir = copilot_pending_dir();
    if (dir.empty()) return L"";
    return dir + L"\\" + hash + L".json";
}

std::wstring copilot_lock_path_by_hash(const std::wstring& hash) {
    std::wstring dir = copilot_pending_dir();
    if (dir.empty()) return L"";
    return dir + L"\\" + hash + L".lock";
}

// Idle threshold in seconds. Reads TOASTY_COPILOT_IDLE_SEC env var; default 6.
unsigned copilot_idle_seconds() {
    wchar_t buf[32] = {};
    if (GetEnvironmentVariableW(L"TOASTY_COPILOT_IDLE_SEC", buf, 32) > 0) {
        wchar_t* end = nullptr;
        unsigned long v = wcstoul(buf, &end, 10);
        if (v >= 1 && v <= 3600) return (unsigned)v;
    }
    return 6;
}

uint64_t now_ms() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// Atomically write a JSON pending record. Best-effort.
bool write_pending(const std::wstring& cwd, uint64_t fireMs,
                   const std::wstring& prompt, const std::wstring& sessionName,
                   uint64_t hwnd) {
    std::wstring path = copilot_pending_path(cwd);
    if (path.empty()) return false;
    std::error_code ec;
    fs::create_directories(copilot_pending_dir(), ec);

    JsonObject o;
    o.SetNamedValue(L"fireMs", JsonValue::CreateNumberValue((double)fireMs));
    o.SetNamedValue(L"cwd", JsonValue::CreateStringValue(cwd.c_str()));
    o.SetNamedValue(L"prompt", JsonValue::CreateStringValue(prompt.c_str()));
    o.SetNamedValue(L"sessionName", JsonValue::CreateStringValue(sessionName.c_str()));
    o.SetNamedValue(L"hwnd", JsonValue::CreateNumberValue((double)hwnd));
    std::string json = from_hstring(o.Stringify());

    // Write to <path>.tmp then rename for atomicity.
    std::wstring tmp = path + L".tmp";
    if (!write_file(tmp, json)) return false;
    fs::rename(tmp, path, ec);
    if (ec) {
        // Fallback: write directly (last-write-wins).
        return write_file(path, json);
    }
    return true;
}

struct PendingRecord {
    bool exists = false;
    uint64_t fireMs = 0;
    std::wstring cwd;
    std::wstring prompt;
    std::wstring sessionName;
    uint64_t hwnd = 0;
};

PendingRecord read_pending_by_hash(const std::wstring& hash) {
    PendingRecord r;
    std::wstring path = copilot_pending_path_by_hash(hash);
    if (path.empty() || !path_exists(path)) return r;
    std::string raw = read_file(path);
    if (raw.empty()) return r;
    try {
        JsonObject o = JsonObject::Parse(to_hstring(raw));
        if (o.HasKey(L"fireMs")) r.fireMs = (uint64_t)o.GetNamedNumber(L"fireMs");
        if (o.HasKey(L"cwd"))    r.cwd    = std::wstring(o.GetNamedString(L"cwd"));
        if (o.HasKey(L"prompt")) r.prompt = std::wstring(o.GetNamedString(L"prompt"));
        if (o.HasKey(L"sessionName"))
            r.sessionName = std::wstring(o.GetNamedString(L"sessionName"));
        if (o.HasKey(L"hwnd")) r.hwnd = (uint64_t)o.GetNamedNumber(L"hwnd");
        r.exists = true;
    } catch (...) {}
    return r;
}

void cancel_pending(const std::wstring& cwd) {
    std::wstring path = copilot_pending_path(cwd);
    if (path.empty()) return;
    std::error_code ec;
    fs::remove(path, ec);
}

// Spawn a detached watchdog process for a given cwd hash. Best-effort.
void spawn_watchdog(const std::wstring& cwdHash) {
    std::wstring exe = get_exe_path();
    if (exe.empty()) return;

    std::wstring cmd = L"\"" + exe + L"\" --copilot-watchdog " + cwdHash;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB;
    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                       flags, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

// Run the watchdog loop for one cwd. Acquires a single-instance lock; if
// another watchdog is already running for this cwd, exits immediately.
// Polls the pending file; when fireMs <= now AND the file still exists,
// spawns `toasty.exe --app copilot --title <t> "<msg>"` and exits.
int run_copilot_watchdog(const std::wstring& hash) {
    std::wstring lockPath = copilot_lock_path_by_hash(hash);
    if (lockPath.empty()) return 0;

    // Ensure parent dir exists for the lock too.
    std::error_code ec;
    fs::create_directories(copilot_pending_dir(), ec);

    HANDLE lock = CreateFileW(lockPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
                              nullptr);
    if (lock == INVALID_HANDLE_VALUE) {
        // Another watchdog instance owns this cwd. That instance will pick up
        // the refreshed fireMs on its next poll, so just exit.
        return 0;
    }

    auto release = [&]() {
        if (lock != INVALID_HANDLE_VALUE) {
            CloseHandle(lock);  // FILE_FLAG_DELETE_ON_CLOSE removes the file.
            lock = INVALID_HANDLE_VALUE;
        }
    };

    const uint64_t maxLifetimeMs = 60ULL * 60ULL * 1000ULL;  // 1 hour cap
    uint64_t startMs = now_ms();

    PendingRecord toFire;
    while (true) {
        if (now_ms() - startMs > maxLifetimeMs) {
            release();
            return 0;
        }
        PendingRecord rec = read_pending_by_hash(hash);
        if (!rec.exists) {
            release();
            return 0;
        }
        uint64_t now = now_ms();
        if (rec.fireMs > now) {
            uint64_t wait = rec.fireMs - now;
            if (wait > 1000) wait = 1000;
            Sleep((DWORD)wait);
            continue;
        }
        toFire = rec;
        // Atomically claim the toast: try to delete the file. If deletion
        // fails (e.g. another process recreated it just now), re-loop.
        std::wstring path = copilot_pending_path_by_hash(hash);
        std::error_code dec;
        if (!fs::remove(path, dec)) {
            Sleep(50);
            continue;
        }
        break;
    }

    // Build title and message just like sessionEnd does, but with an "idle"
    // suffix so users can tell the difference. Then re-invoke toasty as the
    // toast emitter (keeps WinRT/icon/ntfy logic in one place).
    std::wstring folder = folder_name_of(toFire.cwd);
    // Strip embedded double quotes from inputs to keep the re-exec command
    // line robust (CreateProcessW parses its lpCommandLine; embedded quotes
    // break our naive `"..."` quoting).
    auto stripQuotes = [](std::wstring s) {
        for (auto& c : s) if (c == L'"') c = L'\'';
        return s;
    };
    std::wstring sessionName = stripQuotes(normalize_ws(toFire.sessionName));
    std::wstring shortPrompt = truncate_w(stripQuotes(normalize_ws(toFire.prompt)), 80);
    std::wstring title = L"GitHub Copilot - ready";
    if (!sessionName.empty()) title = L"GitHub Copilot - " + sessionName + L" (ready)";
    std::wstring message;
    if (!shortPrompt.empty()) {
        message = L"\"" + shortPrompt + L"\"";
        if (!folder.empty()) message += L" - " + folder;
    } else {
        message = folder.empty() ? L"Awaiting input" : (L"Awaiting input - " + folder);
    }
    // Final safety cap on total body length.
    message = truncate_w(message, 140);
    title   = truncate_w(title, 100);

    std::wstring exe = get_exe_path();
    if (!exe.empty()) {
        std::wstring cmd = L"\"" + exe + L"\" --app copilot --title \"" + title + L"\" \"" + message + L"\"";
        if (toFire.hwnd != 0) {
            cmd += L" --launch-hwnd " + std::to_wstring(toFire.hwnd);
        }
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');
        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        DWORD flags = CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB;
        CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                       flags, nullptr, nullptr, &si, &pi);
        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread)  CloseHandle(pi.hThread);
    }

    release();
    return 0;
}

// Convert UTF-8 to wstring (UTF-16). Returns empty on failure.
std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Collapse CR/LF/tab to spaces, collapse runs of spaces, trim.
std::wstring normalize_ws(const std::wstring& in) {
    std::wstring tmp;
    tmp.reserve(in.size());
    for (wchar_t c : in) {
        if (c == L'\r' || c == L'\n' || c == L'\t') tmp += L' ';
        else tmp += c;
    }
    std::wstring out;
    out.reserve(tmp.size());
    bool prevSpace = false;
    for (wchar_t c : tmp) {
        if (c == L' ') {
            if (!prevSpace) out += c;
            prevSpace = true;
        } else {
            out += c;
            prevSpace = false;
        }
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    size_t start = 0;
    while (start < out.size() && out[start] == L' ') ++start;
    return out.substr(start);
}

// Truncate by wide-char count, append ellipsis if truncated.
std::wstring truncate_w(const std::wstring& s, size_t maxChars) {
    if (s.size() <= maxChars) return s;
    return s.substr(0, maxChars) + L"...";
}

// Persist a Copilot prompt for later sessionEnd lookup. Best-effort.
void cache_copilot_prompt(const std::wstring& cwd, uint64_t timestampMs,
                          const std::wstring& prompt) {
    if (cwd.empty() || prompt.empty()) return;
    std::wstring dir = copilot_cache_dir();
    if (dir.empty()) return;

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;

    std::wstring hash = fnv1a_hex(cwd);

    // Sweep stale files (older than 24h) for any cwd. Best-effort, ignore errors.
    auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24);
    {
        std::error_code iec;
        for (auto it = fs::directory_iterator(dir, iec);
             !iec && it != fs::directory_iterator(); it.increment(iec)) {
            std::error_code fec;
            if (!it->is_regular_file(fec)) continue;
            auto ftime = fs::last_write_time(it->path(), fec);
            if (fec) continue;
            // Convert file_time_type to system_clock::time_point
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - decltype(ftime)::clock::now() + std::chrono::system_clock::now());
            if (sctp < cutoff) {
                std::error_code rec;
                fs::remove(it->path(), rec);
            }
        }
    }

    wchar_t name[64];
    swprintf(name, 64, L"%s-%020llu.txt", hash.c_str(),
             (unsigned long long)timestampMs);
    std::wstring filePath = dir + L"\\" + name;

    std::wstring norm = truncate_w(normalize_ws(prompt), 512);

    int n = WideCharToMultiByte(CP_UTF8, 0, norm.c_str(), (int)norm.size(),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return;
    std::string utf8((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, norm.c_str(), (int)norm.size(),
                        &utf8[0], n, nullptr, nullptr);
    write_file(filePath, utf8);
}

// Look up the most recent cached prompt for a given cwd, with prompt
// timestamp <= sessionEnd timestamp. Removes the matched file (and any
// older same-cwd files) after read. Returns empty wstring if none found.
std::wstring consume_copilot_prompt(const std::wstring& cwd, uint64_t endTs) {
    if (cwd.empty()) return L"";
    std::wstring dir = copilot_cache_dir();
    if (dir.empty()) return L"";
    std::wstring hash = fnv1a_hex(cwd);
    std::wstring prefix = hash + L"-";

    fs::path bestPath;
    uint64_t bestTs = 0;
    bool found = false;

    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        std::wstring name = it->path().filename().wstring();
        if (name.size() < prefix.size() + 4) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        size_t end = name.rfind(L".txt");
        if (end == std::wstring::npos || end <= prefix.size()) continue;
        std::wstring tsStr = name.substr(prefix.size(), end - prefix.size());
        uint64_t ts = 0;
        try { ts = std::stoull(tsStr); }
        catch (...) { continue; }
        if (endTs > 0 && ts > endTs) continue;
        if (!found || ts >= bestTs) {
            bestTs = ts;
            bestPath = it->path();
            found = true;
        }
    }

    std::wstring result;
    if (found) {
        std::string content = read_file(bestPath.wstring());
        result = utf8_to_wide(content);
        std::error_code rec;
        for (auto it = fs::directory_iterator(dir, rec);
             !rec && it != fs::directory_iterator(); it.increment(rec)) {
            std::error_code fec;
            if (!it->is_regular_file(fec)) continue;
            std::wstring name = it->path().filename().wstring();
            if (name.compare(0, prefix.size(), prefix) != 0) continue;
            size_t end = name.rfind(L".txt");
            if (end == std::wstring::npos || end <= prefix.size()) continue;
            std::wstring tsStr = name.substr(prefix.size(), end - prefix.size());
            uint64_t ts = 0;
            try { ts = std::stoull(tsStr); } catch (...) { continue; }
            if (ts <= bestTs) {
                std::error_code re;
                fs::remove(it->path(), re);
            }
        }
    }
    return result;
}

// Read-only variant of consume_copilot_prompt: returns the latest cached
// prompt for cwd whose timestamp <= refTs (or any if refTs == 0). Does NOT
// delete files. Used by the postToolUse path so it doesn't race with
// sessionEnd consuming the same file.
std::wstring peek_copilot_prompt(const std::wstring& cwd, uint64_t refTs) {
    if (cwd.empty()) return L"";
    std::wstring dir = copilot_cache_dir();
    if (dir.empty()) return L"";
    std::wstring hash = fnv1a_hex(cwd);
    std::wstring prefix = hash + L"-";

    fs::path bestPath;
    uint64_t bestTs = 0;
    bool found = false;

    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        std::wstring name = it->path().filename().wstring();
        if (name.size() < prefix.size() + 4) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        size_t end = name.rfind(L".txt");
        if (end == std::wstring::npos || end <= prefix.size()) continue;
        std::wstring tsStr = name.substr(prefix.size(), end - prefix.size());
        uint64_t ts = 0;
        try { ts = std::stoull(tsStr); }
        catch (...) { continue; }
        if (refTs > 0 && ts > refTs) continue;
        if (!found || ts >= bestTs) {
            bestTs = ts;
            bestPath = it->path();
            found = true;
        }
    }
    if (!found) return L"";
    return utf8_to_wide(read_file(bestPath.wstring()));
}

// =============================================================
// Copilot session-name lookup (via sessionId in hook payload)
//
// Newer Copilot CLI builds (verified against on-disk events.jsonl logs)
// include a "sessionId" UUID in the hook input JSON. Each session writes
// a workspace.yaml at %USERPROFILE%\.copilot\session-state\<id>\
// containing fields like name, summary, repository, branch. We use those
// to enrich the toast.
// =============================================================

std::wstring copilot_session_state_dir() {
    std::wstring home = expand_env(L"%USERPROFILE%");
    if (home.empty()) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &p))) {
            home = p; CoTaskMemFree(p);
        }
    }
    if (home.empty()) return L"";
    return home + L"\\.copilot\\session-state";
}

// Trim leading/trailing ASCII whitespace (incl. CR).
std::wstring trim_ws_w(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r')) --b;
    return s.substr(a, b - a);
}

// Extract a top-level scalar from a tiny one-level YAML file. Handles
// leading whitespace, optional double/single quoted values, trailing
// comments (when value is unquoted), and CRLF line endings.
std::wstring read_yaml_scalar(const std::wstring& filePath, const std::wstring& key) {
    std::string raw = read_file(filePath);
    if (raw.empty()) return L"";
    std::wstring text = utf8_to_wide(raw);

    std::wstring needle = key + L":";
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t lineEnd = text.find(L'\n', pos);
        std::wstring line = text.substr(
            pos, lineEnd == std::wstring::npos ? std::wstring::npos : lineEnd - pos);
        if (lineEnd == std::wstring::npos) pos = text.size() + 1;
        else pos = lineEnd + 1;

        std::wstring t = trim_ws_w(line);
        if (t.empty() || t[0] == L'#') continue;
        if (t.size() < needle.size()) continue;
        if (t.compare(0, needle.size(), needle) != 0) continue;
        std::wstring val = trim_ws_w(t.substr(needle.size()));
        if (val.empty()) return L"";
        if (val[0] != L'"' && val[0] != L'\'') {
            size_t hash = val.find(L" #");
            if (hash != std::wstring::npos) val = trim_ws_w(val.substr(0, hash));
        }
        if (val.size() >= 2 &&
            ((val.front() == L'"' && val.back() == L'"') ||
             (val.front() == L'\'' && val.back() == L'\''))) {
            val = val.substr(1, val.size() - 2);
        }
        return val;
    }
    return L"";
}

// Validate a UUID-shaped string (8-4-4-4-12 hex + 4 dashes). Defends
// against path traversal via a malicious sessionId.
bool is_valid_session_id(const std::wstring& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        wchar_t c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != L'-') return false;
        } else {
            if (!((c >= L'0' && c <= L'9') ||
                  (c >= L'a' && c <= L'f') ||
                  (c >= L'A' && c <= L'F'))) return false;
        }
    }
    return true;
}

// Look up the human-readable session name. Prefers workspace.yaml `name`,
// falls back to `summary`, then `repository`. Returns empty on failure.
std::wstring get_copilot_session_name(const std::wstring& sessionId) {
    if (!is_valid_session_id(sessionId)) return L"";
    std::wstring base = copilot_session_state_dir();
    if (base.empty()) return L"";
    std::wstring yaml = base + L"\\" + sessionId + L"\\workspace.yaml";
    if (!path_exists(yaml)) return L"";

    std::wstring name = read_yaml_scalar(yaml, L"name");
    if (!name.empty()) return name;
    name = read_yaml_scalar(yaml, L"summary");
    if (!name.empty()) return name;
    return read_yaml_scalar(yaml, L"repository");
}

std::wstring humanize_reason(const std::wstring& reason) {
    if (reason == L"complete") return L"";
    if (reason == L"error") return L"failed";
    if (reason == L"timeout") return L"timed out";
    if (reason == L"abort") return L"aborted";
    if (reason == L"user_exit") return L"session ended";
    return reason;
}

std::wstring folder_name_of(const std::wstring& cwd) {
    if (cwd.empty()) return L"";
    fs::path p(cwd);
    std::wstring name = p.filename().wstring();
    if (name.empty()) name = p.root_name().wstring();
    return name;
}

struct CopilotEndContent {
    std::wstring title;
    std::wstring message;
};

CopilotEndContent build_copilot_end_content(const std::wstring& cwd,
                                            const std::wstring& reason,
                                            const std::wstring& prompt,
                                            const std::wstring& sessionName) {
    CopilotEndContent c;
    std::wstring folder = folder_name_of(cwd);
    std::wstring human = humanize_reason(reason);
    bool ok = reason.empty() || reason == L"complete";

    std::wstring titleSuffix;
    if (ok) {
        // No suffix.
    } else if (reason == L"user_exit") {
        titleSuffix = L"session ended";
    } else {
        titleSuffix = human;
    }

    if (sessionName.empty()) {
        c.title = L"GitHub Copilot";
        if (!titleSuffix.empty()) c.title += L" - " + titleSuffix;
    } else {
        c.title = L"GitHub Copilot - " + sessionName;
        if (!titleSuffix.empty()) c.title += L" (" + titleSuffix + L")";
    }

    std::wstring shortPrompt = truncate_w(normalize_ws(prompt), 80);
    if (!shortPrompt.empty()) {
        c.message = L"\"" + shortPrompt + L"\"";
        if (!folder.empty()) c.message += L" - " + folder;
    } else {
        std::wstring lead = ok ? L"Finished" : (human.empty() ? L"Ended" : human);
        if (!lead.empty() && lead[0] >= L'a' && lead[0] <= L'z')
            lead[0] = (wchar_t)(lead[0] - 32);
        c.message = lead;
        if (!folder.empty()) c.message += L" - " + folder;
    }
    // Cap total lengths defensively (sessionName/prompt are unbounded).
    c.title   = truncate_w(c.title, 100);
    c.message = truncate_w(c.message, 140);
    return c;
}

struct CopilotHookResult {
    bool shouldToast = false;
    std::wstring title;
    std::wstring message;
};

// Process a Copilot hook event. event must be "prompt" or "end".
CopilotHookResult handle_copilot_hook(const std::wstring& event) {
    CopilotHookResult r;
    std::string raw = read_stdin_utf8();

    JsonObject obj;
    bool parsed = false;
    if (!raw.empty()) {
        try {
            obj = JsonObject::Parse(to_hstring(raw));
            parsed = true;
        } catch (const hresult_error&) {
            parsed = false;
        }
    }

    auto getString = [&](const wchar_t* key) -> std::wstring {
        if (!parsed || !obj.HasKey(key)) return L"";
        try {
            auto v = obj.GetNamedValue(key);
            if (v.ValueType() == JsonValueType::String)
                return std::wstring(v.GetString());
        } catch (...) {}
        return L"";
    };
    auto getNumber = [&](const wchar_t* key) -> uint64_t {
        if (!parsed || !obj.HasKey(key)) return 0;
        try {
            auto v = obj.GetNamedValue(key);
            if (v.ValueType() == JsonValueType::Number)
                return (uint64_t)v.GetNumber();
        } catch (...) {}
        return 0;
    };

    if (event == L"prompt") {
        std::wstring cwd = getString(L"cwd");
        std::wstring prompt = getString(L"prompt");
        uint64_t ts = getNumber(L"timestamp");
        if (ts == 0) {
            ts = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }
        cache_copilot_prompt(cwd, ts, prompt);
        // User just submitted -> they're in the terminal. Cancel any pending
        // idle toast for this cwd so the watchdog doesn't fire after they
        // re-engage.
        if (!cwd.empty()) cancel_pending(cwd);
        return r; // shouldToast = false
    }

    if (event == L"tool") {
        // postToolUse: a tool just finished. Refresh the pending file's
        // fireMs to (now + idleSec*1000), snapshotting the latest cached
        // prompt for the toast body. Then spawn a detached watchdog (which
        // dedupes via a single-instance lock).
        std::wstring cwd = getString(L"cwd");
        if (cwd.empty()) return r;
        // Read-only peek so we don't race with sessionEnd consuming the
        // prompt cache file.
        std::wstring prompt = peek_copilot_prompt(cwd, now_ms());
        std::wstring sessionName = get_copilot_session_name(getString(L"sessionId"));
        // Capture the terminal window HWND NOW, while Copilot is still our
        // ancestor. By the time the detached watchdog fires the toast we'd
        // no longer be reachable from the terminal via parent walk.
        HWND termWnd = find_ancestor_window();
        unsigned idleSec = copilot_idle_seconds();
        uint64_t fireMs = now_ms() + (uint64_t)idleSec * 1000ULL;
        write_pending(cwd, fireMs, prompt, sessionName, (uint64_t)(ULONG_PTR)termWnd);
        spawn_watchdog(fnv1a_hex(cwd));
        return r; // shouldToast = false
    }

    // event == "end" (or unknown — treat as end)
    std::wstring cwd = getString(L"cwd");
    std::wstring reason = getString(L"reason");
    uint64_t endTs = getNumber(L"timestamp");
    std::wstring prompt = consume_copilot_prompt(cwd, endTs);
    std::wstring sessionName = get_copilot_session_name(getString(L"sessionId"));
    if (!cwd.empty()) cancel_pending(cwd);
    auto content = build_copilot_end_content(cwd, reason, prompt, sessionName);
    r.shouldToast = true;
    r.title = content.title;
    r.message = content.message;
    return r;
}

// Install hook for OpenAI Codex CLI
// Codex uses TOML config at ~/.codex/config.toml with notify = "command"
bool install_codex(const std::wstring& exePath) {
    std::wstring configDir = expand_env(L"%USERPROFILE%\\.codex");
    std::wstring configPath = configDir + L"\\config.toml";

    // Create .codex directory if needed
    fs::create_directories(configDir);

    std::string content = read_file(configPath);

    // Escape backslashes for TOML string
    std::string exePathUtf8;
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, exePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            exePathUtf8.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, exePath.c_str(), -1, &exePathUtf8[0], size, nullptr, nullptr);
        }
    }
    // Double backslashes for TOML
    std::string escapedPath;
    for (char c : exePathUtf8) {
        if (c == '\\') escapedPath += "\\\\";
        else escapedPath += c;
    }

    std::string notifyLine = "notify = [\"" + escapedPath + "\", \"Codex finished\", \"-t\", \"Codex\"]\n";

    if (content.empty()) {
        // New file
        return write_file(configPath, notifyLine);
    }

    // Check if notify is already set
    if (content.find("toasty") != std::string::npos) {
        return true; // Already installed
    }

    backup_file(configPath);

    // Replace existing notify line or append
    size_t notifyPos = content.find("notify");
    if (notifyPos != std::string::npos) {
        // Find end of line
        size_t lineEnd = content.find('\n', notifyPos);
        if (lineEnd == std::string::npos) lineEnd = content.size();
        else lineEnd++; // include the newline
        content.replace(notifyPos, lineEnd - notifyPos, notifyLine);
    } else {
        // Append
        if (!content.empty() && content.back() != '\n') content += '\n';
        content += notifyLine;
    }

    return write_file(configPath, content);
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
bool is_copilot_installed_at(const std::wstring& configPath) {
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
        return false;
    }

    return false;
}

bool is_copilot_installed() {
    return is_copilot_installed_at(copilot_repo_config_path());
}

bool is_copilot_installed_global() {
    std::wstring p = copilot_global_config_path();
    if (p.empty()) return false;
    return is_copilot_installed_at(p);
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

// Uninstall hook for GitHub Copilot. Removes both the per-repo config and
// the user-global config (best-effort).
bool uninstall_copilot() {
    bool ok = true;
    auto removeOne = [&](const std::wstring& configPath) {
        if (configPath.empty()) return;
        try {
            if (path_exists(configPath)) {
                backup_file(configPath);
                fs::remove(configPath);
            }
        } catch (const std::exception& e) {
            ok = false;
            int size = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, nullptr, 0);
            if (size > 0) {
                std::wstring wideMsg(size - 1, 0);
                MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, &wideMsg[0], size);
                std::wcerr << L"Error uninstalling Copilot hook: " << wideMsg << L"\n";
            } else {
                std::wcerr << L"Error uninstalling Copilot hook\n";
            }
        }
    };
    removeOne(copilot_repo_config_path());
    removeOne(copilot_global_config_path());
    return ok;
}

// Uninstall hook for OpenAI Codex
bool uninstall_codex() {
    std::wstring configPath = expand_env(L"%USERPROFILE%\\.codex\\config.toml");
    std::string content = read_file(configPath);

    if (content.empty()) return true;

    if (content.find("toasty") == std::string::npos) return true;

    backup_file(configPath);

    // Remove the notify line containing toasty
    std::string result;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find("notify") != std::string::npos && line.find("toasty") != std::string::npos) {
            continue; // Skip toasty notify line
        }
        result += line + '\n';
    }

    return write_file(configPath, result);
}

// Check if Codex hook is installed
bool is_codex_installed() {
    std::wstring configPath = expand_env(L"%USERPROFILE%\\.codex\\config.toml");
    std::string content = read_file(configPath);
    return content.find("toasty") != std::string::npos;
}

// Show installation status
void show_status() {
    std::wcout << L"Installation status:\n\n";
    
    bool claudeDetected = detect_claude();
    bool geminiDetected = detect_gemini();
    bool copilotDetected = detect_copilot();
    bool codexDetected = detect_codex();
    
    std::wcout << L"Detected agents:\n";
    std::wcout << L"  " << (claudeDetected ? L"[x]" : L"[ ]") << L" Claude Code\n";
    std::wcout << L"  " << (geminiDetected ? L"[x]" : L"[ ]") << L" Gemini CLI\n";
    std::wcout << L"  " << (copilotDetected ? L"[x]" : L"[ ]") << L" GitHub Copilot (in current repo)\n";
    std::wcout << L"  " << (codexDetected ? L"[x]" : L"[ ]") << L" OpenAI Codex\n";
    std::wcout << L"\n";
    
    std::wcout << L"Installed hooks:\n";
    std::wcout << L"  " << (is_claude_installed() ? L"[x]" : L"[ ]") << L" Claude Code\n";
    std::wcout << L"  " << (is_gemini_installed() ? L"[x]" : L"[ ]") << L" Gemini CLI\n";
    std::wcout << L"  " << (is_copilot_installed() ? L"[x]" : L"[ ]") << L" GitHub Copilot (repo: .github\\hooks\\toasty.json)\n";
    std::wcout << L"  " << (is_copilot_installed_global() ? L"[x]" : L"[ ]") << L" GitHub Copilot (global: ~\\.copilot\\hooks\\toasty.json)\n";
    std::wcout << L"  " << (is_codex_installed() ? L"[x]" : L"[ ]") << L" OpenAI Codex\n";
}

// Handle --install command
void handle_install(const std::wstring& agent, bool global, bool noSessionEnd) {
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
    bool installCodex = installAll || agent == L"codex";

    if (g_dryRun) {
        std::wcout << L"[dry-run] Install targets:";
        if (installClaude) std::wcout << L" claude";
        if (installGemini) std::wcout << L" gemini";
        if (installCopilot) std::wcout << L" copilot";
        if (installCodex) std::wcout << L" codex";
        if (global) std::wcout << L" (global)";
        std::wcout << L"\n";

        if (installClaude) {
            std::wstring configPath = expand_env(L"%USERPROFILE%\\.claude\\settings.json");
            std::wcout << L"[dry-run] Would write: " << configPath << L"\n";
            std::wstring escapedPath = escape_json_string(exePath);
            std::wcout << L"[dry-run] Hook command: " << escapedPath << L" \"Task complete\" -t \"Claude Code\"\n";
            std::wcout << L"[dry-run] Hook type: Stop\n";
        }
        if (installGemini) {
            std::wstring configPath = expand_env(L"%USERPROFILE%\\.gemini\\settings.json");
            std::wcout << L"[dry-run] Would write: " << configPath << L"\n";
            std::wstring escapedPath = escape_json_string(exePath);
            std::wcout << L"[dry-run] Hook command: " << escapedPath << L" \"Gemini finished\" -t \"Gemini\"\n";
            std::wcout << L"[dry-run] Hook type: AfterAgent\n";
        }
        if (installCopilot) {
            std::wstring cpath = global ? copilot_global_config_path() : copilot_repo_config_path();
            std::wcout << L"[dry-run] Would write: " << (cpath.empty() ? L"<unresolved>" : cpath) << L"\n";
            if (!noSessionEnd) {
                std::wcout << L"[dry-run] Hook command (sessionEnd): toasty --copilot-hook end\n";
            }
            std::wcout << L"[dry-run] Hook command (userPromptSubmitted): toasty --copilot-hook prompt\n";
            std::wcout << L"[dry-run] Hook command (postToolUse): toasty --copilot-hook tool (idle watchdog)\n";
            if (noSessionEnd) {
                std::wcout << L"[dry-run] Hook type: userPromptSubmitted, postToolUse (sessionEnd skipped via --no-session-end)\n";
            } else {
                std::wcout << L"[dry-run] Hook type: sessionEnd, userPromptSubmitted, postToolUse\n";
            }
            std::wcout << L"[dry-run] Hook scope: " << (global ? L"global (user-level)" : L"repo (per-project)") << L"\n";
            std::wcout << L"[dry-run] Idle threshold: " << copilot_idle_seconds() << L"s (TOASTY_COPILOT_IDLE_SEC)\n";
        }
        if (installCodex) {
            std::wstring configPath = expand_env(L"%USERPROFILE%\\.codex\\config.toml");
            std::wcout << L"[dry-run] Would write: " << configPath << L"\n";
            std::wcout << L"[dry-run] Hook type: notify\n";
        }
        return;
    }

    std::wcout << L"Detecting AI CLI agents...\n";

    bool claudeDetected = detect_claude();
    bool geminiDetected = detect_gemini();
    bool copilotDetected = detect_copilot();
    bool codexDetected = detect_codex();

    std::wcout << L"  " << (claudeDetected ? L"[x]" : L"[ ]") << L" Claude Code found\n";
    std::wcout << L"  " << (geminiDetected ? L"[x]" : L"[ ]") << L" Gemini CLI found\n";
    std::wcout << L"  " << (copilotDetected ? L"[x]" : L"[ ]") << L" GitHub Copilot (in current repo)\n";
    std::wcout << L"  " << (codexDetected ? L"[x]" : L"[ ]") << L" OpenAI Codex found\n";
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
    
    if (installCopilot && (copilotDetected || explicitAgent || global)) {
        bool ok = global ? install_copilot_global(exePath, noSessionEnd)
                         : install_copilot(exePath, noSessionEnd);
        const wchar_t* hookList = noSessionEnd
            ? L"userPromptSubmitted + postToolUse hooks (sessionEnd skipped)"
            : L"sessionEnd + userPromptSubmitted + postToolUse hooks";
        if (ok) {
            if (global) {
                std::wcout << L"  [x] GitHub Copilot: Added " << hookList << L" (global)\n";
                std::wcout << L"      Config: " << copilot_global_config_path() << L"\n";
                std::wcout << L"      Idle watchdog: " << copilot_idle_seconds() << L"s after last tool (override with TOASTY_COPILOT_IDLE_SEC)\n";
            } else {
                std::wcout << L"  [x] GitHub Copilot: Added " << hookList << L"\n";
                std::wcout << L"      Idle watchdog: " << copilot_idle_seconds() << L"s after last tool (override with TOASTY_COPILOT_IDLE_SEC)\n";
                std::wcout << L"      Note: This is repo-level only. Use --install copilot --global for user-wide.\n";
            }
            anyInstalled = true;
        } else {
            std::wcout << L"  [ ] GitHub Copilot: Failed to install\n";
        }
    }
    
    if (installCodex && (codexDetected || explicitAgent)) {
        if (install_codex(exePath)) {
            std::wcout << L"  [x] OpenAI Codex: Added notify hook\n";
            anyInstalled = true;
        } else {
            std::wcout << L"  [ ] OpenAI Codex: Failed to install\n";
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
    if (g_dryRun) {
        std::wcout << L"[dry-run] Would check and remove hooks from:\n";
        std::wstring claudePath = expand_env(L"%USERPROFILE%\\.claude\\settings.json");
        std::wstring geminiPath = expand_env(L"%USERPROFILE%\\.gemini\\settings.json");
        std::wstring codexPath = expand_env(L"%USERPROFILE%\\.codex\\config.toml");
        std::wcout << L"[dry-run]   Claude: " << claudePath << L"\n";
        std::wcout << L"[dry-run]   Gemini: " << geminiPath << L"\n";
        std::wcout << L"[dry-run]   Copilot: .github\\hooks\\toasty.json\n";
        std::wcout << L"[dry-run]   Codex: " << codexPath << L"\n";
        return;
    }

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
    
    if (is_copilot_installed() || is_copilot_installed_global()) {
        if (uninstall_copilot()) {
            std::wcout << L"  [x] GitHub Copilot: Removed hooks (repo + global)\n";
            anyUninstalled = true;
        } else {
            std::wcout << L"  [ ] GitHub Copilot: Failed to remove\n";
        }
    }
    
    if (is_codex_installed()) {
        if (uninstall_codex()) {
            std::wcout << L"  [x] OpenAI Codex: Removed notify hook\n";
            anyUninstalled = true;
        } else {
            std::wcout << L"  [ ] OpenAI Codex: Failed to remove\n";
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
    bool installGlobal = false;
    bool noSessionEnd = false;
    bool explicitApp = false;  // Track if user explicitly set --app
    bool explicitTitle = false; // Track if user explicitly set -t
    bool debug = false;
    std::wstring copilotHookEvent; // Set when --copilot-hook <event> is used
    std::wstring copilotWatchdogHash; // Set when --copilot-watchdog <hash> is used
    uint64_t launchHwnd = 0;       // Set when --launch-hwnd <decimal> is used
    std::wstring focusUrl;         // Set when --focus <url> is used

    // Quick scan for --debug and --dry-run flags
    for (int i = 1; i < argc; i++) {
        std::wstring flag(argv[i]);
        if (flag == L"--debug") {
            debug = true;
        } else if (flag == L"--dry-run") {
            g_dryRun = true;
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
        else if (arg == L"-v" || arg == L"--version") {
            std::wcout << L"toasty v" << TOASTY_VERSION << L"\n";
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
            // Optional next arg is the activation URL passed by Windows when
            // the toast is clicked (e.g. "toasty://focus?hwnd=12345").
            if (i + 1 < argc) {
                std::wstring next = argv[i + 1];
                if (!next.empty() && next[0] != L'-') {
                    focusUrl = next;
                    ++i;
                }
            }
        }
        else if (arg == L"--launch-hwnd") {
            if (i + 1 < argc) {
                try { launchHwnd = std::stoull(argv[++i]); }
                catch (...) { launchHwnd = 0; }
            } else {
                std::wcerr << L"Error: --launch-hwnd requires a decimal HWND\n";
                return 1;
            }
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
        else if (arg == L"--global") {
            installGlobal = true;
        }
        else if (arg == L"--no-session-end") {
            noSessionEnd = true;
        }
        else if (arg == L"--copilot-hook") {
            if (i + 1 < argc) {
                copilotHookEvent = argv[++i];
            } else {
                std::wcerr << L"Error: --copilot-hook requires an event name (prompt|end|tool)\n";
                return 1;
            }
        }
        else if (arg == L"--copilot-watchdog") {
            if (i + 1 < argc) {
                copilotWatchdogHash = argv[++i];
            } else {
                std::wcerr << L"Error: --copilot-watchdog requires a hash argument\n";
                return 1;
            }
        }
        else if (arg == L"--debug") {
            // Already handled in pre-scan
        }
        else if (arg == L"--dry-run") {
            // Already handled in pre-scan
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
        handle_install(installAgent, installGlobal, noSessionEnd);
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

        // Protocol handlers have focus restrictions - use aggressive approach.
        // Prefer an HWND embedded in the activation URL (per-toast targeting,
        // works correctly for multiple concurrent Copilot sessions). Fall back
        // to the saved registry handle for older toasts and other apps.
        HWND targetWnd = nullptr;
        if (!focusUrl.empty()) {
            size_t qpos = focusUrl.find(L"hwnd=");
            if (qpos != std::wstring::npos) {
                size_t start = qpos + 5;
                size_t end = focusUrl.find_first_of(L"&#", start);
                std::wstring v = focusUrl.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
                try {
                    uint64_t h = std::stoull(v);
                    HWND cand = (HWND)(ULONG_PTR)h;
                    if (h != 0 && IsWindow(cand)) targetWnd = cand;
                } catch (...) {}
            }
        }
        if (!targetWnd) {
            targetWnd = get_saved_console_window_handle();
        }
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

    // Watchdog mode: detached process that sleeps until the pending file's
    // fireMs elapses, then re-invokes toasty to fire the toast. No WinRT.
    if (!copilotWatchdogHash.empty()) {
        return run_copilot_watchdog(copilotWatchdogHash);
    }

    // Handle Copilot hook events: read JSON payload from stdin, build a rich
    // toast (or silently cache the prompt for later).
    if (!copilotHookEvent.empty()) {
        init_apartment();
        auto res = handle_copilot_hook(copilotHookEvent);
        if (!res.shouldToast) {
            return 0;
        }
        message = res.message;
        title = res.title;
        explicitTitle = true;
        // Use the copilot icon for the toast.
        const AppPreset* p = find_preset(L"copilot");
        if (p) {
            std::wstring ipath = extract_icon_to_temp(p->iconResourceId);
            if (!ipath.empty()) iconPath = ipath;
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
        HWND terminalWnd = nullptr;
        if (launchHwnd != 0) {
            // Caller (e.g. detached watchdog) explicitly passed an HWND
            // captured at hook time. Trust it if it's still a valid window.
            HWND cand = (HWND)(ULONG_PTR)launchHwnd;
            if (IsWindow(cand)) terminalWnd = cand;
        }
        if (!terminalWnd) {
            terminalWnd = find_ancestor_window();
        }

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

        // Build toast XML with protocol activation for click-to-focus.
        // Embed HWND directly in the launch URL so concurrent toasts each
        // target their own window (the registry-saved handle is shared).
        std::wstring launchUrl = L"toasty://focus";
        if (terminalWnd) {
            launchUrl += L"?hwnd=" + std::to_wstring((uint64_t)(ULONG_PTR)terminalWnd);
        }
        std::wstring xml = L"<toast activationType=\"protocol\" launch=\"" +
                           escape_xml(launchUrl) + L"\"><visual><binding template=\"ToastGeneric\">";
        
        // Add icon if provided
        if (!iconPath.empty()) {
            xml += L"<image placement=\"appLogoOverride\" src=\"" + escape_xml(iconPath) + L"\"/>";
        }
        
        xml += L"<text>" + escape_xml(title) + L"</text>"
               L"<text>" + escape_xml(message) + L"</text>"
               L"</binding></visual></toast>";

        if (g_dryRun) {
            std::wcout << L"[dry-run] Title: " << title << L"\n";
            std::wcout << L"[dry-run] Message: " << message << L"\n";
            std::wcout << L"[dry-run] Icon: " << (iconPath.empty() ? L"(none)" : iconPath) << L"\n";
            std::wcout << L"[dry-run] Toast XML:\n" << xml << L"\n";

            // Show ntfy status
            wchar_t topicBuf[256] = {};
            if (GetEnvironmentVariableW(L"TOASTY_NTFY_TOPIC", topicBuf, 256) && topicBuf[0] != L'\0') {
                wchar_t serverBuf[256] = {};
                std::wstring server = L"ntfy.sh";
                if (GetEnvironmentVariableW(L"TOASTY_NTFY_SERVER", serverBuf, 256) && serverBuf[0] != L'\0') {
                    server = serverBuf;
                }
                std::wcout << L"[dry-run] ntfy: would POST to https://" << server << L"/" << topicBuf << L"\n";
            } else {
                std::wcout << L"[dry-run] ntfy: not configured\n";
            }

            std::wcout << L"[dry-run] Update check: skipped\n";
            return 0;
        }

        XmlDocument doc;
        doc.LoadXml(xml);

        ToastNotification toast(doc);

        auto notifier = ToastNotificationManager::CreateToastNotifier(APP_ID);
        notifier.Show(toast);

        // Send push notification via ntfy if configured (fire-and-forget)
        send_ntfy_notification(title, message);

        // Check for updates (throttled to once per day, non-blocking)
        check_for_updates();

        return 0;
    }
    catch (const hresult_error& ex) {
        std::wcerr << L"Error: " << ex.message().c_str() << L"\n";
        return 1;
    }
}
