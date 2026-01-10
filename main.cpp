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
#include <vector>
#include <filesystem>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace winrt;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::UI::Notifications;

struct ToastButton {
    std::wstring label;
    std::wstring protocol;  // Full protocol URL (https://... or file://...)
};

const wchar_t* APP_ID = L"Toasty.CLI.Notification";
const wchar_t* APP_NAME = L"Toasty";

void print_usage() {
    std::wcout << L"toasty - Windows toast notification CLI\n\n"
               << L"Usage: toasty <message> [options]\n\n"
               << L"Options:\n"
               << L"  -t, --title <text>              Set notification title (default: \"Notification\")\n"
               << L"  --button-url <label> <url>      Add button that opens URL\n"
               << L"  --button-path <label> <path>    Add button that opens folder/file\n"
               << L"  -h, --help                      Show this help\n\n"
               << L"Examples:\n"
               << L"  toasty \"Build completed\"\n"
               << L"  toasty \"Task done\" -t \"Claude Code\"\n"
               << L"  toasty \"Build complete\" --button-path \"Open Folder\" \"C:\\build\"\n"
               << L"  toasty \"Deploy done\" --button-url \"View Docs\" \"https://example.com\"\n";
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

std::wstring path_to_protocol_url(const std::wstring& path) {
    // Convert Windows path to file:/// protocol URL for explorer
    std::wstring result = L"file:///";
    for (size_t i = 0; i < path.length(); i++) {
        wchar_t c = path[i];
        if (c == L'\\') {
            result += L'/';
        } else if (c == L':' && i == 1 && path.length() > 0 && iswalpha(path[0])) {
            // Drive letter colon (e.g., C:), keep as-is
            result += c;
        } else if (c == L' ') {
            result += L"%20";
        } else if (c == L'#') {
            result += L"%23";
        } else if (c == L'?') {
            result += L"%3F";
        } else if (c == L'&') {
            result += L"%26";
        } else if (c == L'%') {
            result += L"%25";
        } else {
            result += c;
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
    bool doRegister = false;
    std::vector<ToastButton> buttons;

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
            } else {
                std::wcerr << L"Error: --title requires a text argument.\n";
                return 1;
            }
        }
        else if (arg == L"--button-url") {
            if (i + 2 < argc) {
                ToastButton btn;
                btn.label = argv[++i];
                std::wstring url = argv[++i];
                // Basic URL validation - check for http:// or https:// prefix
                if (url.find(L"http://") == 0 || url.find(L"https://") == 0) {
                    btn.protocol = url;
                    buttons.push_back(btn);
                } else {
                    std::wcerr << L"Error: URL must start with http:// or https://\n";
                    return 1;
                }
            } else {
                std::wcerr << L"Error: --button-url requires label and URL arguments.\n";
                return 1;
            }
        }
        else if (arg == L"--button-path") {
            if (i + 2 < argc) {
                ToastButton btn;
                btn.label = argv[++i];
                std::wstring path = argv[++i];
                btn.protocol = path_to_protocol_url(path);
                buttons.push_back(btn);
            } else {
                std::wcerr << L"Error: --button-path requires label and path arguments.\n";
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

        std::wstring xml = L"<toast><visual><binding template=\"ToastGeneric\">"
                          L"<text>" + escape_xml(title) + L"</text>"
                          L"<text>" + escape_xml(message) + L"</text>"
                          L"</binding></visual>";

        // Add actions if there are buttons
        if (!buttons.empty()) {
            xml += L"<actions>";
            for (const auto& btn : buttons) {
                xml += L"<action content=\"" + escape_xml(btn.label) + 
                       L"\" arguments=\"" + escape_xml(btn.protocol) + 
                       L"\" activationType=\"protocol\" />";
            }
            xml += L"</actions>";
        }

        xml += L"</toast>";

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
