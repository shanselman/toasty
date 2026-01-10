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

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::UI::Notifications;

const wchar_t* APP_ID = L"Toasty.CLI.Notification";
const wchar_t* APP_NAME = L"Toasty";
const wchar_t* PROTOCOL_NAME = L"toasty";

void print_usage() {
    std::wcout << L"toasty - Windows toast notification CLI\n\n"
               << L"Usage: toasty <message> [options]\n\n"
               << L"Options:\n"
               << L"  -t, --title <text>   Set notification title (default: \"Notification\")\n"
               << L"  -h, --help           Show this help\n"
               << L"  --register           Register app for notifications (run once)\n\n"
               << L"Examples:\n"
               << L"  toasty --register\n"
               << L"  toasty \"Build completed\"\n"
               << L"  toasty \"Task done\" -t \"Claude Code\"\n";
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

// Register protocol handler for toast activation
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
    // Validate that the window still exists
    if (IsWindow(hwnd)) {
        return hwnd;
    }
    
    return nullptr;
}

// Find and focus the console window
bool focus_console_window() {
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != nullptr) {
        // Try to restore if minimized
        if (IsIconic(consoleWindow)) {
            ShowWindow(consoleWindow, SW_RESTORE);
        }
        
        // Bring window to foreground
        SetForegroundWindow(consoleWindow);
        SetFocus(consoleWindow);
        return true;
    }
    
    // Try to get the saved console window handle
    HWND savedWindow = get_saved_console_window_handle();
    if (savedWindow != nullptr) {
        if (IsIconic(savedWindow)) {
            ShowWindow(savedWindow, SW_RESTORE);
        }
        SetForegroundWindow(savedWindow);
        SetFocus(savedWindow);
        return true;
    }
    
    // Last resort: try to find a console or terminal window
    // This is used when toasty is launched via protocol (no console attached)
    HWND foundWindow = nullptr;
    
    // Enumerate all top-level windows to find console windows or Windows Terminal
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        wchar_t className[256];
        GetClassNameW(hwnd, className, 256);
        
        // Look for console windows or Windows Terminal
        // Only consider visible windows
        if (!IsWindowVisible(hwnd)) {
            return TRUE;
        }
        
        if (wcscmp(className, L"ConsoleWindowClass") == 0 || 
            wcscmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0) {
            HWND* pResult = (HWND*)lParam;
            *pResult = hwnd;
            return FALSE; // Stop enumeration - found a console/terminal
        }
        return TRUE; // Continue enumeration
    }, (LPARAM)&foundWindow);
    
    if (foundWindow != nullptr) {
        if (IsIconic(foundWindow)) {
            ShowWindow(foundWindow, SW_RESTORE);
        }
        SetForegroundWindow(foundWindow);
        SetFocus(foundWindow);
        return true;
    }
    
    return false;
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
        // Also register the protocol handler
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
        // Also register the protocol handler
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
    bool doRegister = false;
    bool doFocus = false;

    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];

        if (arg == L"-h" || arg == L"--help") {
            print_usage();
            return 0;
        }
        else if (arg == L"--register") {
            doRegister = true;
        }
        else if (arg == L"--focus") {
            doFocus = true;
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

    if (doFocus) {
        // Handle protocol activation - focus the console window
        focus_console_window();
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

        // Save the current console window handle for later focus
        HWND consoleWindow = GetConsoleWindow();
        if (consoleWindow != nullptr) {
            save_console_window_handle(consoleWindow);
        }

        std::wstring xml = L"<toast activationType=\"protocol\" launch=\"toasty://focus\">"
                          L"<visual><binding template=\"ToastGeneric\">"
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
