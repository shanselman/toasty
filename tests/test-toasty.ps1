# test-toasty.ps1 - Toasty test runner
# Usage: .\tests\test-toasty.ps1 [-ExePath .\build\Release\toasty.exe]

param(
    [string]$ExePath = ".\build\Release\toasty.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ExePath)) {
    Write-Error "toasty.exe not found at '$ExePath'. Build first."
    exit 1
}

$ExePath = (Resolve-Path $ExePath).Path

# Test framework
$script:passed = 0
$script:failed = 0
$script:errors = @()

function Assert-ExitCode {
    param([string]$Name, [int]$Expected, [int]$Actual)
    if ($Expected -ne $Actual) {
        $script:failed++
        $script:errors += "FAIL: $Name (expected exit code $Expected, got $Actual)"
        Write-Host "  FAIL: $Name (exit code $Actual != $Expected)" -ForegroundColor Red
        return $false
    }
    return $true
}

function Assert-OutputContains {
    param([string]$Name, [string]$Output, [string]$Expected)
    if ($Output -notmatch [regex]::Escape($Expected)) {
        $script:failed++
        $script:errors += "FAIL: $Name (output missing '$Expected')"
        Write-Host "  FAIL: $Name (missing '$Expected')" -ForegroundColor Red
        return $false
    }
    return $true
}

function Assert-OutputNotContains {
    param([string]$Name, [string]$Output, [string]$Expected)
    if ($Output -match [regex]::Escape($Expected)) {
        $script:failed++
        $script:errors += "FAIL: $Name (output should NOT contain '$Expected')"
        Write-Host "  FAIL: $Name (unexpected '$Expected')" -ForegroundColor Red
        return $false
    }
    return $true
}

function Pass {
    param([string]$Name)
    $script:passed++
    Write-Host "  PASS: $Name" -ForegroundColor Green
}

function Run-Toasty {
    param(
        [string[]]$Arguments,
        [hashtable]$Env = @{}
    )
    
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.Arguments = ($Arguments | ForEach-Object { 
        if ($_ -match '\s') { "`"$_`"" } else { $_ }
    }) -join ' '
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    
    foreach ($key in $Env.Keys) {
        $psi.EnvironmentVariables[$key] = $Env[$key]
    }
    
    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdout = $proc.StandardOutput.ReadToEnd()
    $stderr = $proc.StandardError.ReadToEnd()
    $proc.WaitForExit(10000) # 10s timeout
    
    return @{
        ExitCode = $proc.ExitCode
        Stdout   = $stdout
        Stderr   = $stderr
        Output   = $stdout + $stderr
    }
}

# ============================================================
# Test Suite: Arguments
# ============================================================
Write-Host "`nArgument Parsing Tests" -ForegroundColor Cyan
Write-Host ("=" * 40)

# --version
$r = Run-Toasty @("--version")
if ((Assert-ExitCode "--version exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "--version output" $r.Stdout "toasty v")) {
    Pass "--version"
}

# -v (short form)
$r = Run-Toasty @("-v")
if ((Assert-ExitCode "-v exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "-v output" $r.Stdout "toasty v")) {
    Pass "-v short form"
}

# --help
$r = Run-Toasty @("--help")
if ((Assert-ExitCode "--help exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "--help has usage" $r.Stdout "Usage:") -and
    (Assert-OutputContains "--help has --dry-run" $r.Stdout "--dry-run")) {
    Pass "--help"
}

# -h (short form)
$r = Run-Toasty @("-h")
if ((Assert-ExitCode "-h exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "-h has usage" $r.Stdout "Usage:")) {
    Pass "-h short form"
}

# No args shows usage (exit 0)
$r = Run-Toasty @()
if (Assert-ExitCode "no args exits 0" 0 $r.ExitCode) {
    Pass "no args shows usage"
}

# Missing message with options
$r = Run-Toasty @("--app", "claude", "--dry-run")
if (Assert-ExitCode "no message exits 1" 1 $r.ExitCode) {
    Pass "missing message error"
}

# Bad --app preset
$r = Run-Toasty @("test", "--app", "nonexistent", "--dry-run")
if ((Assert-ExitCode "bad app exits 1" 1 $r.ExitCode) -and
    (Assert-OutputContains "bad app error" $r.Output "Unknown app preset")) {
    Pass "bad --app preset"
}

# --title without argument
$r = Run-Toasty @("test", "--title")
if (Assert-ExitCode "--title no arg exits 1" 1 $r.ExitCode) {
    Pass "--title missing argument"
}

# ============================================================
# Test Suite: Presets (via --dry-run)
# ============================================================
Write-Host "`nPreset Tests" -ForegroundColor Cyan
Write-Host ("=" * 40)

$presets = @(
    @{ Name = "claude";  ExpectedTitle = "Claude" },
    @{ Name = "copilot"; ExpectedTitle = "GitHub Copilot" },
    @{ Name = "gemini";  ExpectedTitle = "Gemini" },
    @{ Name = "codex";   ExpectedTitle = "Codex" },
    @{ Name = "cursor";  ExpectedTitle = "Cursor" }
)

foreach ($preset in $presets) {
    $r = Run-Toasty @("Test message", "--app", $preset.Name, "--dry-run")
    if ((Assert-ExitCode "$($preset.Name) preset exits 0" 0 $r.ExitCode) -and
        (Assert-OutputContains "$($preset.Name) has title" $r.Stdout "[dry-run] Title: $($preset.ExpectedTitle)") -and
        (Assert-OutputContains "$($preset.Name) has message" $r.Stdout "[dry-run] Message: Test message") -and
        (Assert-OutputContains "$($preset.Name) has XML" $r.Stdout "Toast XML:") -and
        (Assert-OutputContains "$($preset.Name) has icon" $r.Stdout "[dry-run] Icon:")) {
        Pass "--app $($preset.Name)"
    }
}

# Custom title overrides preset
$r = Run-Toasty @("Test", "--app", "claude", "-t", "My Title", "--dry-run")
if ((Assert-ExitCode "custom title exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "custom title" $r.Stdout "[dry-run] Title: My Title")) {
    Pass "custom title overrides preset"
}

# ============================================================
# Test Suite: Toast XML Validation
# ============================================================
Write-Host "`nToast XML Tests" -ForegroundColor Cyan
Write-Host ("=" * 40)

# Basic XML structure
$r = Run-Toasty @("Hello World", "--dry-run")
$xml = $r.Stdout
if ((Assert-OutputContains "has toast element" $xml "activationType=`"protocol`"") -and
    (Assert-OutputContains "has protocol launch" $xml "launch=`"toasty://focus`"") -and
    (Assert-OutputContains "has message text" $xml "<text>Hello World</text>") -and
    (Assert-OutputContains "has binding" $xml "template=`"ToastGeneric`"")) {
    Pass "toast XML structure"
}

# XML escaping
$r = Run-Toasty @("A & B <test>", "--dry-run")
if ((Assert-ExitCode "xml escape exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "ampersand escaped" $r.Stdout "&amp;") -and
    (Assert-OutputContains "lt escaped" $r.Stdout "&lt;")) {
    Pass "XML special char escaping"
}

# Icon included in XML
$r = Run-Toasty @("test", "--app", "claude", "--dry-run")
if (Assert-OutputContains "icon in XML" $r.Stdout "appLogoOverride") {
    Pass "icon included in toast XML"
}

# ============================================================
# Test Suite: Install --dry-run
# ============================================================
Write-Host "`nInstall Tests" -ForegroundColor Cyan
Write-Host ("=" * 40)

# Install claude
$r = Run-Toasty @("--install", "claude", "--dry-run")
if ((Assert-ExitCode "install claude exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "install claude target" $r.Stdout "Install targets: claude") -and
    (Assert-OutputContains "install claude path" $r.Stdout "settings.json") -and
    (Assert-OutputContains "install claude hook" $r.Stdout "Hook type: Stop")) {
    Pass "install claude --dry-run"
}

# Install gemini
$r = Run-Toasty @("--install", "gemini", "--dry-run")
if ((Assert-ExitCode "install gemini exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "install gemini target" $r.Stdout "Install targets: gemini") -and
    (Assert-OutputContains "install gemini hook" $r.Stdout "Hook type: AfterAgent")) {
    Pass "install gemini --dry-run"
}

# Install copilot
$r = Run-Toasty @("--install", "copilot", "--dry-run")
if ((Assert-ExitCode "install copilot exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "install copilot target" $r.Stdout "Install targets: copilot") -and
    (Assert-OutputContains "install copilot hook" $r.Stdout "Hook type: sessionEnd") -and
    (Assert-OutputContains "install copilot path" $r.Stdout "toasty.json")) {
    Pass "install copilot --dry-run"
}

# Install all (no agent specified)
$r = Run-Toasty @("--install", "--dry-run")
if ((Assert-ExitCode "install all exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "install all claude" $r.Stdout "claude") -and
    (Assert-OutputContains "install all gemini" $r.Stdout "gemini") -and
    (Assert-OutputContains "install all copilot" $r.Stdout "copilot")) {
    Pass "install all --dry-run"
}

# Uninstall
$r = Run-Toasty @("--uninstall", "--dry-run")
if ((Assert-ExitCode "uninstall exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "uninstall claude" $r.Stdout "Claude:") -and
    (Assert-OutputContains "uninstall gemini" $r.Stdout "Gemini:") -and
    (Assert-OutputContains "uninstall copilot" $r.Stdout "Copilot:")) {
    Pass "uninstall --dry-run"
}

# ============================================================
# Test Suite: ntfy Configuration
# ============================================================
Write-Host "`nntfy Tests" -ForegroundColor Cyan
Write-Host ("=" * 40)

# ntfy not configured
$r = Run-Toasty @("test", "--dry-run")
if ((Assert-ExitCode "ntfy unconfigured exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "ntfy not configured" $r.Stdout "ntfy: not configured")) {
    Pass "ntfy not configured"
}

# ntfy configured with topic
$r = Run-Toasty -Arguments @("test", "--dry-run") -Env @{ TOASTY_NTFY_TOPIC = "my-topic" }
if ((Assert-ExitCode "ntfy configured exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "ntfy shows topic" $r.Stdout "ntfy.sh/my-topic")) {
    Pass "ntfy with topic"
}

# ntfy with custom server
$r = Run-Toasty -Arguments @("test", "--dry-run") -Env @{ TOASTY_NTFY_TOPIC = "my-topic"; TOASTY_NTFY_SERVER = "notify.example.com" }
if ((Assert-ExitCode "ntfy custom server exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "ntfy custom server" $r.Stdout "notify.example.com/my-topic")) {
    Pass "ntfy with custom server"
}

# ============================================================
# Summary
# ============================================================
Write-Host "`n$("=" * 40)" -ForegroundColor Cyan
$total = $script:passed + $script:failed
Write-Host "Results: $($script:passed)/$total passed" -ForegroundColor $(if ($script:failed -eq 0) { "Green" } else { "Red" })

if ($script:failed -gt 0) {
    Write-Host "`nFailures:" -ForegroundColor Red
    foreach ($err in $script:errors) {
        Write-Host "  $err" -ForegroundColor Red
    }
    exit 1
}

exit 0
