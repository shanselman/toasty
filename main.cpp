#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <tlhelp32.h>
#include "resource.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::UI::Notifications;

const wchar_t* APP_ID = L"Toasty.CLI.Notification";
const wchar_t* APP_NAME = L"Toasty";

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

// Find preset by name (case-insensitive)
const AppPreset* find_preset(const std::wstring& name) {
    std::wstring lowerName = name;
    for (auto& c : lowerName) c = towlower(c);
    
    for (const auto& preset : APP_PRESETS) {
        std::wstring presetName = preset.name;
        for (auto& c : presetName) c = towlower(c);
        if (presetName == lowerName) {
            return &preset;
        }
    }
    return nullptr;
}

// Walk up process tree to find a matching AI CLI preset
const AppPreset* detect_preset_from_ancestors() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    DWORD currentPid = GetCurrentProcessId();

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
                    for (auto& c : exeName) c = towlower(c);

                    // Check if this matches a preset
                    const AppPreset* preset = find_preset(exeName);
                    if (preset) {
                        CloseHandle(snapshot);
                        return preset;
                    }
                    break;
                }
            } while (Process32NextW(snapshot, &pe32));
        }

        // Move up to parent
        currentPid = parentPid;
    }

    CloseHandle(snapshot);
    return nullptr;
}

void print_usage() {
    std::wcout << L"toasty - Windows toast notification CLI\n\n"
               << L"Usage: toasty <message> [options]\n\n"
               << L"Options:\n"
               << L"  -t, --title <text>   Set notification title (default: \"Notification\")\n"
               << L"  --app <name>         Use AI CLI preset (claude, copilot, gemini, codex, cursor)\n"
               << L"  -i, --icon <path>    Custom icon path (PNG recommended, 48x48px)\n"
               << L"  -h, --help           Show this help\n"
               << L"  --register           Register app for notifications (run once)\n\n"
               << L"Note: Toasty auto-detects known parent processes (Claude, Copilot, etc.)\n"
               << L"      and applies the appropriate preset automatically. Use --app to override.\n\n"
               << L"Examples:\n"
               << L"  toasty --register\n"
               << L"  toasty \"Build completed\"\n"
               << L"  toasty \"Task done\" -t \"Custom Title\"\n"
               << L"  toasty \"Analysis complete\" --app claude\n"
               << L"  toasty \"Build succeeded\" --app copilot\n";
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
    std::wstring iconPath;
    bool doRegister = false;
    bool explicitApp = false;  // Track if user explicitly set --app
    bool explicitTitle = false; // Track if user explicitly set -t

    // Auto-detect parent process and apply preset if found
    const AppPreset* autoPreset = detect_preset_from_ancestors();
    if (autoPreset) {
        title = autoPreset->title;
        iconPath = extract_icon_to_temp(autoPreset->iconResourceId);
    }

    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];

        if (arg == L"-h" || arg == L"--help") {
            print_usage();
            return 0;
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

        // Build toast XML with optional icon
        std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\">";
        
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
