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

void print_usage() {
    std::wcout << L"toasty - Windows toast notification CLI\n\n"
               << L"Usage: toasty <message> [options]\n\n"
               << L"Options:\n"
               << L"  -t, --title <text>        Set notification title (default: \"Notification\")\n"
               << L"  --hero-image <path>       Add a large banner image at the top\n"
               << L"  --scenario <type>         Set scenario: reminder, alarm, incomingCall, urgent\n"
               << L"  --attribution <text>      Add small gray text at bottom showing source\n"
               << L"  --progress <title:value:status>  Add progress bar (e.g., \"Building:0.75:75%\")\n"
               << L"  --audio <sound>           Set audio: default, im, mail, reminder, sms, loopingAlarm, loopingCall, silent\n"
               << L"  -h, --help                Show this help\n"
               << L"  --register                Register app for notifications (run once)\n\n"
               << L"Examples:\n"
               << L"  toasty --register\n"
               << L"  toasty \"Build completed\"\n"
               << L"  toasty \"Task done\" -t \"Claude Code\"\n"
               << L"  toasty \"Meeting in 5 min\" --scenario reminder --audio reminder\n"
               << L"  toasty \"Download complete\" --hero-image C:\\pics\\banner.jpg\n"
               << L"  toasty \"Building...\" --progress \"Build:0.5:50%\" --attribution \"via CI/CD\"\n";
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
    std::wstring heroImage;
    std::wstring scenario;
    std::wstring attribution;
    std::wstring progressData;
    std::wstring audio;
    bool doRegister = false;

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
                std::wcerr << L"Error: " << arg << L" requires a value.\n";
                return 1;
            }
        }
        else if (arg == L"--hero-image") {
            if (i + 1 < argc) {
                heroImage = argv[++i];
            } else {
                std::wcerr << L"Error: --hero-image requires a path.\n";
                return 1;
            }
        }
        else if (arg == L"--scenario") {
            if (i + 1 < argc) {
                std::wstring scenarioValue = argv[++i];
                if (scenarioValue == L"reminder" || scenarioValue == L"alarm" || 
                    scenarioValue == L"incomingCall" || scenarioValue == L"urgent") {
                    scenario = scenarioValue;
                } else {
                    std::wcerr << L"Error: Invalid scenario '" << scenarioValue 
                              << L"'. Valid options: reminder, alarm, incomingCall, urgent\n";
                    return 1;
                }
            } else {
                std::wcerr << L"Error: --scenario requires a value.\n";
                return 1;
            }
        }
        else if (arg == L"--attribution") {
            if (i + 1 < argc) {
                attribution = argv[++i];
            } else {
                std::wcerr << L"Error: --attribution requires text.\n";
                return 1;
            }
        }
        else if (arg == L"--progress") {
            if (i + 1 < argc) {
                progressData = argv[++i];
            } else {
                std::wcerr << L"Error: --progress requires data in format 'title:value:status'.\n";
                return 1;
            }
        }
        else if (arg == L"--audio") {
            if (i + 1 < argc) {
                std::wstring audioValue = argv[++i];
                if (audioValue == L"default" || audioValue == L"im" || audioValue == L"mail" || 
                    audioValue == L"reminder" || audioValue == L"sms" || audioValue == L"loopingAlarm" || 
                    audioValue == L"loopingCall" || audioValue == L"silent") {
                    audio = audioValue;
                } else {
                    std::wcerr << L"Error: Invalid audio '" << audioValue 
                              << L"'. Valid options: default, im, mail, reminder, sms, loopingAlarm, loopingCall, silent\n";
                    return 1;
                }
            } else {
                std::wcerr << L"Error: --audio requires a value.\n";
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

        // Build toast XML with optional features
        std::wstring xml = L"<toast";
        
        // Add scenario attribute if specified
        if (!scenario.empty()) {
            xml += L" scenario=\"" + escape_xml(scenario) + L"\"";
        }
        
        xml += L"><visual><binding template=\"ToastGeneric\">";
        
        // Add title and message
        xml += L"<text>" + escape_xml(title) + L"</text>";
        xml += L"<text>" + escape_xml(message) + L"</text>";
        
        // Add hero image if specified
        if (!heroImage.empty()) {
            // Convert path to file:/// URI if it's a local path
            std::wstring imageUri = heroImage;
            if (heroImage.find(L"://") == std::wstring::npos) {
                // Convert to absolute path and file URI
                wchar_t absolutePath[MAX_PATH];
                GetFullPathNameW(heroImage.c_str(), MAX_PATH, absolutePath, nullptr);
                imageUri = L"file:///" + std::wstring(absolutePath);
                // Replace backslashes with forward slashes for URI
                for (auto& ch : imageUri) {
                    if (ch == L'\\') ch = L'/';
                }
            }
            xml += L"<image placement=\"hero\" src=\"" + escape_xml(imageUri) + L"\"/>";
        }
        
        // Add attribution text if specified
        if (!attribution.empty()) {
            xml += L"<text placement=\"attribution\">" + escape_xml(attribution) + L"</text>";
        }
        
        // Add progress bar if specified
        if (!progressData.empty()) {
            // Parse progress data: "title:value:status"
            size_t firstColon = progressData.find(L':');
            size_t secondColon = progressData.rfind(L':');
            
            // Validate format: must have exactly 2 colons in the right order
            if (firstColon != std::wstring::npos && secondColon != std::wstring::npos && 
                firstColon < secondColon && firstColon != 0 && secondColon != progressData.length() - 1) {
                std::wstring progressTitle = progressData.substr(0, firstColon);
                std::wstring progressValue = progressData.substr(firstColon + 1, secondColon - firstColon - 1);
                std::wstring progressStatus = progressData.substr(secondColon + 1);
                
                // Validate that components are not empty
                if (!progressTitle.empty() && !progressValue.empty() && !progressStatus.empty()) {
                    xml += L"<progress title=\"" + escape_xml(progressTitle) + L"\" ";
                    xml += L"value=\"" + escape_xml(progressValue) + L"\" ";
                    xml += L"status=\"" + escape_xml(progressStatus) + L"\"/>";
                }
            }
        }
        
        xml += L"</binding></visual>";
        
        // Add audio if specified
        if (!audio.empty()) {
            std::wstring audioSrc;
            if (audio == L"default") {
                audioSrc = L"ms-winsoundevent:Notification.Default";
            } else if (audio == L"im") {
                audioSrc = L"ms-winsoundevent:Notification.IM";
            } else if (audio == L"mail") {
                audioSrc = L"ms-winsoundevent:Notification.Mail";
            } else if (audio == L"reminder") {
                audioSrc = L"ms-winsoundevent:Notification.Reminder";
            } else if (audio == L"sms") {
                audioSrc = L"ms-winsoundevent:Notification.SMS";
            } else if (audio == L"loopingAlarm") {
                audioSrc = L"ms-winsoundevent:Notification.Looping.Alarm";
            } else if (audio == L"loopingCall") {
                audioSrc = L"ms-winsoundevent:Notification.Looping.Call";
            } else if (audio == L"silent") {
                xml += L"<audio silent=\"true\"/>";
            }
            
            if (!audioSrc.empty()) {
                xml += L"<audio src=\"" + audioSrc + L"\"/>";
            }
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
