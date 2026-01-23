// Toasty - macOS implementation
// Native Objective-C++ for macOS notifications

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>
#import <libproc.h>
#import <sys/sysctl.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

// App presets for AI agents
struct AppPreset {
    std::string name;
    std::string title;
    std::string iconFile;
};

const AppPreset APP_PRESETS[] = {
    { "claude", "Claude", "claude.png" },
    { "copilot", "GitHub Copilot", "copilot.png" },
    { "gemini", "Gemini", "gemini.png" },
    { "codex", "Codex", "codex.png" },
    { "cursor", "Cursor", "cursor.png" }
};

// Convert std::string to NSString
NSString* toNSString(const std::string& str) {
    return [NSString stringWithUTF8String:str.c_str()];
}

// Convert NSString to std::string
std::string toString(NSString* nsstr) {
    return nsstr ? [nsstr UTF8String] : "";
}

// Convert string to lowercase
std::string toLower(std::string str) {
    for (auto& c : str) c = tolower(c);
    return str;
}

// Find preset by name (case-insensitive)
const AppPreset* findPreset(const std::string& name) {
    auto lowerName = toLower(name);
    for (const auto& preset : APP_PRESETS) {
        if (toLower(preset.name) == lowerName) {
            return &preset;
        }
    }
    return nullptr;
}

// Get executable path
std::string getExePath() {
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(getpid(), path, sizeof(path)) > 0) {
        return std::string(path);
    }
    return "";
}

// Get home directory
std::string getHomeDir() {
    return toString(NSHomeDirectory());
}

// Get icons directory (next to executable or in Resources)
std::string getIconsDir() {
    std::string exePath = getExePath();
    fs::path exeDir = fs::path(exePath).parent_path();
    
    // Check for icons directory next to executable
    fs::path iconsDir = exeDir / "icons";
    if (fs::exists(iconsDir)) {
        return iconsDir.string();
    }
    
    // Check in Resources (for .app bundle)
    fs::path resourcesDir = exeDir.parent_path() / "Resources" / "icons";
    if (fs::exists(resourcesDir)) {
        return resourcesDir.string();
    }
    
    // Fallback to source directory icons
    fs::path sourceIcons = exeDir.parent_path() / "icons";
    if (fs::exists(sourceIcons)) {
        return sourceIcons.string();
    }
    
    return "";
}

// Get icon path for preset
std::string getIconPath(const AppPreset* preset) {
    if (!preset) return "";
    
    std::string iconsDir = getIconsDir();
    if (iconsDir.empty()) return "";
    
    fs::path iconPath = fs::path(iconsDir) / preset->iconFile;
    if (fs::exists(iconPath)) {
        return iconPath.string();
    }
    return "";
}

// Get process name by PID
std::string getProcessName(pid_t pid) {
    char name[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_name(pid, name, sizeof(name)) > 0) {
        return std::string(name);
    }
    return "";
}

// Get process path by PID
std::string getProcessPath(pid_t pid) {
    char path[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(pid, path, sizeof(path)) > 0) {
        return std::string(path);
    }
    return "";
}

// Get parent PID
pid_t getParentPid(pid_t pid) {
    struct kinfo_proc info;
    size_t length = sizeof(info);
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, pid };
    
    if (sysctl(mib, 4, &info, &length, NULL, 0) == 0) {
        return info.kp_eproc.e_ppid;
    }
    return 0;
}

// Check if process path contains a known CLI pattern
const AppPreset* checkProcessForPreset(const std::string& path, const std::string& name) {
    auto lowerPath = toLower(path);
    auto lowerName = toLower(name);
    
    // Check for Gemini CLI
    if (lowerPath.find("gemini-cli") != std::string::npos ||
        lowerPath.find("gemini/cli") != std::string::npos ||
        lowerPath.find("@google/gemini") != std::string::npos) {
        return findPreset("gemini");
    }
    
    // Check for Claude Code
    if (lowerPath.find("claude-code") != std::string::npos ||
        lowerPath.find("@anthropic") != std::string::npos ||
        lowerName == "claude") {
        return findPreset("claude");
    }
    
    // Check for Cursor
    if (lowerName.find("cursor") != std::string::npos) {
        return findPreset("cursor");
    }
    
    // Check for Copilot
    if (lowerName == "copilot" || lowerPath.find("github-copilot") != std::string::npos) {
        return findPreset("copilot");
    }
    
    // Check for Codex
    if (lowerName == "codex") {
        return findPreset("codex");
    }
    
    // Direct preset name match
    return findPreset(lowerName);
}

// Walk up process tree to find a matching AI CLI preset
const AppPreset* detectPresetFromAncestors(bool debug = false) {
    pid_t currentPid = getpid();
    
    if (debug) {
        std::cerr << "[DEBUG] Starting from PID: " << currentPid << "\n";
    }
    
    // Walk up the process tree (max 20 levels)
    for (int depth = 0; depth < 20; depth++) {
        pid_t parentPid = getParentPid(currentPid);
        
        if (parentPid == 0 || parentPid == 1 || parentPid == currentPid) {
            break; // Reached root or init
        }
        
        std::string procName = getProcessName(parentPid);
        std::string procPath = getProcessPath(parentPid);
        
        if (debug) {
            std::cerr << "[DEBUG] Level " << depth << ": PID=" << parentPid
                      << " Name=" << procName << "\n";
            std::cerr << "[DEBUG]   Path: " << procPath << "\n";
        }
        
        const AppPreset* preset = checkProcessForPreset(procPath, procName);
        if (preset) {
            if (debug) std::cerr << "[DEBUG] MATCH: " << preset->name << "\n";
            return preset;
        }
        
        currentPid = parentPid;
    }
    
    return nullptr;
}

// Show notification using terminal-notifier (reliable on modern macOS)
bool showNotification(const std::string& title, const std::string& message, const std::string& iconPath) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process - detach from parent
        setsid();
        
        // Try homebrew path first (Apple Silicon)
        if (!iconPath.empty() && fs::exists(iconPath)) {
            execl("/opt/homebrew/bin/terminal-notifier", "terminal-notifier",
                  "-title", title.c_str(),
                  "-message", message.c_str(),
                  "-contentImage", iconPath.c_str(),
                  "-sound", "default",
                  nullptr);
        } else {
            execl("/opt/homebrew/bin/terminal-notifier", "terminal-notifier",
                  "-title", title.c_str(),
                  "-message", message.c_str(),
                  "-sound", "default",
                  nullptr);
        }
        // Try /usr/local/bin as fallback (Intel Macs)
        if (!iconPath.empty() && fs::exists(iconPath)) {
            execl("/usr/local/bin/terminal-notifier", "terminal-notifier",
                  "-title", title.c_str(),
                  "-message", message.c_str(),
                  "-contentImage", iconPath.c_str(),
                  "-sound", "default",
                  nullptr);
        } else {
            execl("/usr/local/bin/terminal-notifier", "terminal-notifier",
                  "-title", title.c_str(),
                  "-message", message.c_str(),
                  "-sound", "default",
                  nullptr);
        }
        _exit(1);
    }
    
    // Parent - don't wait, let child run independently  
    return pid > 0;
}

// Escape special characters in JSON strings
std::string escapeJsonString(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c;
        }
    }
    return result;
}

// Read file contents
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Write file contents
bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file) return false;
    file << content;
    return file.good();
}

// Check if JSON contains toasty hook
bool hasToastyHook(const std::string& jsonContent) {
    return jsonContent.find("toasty") != std::string::npos;
}

// Claude Code hook installation
bool isClaudeInstalled() {
    std::string configPath = getHomeDir() + "/.claude/settings.json";
    if (!fs::exists(configPath)) return false;
    return hasToastyHook(readFile(configPath));
}

bool installClaude() {
    std::string configDir = getHomeDir() + "/.claude";
    std::string configPath = configDir + "/settings.json";
    std::string exePath = getExePath();
    
    // Create directory if needed
    fs::create_directories(configDir);
    
    std::string existingContent = readFile(configPath);
    
    // Build the hook command
    std::string hookCmd = escapeJsonString(exePath) + " \\\"Task complete\\\" -t \\\"Claude Code\\\"";
    
    std::string hookJson = R"({
  "hooks": {
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": ")" + hookCmd + R"(",
            "timeout": 5000
          }
        ]
      }
    ]
  }
})";
    
    if (existingContent.empty() || existingContent.find("hooks") == std::string::npos) {
        return writeFile(configPath, hookJson);
    }
    
    // TODO: Merge with existing hooks
    std::cerr << "Claude settings.json already has hooks. Manual merge required.\n";
    return false;
}

bool uninstallClaude() {
    std::string configPath = getHomeDir() + "/.claude/settings.json";
    if (!fs::exists(configPath)) return true;
    
    // TODO: Remove toasty hook from existing config
    std::cerr << "Manual removal required from " << configPath << "\n";
    return false;
}

// Gemini CLI hook installation
bool isGeminiInstalled() {
    std::string configPath = getHomeDir() + "/.gemini/settings.json";
    if (!fs::exists(configPath)) return false;
    return hasToastyHook(readFile(configPath));
}

bool installGemini() {
    std::string configDir = getHomeDir() + "/.gemini";
    std::string configPath = configDir + "/settings.json";
    std::string exePath = getExePath();
    
    fs::create_directories(configDir);
    
    std::string existingContent = readFile(configPath);
    
    std::string hookCmd = escapeJsonString(exePath) + " \\\"Gemini finished\\\" -t \\\"Gemini\\\"";
    
    std::string hookJson = R"({
  "hooks": {
    "AfterAgent": [
      {
        "hooks": [
          {
            "type": "command",
            "command": ")" + hookCmd + R"(",
            "timeout": 5000
          }
        ]
      }
    ]
  }
})";
    
    if (existingContent.empty() || existingContent.find("hooks") == std::string::npos) {
        return writeFile(configPath, hookJson);
    }
    
    std::cerr << "Gemini settings.json already has hooks. Manual merge required.\n";
    return false;
}

bool uninstallGemini() {
    std::string configPath = getHomeDir() + "/.gemini/settings.json";
    if (!fs::exists(configPath)) return true;
    std::cerr << "Manual removal required from " << configPath << "\n";
    return false;
}

// GitHub Copilot hook installation (repo-local)
bool isCopilotInstalled() {
    return fs::exists(".github/hooks/toasty.json");
}

bool installCopilot() {
    fs::create_directories(".github/hooks");
    
    std::string hookJson = R"({
  "version": 1,
  "hooks": {
    "sessionEnd": [
      {
        "type": "command",
        "bash": "toasty 'Copilot finished' -t 'GitHub Copilot'",
        "timeoutSec": 5
      }
    ]
  }
})";
    
    return writeFile(".github/hooks/toasty.json", hookJson);
}

bool uninstallCopilot() {
    if (fs::exists(".github/hooks/toasty.json")) {
        return fs::remove(".github/hooks/toasty.json");
    }
    return true;
}

// Check if agents are available
bool isClaudeAvailable() {
    return fs::exists(getHomeDir() + "/.claude");
}

bool isGeminiAvailable() {
    return fs::exists(getHomeDir() + "/.gemini");
}

bool isCopilotAvailable() {
    return fs::exists(".git");
}

// Status command
void showStatus() {
    std::cout << "Toasty Hook Status\n";
    std::cout << "==================\n\n";
    
    std::cout << "Claude Code:    ";
    if (isClaudeInstalled()) {
        std::cout << "[x] Installed\n";
    } else if (isClaudeAvailable()) {
        std::cout << "[ ] Available (not installed)\n";
    } else {
        std::cout << "[-] Not detected\n";
    }
    
    std::cout << "Gemini CLI:     ";
    if (isGeminiInstalled()) {
        std::cout << "[x] Installed\n";
    } else if (isGeminiAvailable()) {
        std::cout << "[ ] Available (not installed)\n";
    } else {
        std::cout << "[-] Not detected\n";
    }
    
    std::cout << "GitHub Copilot: ";
    if (isCopilotInstalled()) {
        std::cout << "[x] Installed (this repo)\n";
    } else if (isCopilotAvailable()) {
        std::cout << "[ ] Available (not installed)\n";
    } else {
        std::cout << "[-] Not a git repository\n";
    }
}

// Install command
void runInstall(const std::string& agent) {
    std::cout << "Detecting AI CLI agents...\n";
    
    bool claudeAvail = isClaudeAvailable();
    bool geminiAvail = isGeminiAvailable();
    bool copilotAvail = isCopilotAvailable();
    
    std::cout << "  " << (claudeAvail ? "[x]" : "[ ]") << " Claude Code" << (isClaudeInstalled() ? " (already installed)" : "") << "\n";
    std::cout << "  " << (geminiAvail ? "[x]" : "[ ]") << " Gemini CLI" << (isGeminiInstalled() ? " (already installed)" : "") << "\n";
    std::cout << "  " << (copilotAvail ? "[x]" : "[ ]") << " GitHub Copilot (in current repo)" << (isCopilotInstalled() ? " (already installed)" : "") << "\n";
    
    std::cout << "\nInstalling toasty hooks...\n";
    
    bool anyInstalled = false;
    auto lowerAgent = toLower(agent);
    
    if ((lowerAgent.empty() || lowerAgent == "all" || lowerAgent == "claude") && claudeAvail && !isClaudeInstalled()) {
        if (installClaude()) {
            std::cout << "  [x] Claude Code: Added Stop hook\n";
            anyInstalled = true;
        } else {
            std::cout << "  [ ] Claude Code: Failed to install\n";
        }
    }
    
    if ((lowerAgent.empty() || lowerAgent == "all" || lowerAgent == "gemini") && geminiAvail && !isGeminiInstalled()) {
        if (installGemini()) {
            std::cout << "  [x] Gemini CLI: Added AfterAgent hook\n";
            anyInstalled = true;
        } else {
            std::cout << "  [ ] Gemini CLI: Failed to install\n";
        }
    }
    
    if ((lowerAgent.empty() || lowerAgent == "all" || lowerAgent == "copilot") && copilotAvail && !isCopilotInstalled()) {
        if (installCopilot()) {
            std::cout << "  [x] GitHub Copilot: Added sessionEnd hook\n";
            anyInstalled = true;
        } else {
            std::cout << "  [ ] GitHub Copilot: Failed to install\n";
        }
    }
    
    if (anyInstalled) {
        std::cout << "\nDone! You'll get notifications when AI agents finish.\n";
    } else {
        std::cout << "\nNo new hooks installed.\n";
    }
}

// Uninstall command
void runUninstall() {
    std::cout << "Removing toasty hooks...\n";
    
    bool anyRemoved = false;
    
    if (isClaudeInstalled()) {
        if (uninstallClaude()) {
            std::cout << "  [x] Claude Code: Removed hooks\n";
            anyRemoved = true;
        }
    }
    
    if (isGeminiInstalled()) {
        if (uninstallGemini()) {
            std::cout << "  [x] Gemini CLI: Removed hooks\n";
            anyRemoved = true;
        }
    }
    
    if (isCopilotInstalled()) {
        if (uninstallCopilot()) {
            std::cout << "  [x] GitHub Copilot: Removed hooks\n";
            anyRemoved = true;
        }
    }
    
    if (anyRemoved) {
        std::cout << "\nDone! Hooks have been removed.\n";
    } else {
        std::cout << "\nNo hooks were installed.\n";
    }
}

// Print usage
void printUsage() {
    std::cout << "toasty - macOS toast notification CLI\n\n"
              << "Usage:\n"
              << "  toasty <message> [options]\n"
              << "  toasty --install [agent]\n"
              << "  toasty --uninstall\n"
              << "  toasty --status\n\n"
              << "Options:\n"
              << "  -t, --title <text>   Set notification title (default: \"Notification\")\n"
              << "  --app <name>         Use AI CLI preset (claude, copilot, gemini, codex, cursor)\n"
              << "  -i, --icon <path>    Custom icon path (PNG recommended)\n"
              << "  -h, --help           Show this help\n"
              << "  --install [agent]    Install hooks for AI CLI agents (claude, gemini, copilot, or all)\n"
              << "  --uninstall          Remove hooks from all AI CLI agents\n"
              << "  --status             Show installation status\n"
              << "  --debug              Show process detection debug info\n\n"
              << "Note: Toasty auto-detects known parent processes (Claude, Copilot, etc.)\n"
              << "      and applies the appropriate preset automatically. Use --app to override.\n\n"
              << "Examples:\n"
              << "  toasty \"Build completed\"\n"
              << "  toasty \"Task done\" -t \"Custom Title\"\n"
              << "  toasty \"Analysis complete\" --app claude\n"
              << "  toasty --install\n"
              << "  toasty --status\n";
}

int main(int argc, char* argv[]) {
    @autoreleasepool {
        std::vector<std::string> args;
        for (int i = 1; i < argc; i++) {
            args.push_back(argv[i]);
        }
        
        if (args.empty()) {
            printUsage();
            return 0;
        }
        
        // Parse arguments
        std::string message;
        std::string title = "Notification";
        std::string iconPath;
        std::string appPreset;
        bool debug = false;
        bool doInstall = false;
        bool doUninstall = false;
        bool doStatus = false;
        std::string installAgent;
        
        for (size_t i = 0; i < args.size(); i++) {
            const auto& arg = args[i];
            
            if (arg == "-h" || arg == "--help") {
                printUsage();
                return 0;
            } else if (arg == "--status") {
                doStatus = true;
            } else if (arg == "--install") {
                doInstall = true;
                if (i + 1 < args.size() && args[i + 1][0] != '-') {
                    installAgent = args[++i];
                }
            } else if (arg == "--uninstall") {
                doUninstall = true;
            } else if (arg == "--debug") {
                debug = true;
            } else if ((arg == "-t" || arg == "--title") && i + 1 < args.size()) {
                title = args[++i];
            } else if (arg == "--app" && i + 1 < args.size()) {
                appPreset = args[++i];
            } else if ((arg == "-i" || arg == "--icon") && i + 1 < args.size()) {
                iconPath = args[++i];
            } else if (arg[0] != '-') {
                message = arg;
            }
        }
        
        // Handle commands
        if (doStatus) {
            showStatus();
            return 0;
        }
        
        if (doInstall) {
            runInstall(installAgent);
            return 0;
        }
        
        if (doUninstall) {
            runUninstall();
            return 0;
        }
        
        // Need a message for notification
        if (message.empty()) {
            std::cerr << "Error: No message provided.\n";
            printUsage();
            return 1;
        }
        
        // Auto-detect preset from parent process
        const AppPreset* preset = nullptr;
        if (!appPreset.empty()) {
            preset = findPreset(appPreset);
            if (!preset) {
                std::cerr << "Warning: Unknown app preset '" << appPreset << "'\n";
            }
        } else {
            preset = detectPresetFromAncestors(debug);
        }
        
        // Apply preset
        if (preset) {
            if (title == "Notification") {
                title = preset->title;
            }
            if (iconPath.empty()) {
                iconPath = getIconPath(preset);
            }
        }
        
        // Show notification
        if (!showNotification(title, message, iconPath)) {
            std::cerr << "Error: Failed to show notification.\n";
            return 1;
        }
        
        return 0;
    }
}
