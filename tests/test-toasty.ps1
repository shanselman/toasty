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
        [hashtable]$Env = @{},
        [string]$StdinInput = $null
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
    if ($null -ne $StdinInput) {
        $psi.RedirectStandardInput = $true
    }
    
    foreach ($key in $Env.Keys) {
        $psi.EnvironmentVariables[$key] = $Env[$key]
    }
    
    $proc = [System.Diagnostics.Process]::Start($psi)
    if ($null -ne $StdinInput) {
        $proc.StandardInput.Write($StdinInput)
        $proc.StandardInput.Close()
    }
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
    (Assert-OutputContains "has protocol launch" $xml "launch=`"toasty://focus") -and
    (Assert-OutputContains "has message text" $xml "<text>Hello World</text>") -and
    (Assert-OutputContains "has binding" $xml "template=`"ToastGeneric`"")) {
    Pass "toast XML structure"
}

# --launch-hwnd embeds the HWND in the toast launch URL.
# Use a real HWND (this PowerShell process's main window) so IsWindow() passes;
# otherwise the code safely falls back to ancestor-walk.
Add-Type -Namespace W -Name N -MemberDefinition '[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();' -ErrorAction SilentlyContinue
$realHwnd = [int64][W.N]::GetForegroundWindow()
$r = Run-Toasty @("Hi", "--dry-run", "--launch-hwnd", "$realHwnd")
if (Assert-OutputContains "launch URL has hwnd" $r.Stdout "launch=`"toasty://focus?hwnd=$realHwnd`"") {
    Pass "launch-hwnd embedded in toast launch URL"
}

# Watchdog tool path stashes hwnd in pending file (best-effort: depends on
# whether Cmder/CI environment has any visible ancestor window). Just verify
# the field exists and is numeric.
$pendingDir = Join-Path $env:LOCALAPPDATA "toasty\copilot-pending"
$lhCwd = "C:\lh-test-" + ([Guid]::NewGuid().ToString("N").Substring(0,12))
$env:TOASTY_COPILOT_IDLE_SEC = "3600"
try {
    $stdin = '{"timestamp":1700000000000,"cwd":"' + ($lhCwd -replace '\\','\\') + '","toolName":"bash","toolArgs":"{}"}'
    Run-Toasty @("--copilot-hook", "tool") -StdinInput $stdin | Out-Null
    $f = @(Get-ChildItem $pendingDir -Filter "*.json" -ErrorAction SilentlyContinue | Where-Object {
        try { (Get-Content $_.FullName -Raw | ConvertFrom-Json).cwd -eq $lhCwd } catch { $false }
    })
    if ($f.Count -eq 1) {
        $rec = Get-Content $f[0].FullName -Raw | ConvertFrom-Json
        if ($rec.PSObject.Properties.Name -contains "hwnd") {
            Pass "tool hook records hwnd field in pending"
        } else {
            $script:failed++
            Write-Host "  FAIL: pending file missing 'hwnd' field" -ForegroundColor Red
        }
        Remove-Item $f[0].FullName -Force -ErrorAction SilentlyContinue
    } else {
        $script:failed++
        Write-Host "  FAIL: tool hook did not create pending file for hwnd test" -ForegroundColor Red
    }
} finally {
    Remove-Item Env:\TOASTY_COPILOT_IDLE_SEC -ErrorAction SilentlyContinue
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
    (Assert-OutputContains "install copilot path" $r.Stdout "toasty.json") -and
    (Assert-OutputContains "install copilot prompt hook" $r.Stdout "userPromptSubmitted") -and
    (Assert-OutputContains "install copilot end cmd" $r.Stdout "--copilot-hook end") -and
    (Assert-OutputContains "install copilot prompt cmd" $r.Stdout "--copilot-hook prompt") -and
    (Assert-OutputContains "install copilot tool cmd" $r.Stdout "--copilot-hook tool") -and
    (Assert-OutputContains "install copilot postToolUse" $r.Stdout "postToolUse")) {
    Pass "install copilot --dry-run"
}

# Install codex
$r = Run-Toasty @("--install", "codex", "--dry-run")
if ((Assert-ExitCode "install codex exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "install codex target" $r.Stdout "Install targets: codex") -and
    (Assert-OutputContains "install codex path" $r.Stdout "config.toml") -and
    (Assert-OutputContains "install codex hook" $r.Stdout "Hook type: notify")) {
    Pass "install codex --dry-run"
}

# Install all (no agent specified)
$r = Run-Toasty @("--install", "--dry-run")
if ((Assert-ExitCode "install all exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "install all claude" $r.Stdout "claude") -and
    (Assert-OutputContains "install all gemini" $r.Stdout "gemini") -and
    (Assert-OutputContains "install all copilot" $r.Stdout "copilot") -and
    (Assert-OutputContains "install all codex" $r.Stdout "codex")) {
    Pass "install all --dry-run"
}

# Uninstall
$r = Run-Toasty @("--uninstall", "--dry-run")
if ((Assert-ExitCode "uninstall exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "uninstall claude" $r.Stdout "Claude:") -and
    (Assert-OutputContains "uninstall gemini" $r.Stdout "Gemini:") -and
    (Assert-OutputContains "uninstall copilot" $r.Stdout "Copilot:") -and
    (Assert-OutputContains "uninstall codex" $r.Stdout "Codex:")) {
    Pass "uninstall --dry-run"
}

# ============================================================
# Test Suite: Copilot hook events
# ============================================================
Write-Host "`nCopilot Hook Tests" -ForegroundColor Cyan
Write-Host ("=" * 40)

$copilotRepoRoot = Join-Path ([System.IO.Path]::GetTempPath()) "toasty-copilot-test-$(Get-Random)"
$cwdEsc = $copilotRepoRoot.Replace('\', '\\')
$folderName = Split-Path -Leaf $copilotRepoRoot

# end with no stdin (manual invocation) must not hang and must show fallback
$r = Run-Toasty @("--copilot-hook", "end", "--dry-run")
if ((Assert-ExitCode "hook end no-stdin exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "hook end no-stdin title" $r.Stdout "Title: GitHub Copilot") -and
    (Assert-OutputContains "hook end no-stdin message" $r.Stdout "Message: Finished")) {
    Pass "copilot-hook end with no stdin (no hang, fallback)"
}

# end with garbage JSON: tolerated
$r = Run-Toasty -Arguments @("--copilot-hook", "end", "--dry-run") -StdinInput "not json"
if ((Assert-ExitCode "hook end garbage exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "hook end garbage fallback" $r.Stdout "Message: Finished")) {
    Pass "copilot-hook end with invalid JSON"
}

# end with reason=timeout but no cached prompt
$payload = '{"timestamp":1700000000000,"cwd":"' + $cwdEsc + '","reason":"timeout"}'
$r = Run-Toasty -Arguments @("--copilot-hook", "end", "--dry-run") -StdinInput $payload
if ((Assert-ExitCode "hook end timeout exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "hook end timeout title" $r.Stdout "GitHub Copilot - timed out") -and
    (Assert-OutputContains "hook end timeout message" $r.Stdout "Timed out - $folderName")) {
    Pass "copilot-hook end with timeout reason"
}

# end with reason=error
$payload = '{"timestamp":1700000000000,"cwd":"' + $cwdEsc + '","reason":"error"}'
$r = Run-Toasty -Arguments @("--copilot-hook", "end", "--dry-run") -StdinInput $payload
if ((Assert-OutputContains "hook end error title" $r.Stdout "GitHub Copilot - failed") -and
    (Assert-OutputContains "hook end error message" $r.Stdout "Failed - $folderName")) {
    Pass "copilot-hook end with error reason"
}

# end with reason=user_exit
$payload = '{"timestamp":1700000000000,"cwd":"' + $cwdEsc + '","reason":"user_exit"}'
$r = Run-Toasty -Arguments @("--copilot-hook", "end", "--dry-run") -StdinInput $payload
if ((Assert-OutputContains "hook end user_exit title" $r.Stdout "GitHub Copilot - session ended") -and
    (Assert-OutputContains "hook end user_exit message" $r.Stdout "Session ended - $folderName")) {
    Pass "copilot-hook end with user_exit reason"
}

# prompt event always exits silently with no toast
$payload = '{"timestamp":1700000000000,"cwd":"' + $cwdEsc + '","prompt":"hello"}'
$r = Run-Toasty -Arguments @("--copilot-hook", "prompt", "--dry-run") -StdinInput $payload
if ((Assert-ExitCode "hook prompt exits 0" 0 $r.ExitCode) -and
    (Assert-OutputNotContains "hook prompt no toast" $r.Stdout "Toast XML")) {
    Pass "copilot-hook prompt is silent"
}

# Round-trip: cache a prompt then end picks it up. Use a unique cwd to avoid
# interference from other test cases.
$rtCwd = Join-Path ([System.IO.Path]::GetTempPath()) "toasty-rt-$(Get-Random)"
$rtCwdEsc = $rtCwd.Replace('\', '\\')
$rtFolder = Split-Path -Leaf $rtCwd
$promptPayload = '{"timestamp":1700000000000,"cwd":"' + $rtCwdEsc + '","prompt":"Refactor the auth module"}'
$endPayload = '{"timestamp":1700000001000,"cwd":"' + $rtCwdEsc + '","reason":"complete"}'
Run-Toasty -Arguments @("--copilot-hook", "prompt") -StdinInput $promptPayload | Out-Null
$r = Run-Toasty -Arguments @("--copilot-hook", "end", "--dry-run") -StdinInput $endPayload
if ((Assert-ExitCode "hook roundtrip exits 0" 0 $r.ExitCode) -and
    (Assert-OutputContains "hook roundtrip title" $r.Stdout "Title: GitHub Copilot") -and
    (Assert-OutputContains "hook roundtrip prompt in msg" $r.Stdout "Refactor the auth module") -and
    (Assert-OutputContains "hook roundtrip folder in msg" $r.Stdout $rtFolder)) {
    Pass "copilot-hook prompt -> end round trip"
}

# Subsequent end with same cwd (cache consumed) falls back to generic message
$r = Run-Toasty -Arguments @("--copilot-hook", "end", "--dry-run") -StdinInput $endPayload
if (Assert-OutputContains "hook cache consumed" $r.Stdout "Message: Finished - $rtFolder") {
    Pass "copilot-hook prompt cache consumed after end"
}

# Multiline prompt is normalized to single line
$mlPayload = '{"timestamp":1700000010000,"cwd":"' + $rtCwdEsc + '","prompt":"line one\n\nline   two\ttab"}'
$endPayload2 = '{"timestamp":1700000011000,"cwd":"' + $rtCwdEsc + '","reason":"complete"}'
Run-Toasty -Arguments @("--copilot-hook", "prompt") -StdinInput $mlPayload | Out-Null
$r = Run-Toasty -Arguments @("--copilot-hook", "end", "--dry-run") -StdinInput $endPayload2
if ((Assert-OutputContains "hook normalized prompt" $r.Stdout "line one line two tab") -and
    (Assert-OutputNotContains "hook no newline in msg" $r.Stdout "line one`n")) {
    Pass "copilot-hook prompt whitespace normalization"
}

# ---- Idle watchdog (postToolUse) tests ----
# Use a unique cwd value per run so its FNV-1a hash maps to a unique pending file.
$pendingDir = Join-Path $env:LOCALAPPDATA "toasty\copilot-pending"
$wdCwd = "C:\watchdog-test-" + ([System.Guid]::NewGuid().ToString("N").Substring(0,12))
$ts = [int64]([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())

# A `tool` event creates a pending file. Use a very long idle so the watchdog
# child won't fire during the test.
$env:TOASTY_COPILOT_IDLE_SEC = "3600"
try {
    $stdin = '{"timestamp":' + $ts + ',"cwd":"' + ($wdCwd -replace '\\','\\') + '","toolName":"bash","toolArgs":"{}"}'
    $r = Run-Toasty @("--copilot-hook", "tool") -StdinInput $stdin
    $pendingFiles = @(Get-ChildItem $pendingDir -Filter "*.json" -ErrorAction SilentlyContinue | Where-Object {
        try { (Get-Content $_.FullName -Raw | ConvertFrom-Json).cwd -eq $wdCwd } catch { $false }
    })
    if ($pendingFiles.Count -eq 1) {
        Pass "tool hook creates pending file"
        $rec = Get-Content $pendingFiles[0].FullName -Raw | ConvertFrom-Json
        $expectedFire = $ts + 3600 * 1000
        # fireMs should be in the near future (allow generous skew for slow CI)
        if ($rec.fireMs -ge ($ts + 3500*1000)) { Pass "tool hook sets fireMs in the future" }
        else { $script:failed++; Write-Host "  FAIL: fireMs $($rec.fireMs) not in expected range" -ForegroundColor Red }
    } else {
        $script:failed++
        Write-Host "  FAIL: tool hook should create exactly 1 pending file, found $($pendingFiles.Count)" -ForegroundColor Red
    }

    # A `prompt` event for the same cwd cancels the pending file
    $stdin2 = '{"timestamp":' + ($ts + 100) + ',"cwd":"' + ($wdCwd -replace '\\','\\') + '","prompt":"hello"}'
    $r = Run-Toasty @("--copilot-hook", "prompt") -StdinInput $stdin2
    $remaining = @(Get-ChildItem $pendingDir -Filter "*.json" -ErrorAction SilentlyContinue | Where-Object {
        try { (Get-Content $_.FullName -Raw | ConvertFrom-Json).cwd -eq $wdCwd } catch { $false }
    })
    if ($remaining.Count -eq 0) { Pass "prompt hook cancels pending watchdog" }
    else { $script:failed++; Write-Host "  FAIL: prompt hook left $($remaining.Count) pending file(s)" -ForegroundColor Red }

    # tool then end also cancels pending
    $r = Run-Toasty @("--copilot-hook", "tool") -StdinInput $stdin
    $stdin3 = '{"timestamp":' + ($ts + 200) + ',"cwd":"' + ($wdCwd -replace '\\','\\') + '","reason":"complete"}'
    $r = Run-Toasty @("--copilot-hook", "end") -StdinInput $stdin3
    $remaining = @(Get-ChildItem $pendingDir -Filter "*.json" -ErrorAction SilentlyContinue | Where-Object {
        try { (Get-Content $_.FullName -Raw | ConvertFrom-Json).cwd -eq $wdCwd } catch { $false }
    })
    if ($remaining.Count -eq 0) { Pass "end hook cancels pending watchdog" }
    else { $script:failed++; Write-Host "  FAIL: end hook left $($remaining.Count) pending file(s)" -ForegroundColor Red }
} finally {
    Remove-Item Env:\TOASTY_COPILOT_IDLE_SEC -ErrorAction SilentlyContinue
    # Clean up any straggler files from this cwd
    Get-ChildItem $pendingDir -ErrorAction SilentlyContinue | Where-Object {
        try {
            $c = Get-Content $_.FullName -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
            $c.cwd -eq $wdCwd
        } catch { $false }
    } | Remove-Item -Force -ErrorAction SilentlyContinue
}

# ---- Session-name lookup tests ----
# Create a fake ~/.copilot/session-state/<UUID>/workspace.yaml fixture, then
# verify the toast title includes the session name.
$fakeId = [Guid]::NewGuid().ToString().ToLower()
$snStateDir = Join-Path $env:USERPROFILE ".copilot\session-state\$fakeId"
New-Item -ItemType Directory $snStateDir -Force | Out-Null
$snFolder = "fake-folder-$(Get-Random)"
$snCwd = Join-Path ([System.IO.Path]::GetTempPath()) $snFolder
$snCwdEsc = $snCwd -replace '\\','\\'
try {
    @"
id: $fakeId
cwd: $snCwd
repository: shanselman/toasty
branch: main
summary: auto-generated-summary
name: my-cool-session
"@ | Set-Content -LiteralPath (Join-Path $snStateDir "workspace.yaml") -Encoding utf8

    # end event with sessionId picks up the name
    $payload = '{"timestamp":1700000000000,"sessionId":"' + $fakeId + '","cwd":"' + $snCwdEsc + '","reason":"complete"}'
    $r = Run-Toasty @("--copilot-hook", "end", "--dry-run") -StdinInput $payload
    if (Assert-OutputContains "end title has session name" $r.Stdout "GitHub Copilot - my-cool-session") {
        Pass "session name appears in end title (complete)"
    }

    # error reason: name + parenthesized status
    $payload = '{"timestamp":1700000000000,"sessionId":"' + $fakeId + '","cwd":"' + $snCwdEsc + '","reason":"error"}'
    $r = Run-Toasty @("--copilot-hook", "end", "--dry-run") -StdinInput $payload
    if (Assert-OutputContains "end title has name + reason" $r.Stdout "GitHub Copilot - my-cool-session (failed)") {
        Pass "session name appears in end title (error)"
    }

    # Falls back to summary when name is absent
    @"
id: $fakeId
cwd: $snCwd
summary: auto-generated-summary
"@ | Set-Content -LiteralPath (Join-Path $snStateDir "workspace.yaml") -Encoding utf8
    $payload = '{"timestamp":1700000000000,"sessionId":"' + $fakeId + '","cwd":"' + $snCwdEsc + '","reason":"complete"}'
    $r = Run-Toasty @("--copilot-hook", "end", "--dry-run") -StdinInput $payload
    if (Assert-OutputContains "summary fallback" $r.Stdout "GitHub Copilot - auto-generated-summary") {
        Pass "session name falls back to summary"
    }

    # Missing/invalid sessionId: title is unchanged "GitHub Copilot"
    $payload = '{"timestamp":1700000000000,"cwd":"' + $snCwdEsc + '","reason":"complete"}'
    $r = Run-Toasty @("--copilot-hook", "end", "--dry-run") -StdinInput $payload
    if ((Assert-OutputContains "no sessionId -> plain title" $r.Stdout "Title: GitHub Copilot") -and
        (Assert-OutputNotContains "no sessionId -> no dash suffix" $r.Stdout "GitHub Copilot - ")) {
        Pass "missing sessionId leaves title unchanged"
    }

    # Path-traversal sessionId is rejected by validator
    $payload = '{"timestamp":1700000000000,"sessionId":"../../../etc/passwd","cwd":"' + $snCwdEsc + '","reason":"complete"}'
    $r = Run-Toasty @("--copilot-hook", "end", "--dry-run") -StdinInput $payload
    if (Assert-OutputNotContains "no path traversal in title" $r.Stdout "passwd") {
        Pass "invalid sessionId rejected"
    }

    # tool event: sessionName is persisted into the pending file
    @"
id: $fakeId
cwd: $snCwd
name: my-cool-session
"@ | Set-Content -LiteralPath (Join-Path $snStateDir "workspace.yaml") -Encoding utf8
    $env:TOASTY_COPILOT_IDLE_SEC = "3600"
    try {
        $snToolCwd = "C:\sn-tool-test-" + ([Guid]::NewGuid().ToString("N").Substring(0,12))
        $stdin = '{"timestamp":1700000000000,"sessionId":"' + $fakeId + '","cwd":"' + ($snToolCwd -replace '\\','\\') + '","toolName":"bash","toolArgs":"{}"}'
        Run-Toasty @("--copilot-hook", "tool") -StdinInput $stdin | Out-Null
        $f = @(Get-ChildItem (Join-Path $env:LOCALAPPDATA "toasty\copilot-pending") -Filter "*.json" -ErrorAction SilentlyContinue | Where-Object {
            try { (Get-Content $_.FullName -Raw | ConvertFrom-Json).cwd -eq $snToolCwd } catch { $false }
        })
        if ($f.Count -eq 1) {
            $rec = Get-Content $f[0].FullName -Raw | ConvertFrom-Json
            if ($rec.sessionName -eq "my-cool-session") {
                Pass "tool hook stashes sessionName in pending file"
            } else {
                $script:failed++
                Write-Host "  FAIL: pending sessionName = '$($rec.sessionName)', expected 'my-cool-session'" -ForegroundColor Red
            }
            Remove-Item $f[0].FullName -Force -ErrorAction SilentlyContinue
        } else {
            $script:failed++
            Write-Host "  FAIL: tool hook with sessionId did not create pending file" -ForegroundColor Red
        }
    } finally {
        Remove-Item Env:\TOASTY_COPILOT_IDLE_SEC -ErrorAction SilentlyContinue
    }
} finally {
    Remove-Item -LiteralPath $snStateDir -Recurse -Force -ErrorAction SilentlyContinue
}

# Real install in a temp dir produces both hooks; idempotent
$tmpRepo = Join-Path ([System.IO.Path]::GetTempPath()) "toasty-cpt-install-$(Get-Random)"
New-Item -ItemType Directory $tmpRepo | Out-Null
Push-Location $tmpRepo
try {
    & $ExePath --install copilot 2>&1 | Out-Null
    & $ExePath --install copilot 2>&1 | Out-Null
    $cfg = Get-Content (Join-Path $tmpRepo ".github\hooks\toasty.json") -Raw | ConvertFrom-Json
    $seCount = @($cfg.hooks.sessionEnd).Count
    $upsCount = @($cfg.hooks.userPromptSubmitted).Count
    $ptuCount = @($cfg.hooks.postToolUse).Count
    if ($seCount -ne 1) {
        $script:failed++
        Write-Host "  FAIL: install idempotent sessionEnd count = $seCount, expected 1" -ForegroundColor Red
    } elseif ($upsCount -ne 1) {
        $script:failed++
        Write-Host "  FAIL: install idempotent userPromptSubmitted count = $upsCount, expected 1" -ForegroundColor Red
    } elseif ($ptuCount -ne 1) {
        $script:failed++
        Write-Host "  FAIL: install idempotent postToolUse count = $ptuCount, expected 1" -ForegroundColor Red
    } elseif ($cfg.hooks.sessionEnd[0].bash -notmatch '--copilot-hook end') {
        $script:failed++
        Write-Host "  FAIL: sessionEnd bash command wrong: $($cfg.hooks.sessionEnd[0].bash)" -ForegroundColor Red
    } elseif ($cfg.hooks.userPromptSubmitted[0].bash -notmatch '--copilot-hook prompt') {
        $script:failed++
        Write-Host "  FAIL: userPromptSubmitted bash command wrong" -ForegroundColor Red
    } elseif ($cfg.hooks.postToolUse[0].bash -notmatch '--copilot-hook tool') {
        $script:failed++
        Write-Host "  FAIL: postToolUse bash command wrong: $($cfg.hooks.postToolUse[0].bash)" -ForegroundColor Red
    } else {
        Pass "install copilot is idempotent (real fs)"
    }
} finally {
    Pop-Location
    Remove-Item -Recurse -Force $tmpRepo -ErrorAction SilentlyContinue
}

# ============================================================
# Test Suite: Copilot Global Install
# ============================================================
Write-Host "`nCopilot Global Install Tests" -ForegroundColor Cyan
Write-Host ("=" * 40)

# Dry-run --global shows global path and scope
$r = Run-Toasty @("--install", "copilot", "--global", "--dry-run") -Env @{ USERPROFILE = "C:\fake-home" }
if (Assert-OutputContains "global dry-run shows global path" $r.Stdout "C:\fake-home\.copilot\hooks\toasty.json") { Pass "global dry-run shows global path" }
if (Assert-OutputContains "global dry-run shows scope label" $r.Stdout "global (user-level)") { Pass "global dry-run shows scope label" }

# Dry-run without --global stays repo-scoped
$r = Run-Toasty @("--install", "copilot", "--dry-run")
if (Assert-OutputContains "repo dry-run scope label" $r.Stdout "repo (per-project)") { Pass "repo dry-run scope label" }
if (Assert-OutputContains "repo dry-run path" $r.Stdout ".github\hooks\toasty.json") { Pass "repo dry-run path" }
if (Assert-OutputNotContains "repo dry-run hides global path" $r.Stdout ".copilot\hooks\toasty.json") { Pass "repo dry-run hides global path" }

# Real install with --global to a temp HOME, then verify file + idempotent re-install
$tmpHome = Join-Path $env:TEMP ("toasty-fakehome-" + [System.Guid]::NewGuid().ToString("N").Substring(0,8))
New-Item -ItemType Directory $tmpHome -Force | Out-Null
$tmpRepo = Join-Path $env:TEMP ("toasty-globalrepo-" + [System.Guid]::NewGuid().ToString("N").Substring(0,8))
New-Item -ItemType Directory $tmpRepo -Force | Out-Null
$prevCwd = Get-Location
try {
    Set-Location $tmpRepo
    $globalCfg = Join-Path $tmpHome ".copilot\hooks\toasty.json"

    $r = Run-Toasty @("--install", "copilot", "--global") -Env @{ USERPROFILE = $tmpHome }
    if (Assert-OutputContains "global install reports success" $r.Stdout "Added sessionEnd + userPromptSubmitted + postToolUse hooks (global)") { Pass "global install reports success" }
    if (Test-Path $globalCfg) { Pass "global install creates ~/.copilot/hooks/toasty.json" }
    else { $script:failed++; $script:errors += "FAIL: global install did not create file"; Write-Host "  FAIL: global install did not create file" -ForegroundColor Red }

    # File contains both hook events
    $cfgContent = Get-Content $globalCfg -Raw
    if ($cfgContent -match "sessionEnd" -and $cfgContent -match "userPromptSubmitted") { Pass "global config has both hook events" }
    else { $script:failed++; $script:errors += "FAIL: global config missing hooks"; Write-Host "  FAIL: global config missing hooks" -ForegroundColor Red }

    # Idempotent: re-install should not duplicate entries
    $r = Run-Toasty @("--install", "copilot", "--global") -Env @{ USERPROFILE = $tmpHome }
    $cfgContent2 = Get-Content $globalCfg -Raw
    $endCount = ([regex]::Matches($cfgContent2, "sessionEnd")).Count
    $promptCount = ([regex]::Matches($cfgContent2, "userPromptSubmitted")).Count
    if ($endCount -eq 1 -and $promptCount -eq 1) { Pass "global re-install is idempotent" }
    else { $script:failed++; $script:errors += "FAIL: global re-install duplicated entries (sessionEnd=$endCount, userPromptSubmitted=$promptCount)"; Write-Host "  FAIL: global re-install duplicated" -ForegroundColor Red }

    # Status surfaces global install
    $r = Run-Toasty @("--status") -Env @{ USERPROFILE = $tmpHome }
    if (Assert-OutputContains "status shows global hook" $r.Stdout "[x] GitHub Copilot (global") { Pass "status shows global hook" }

    # Uninstall removes global file even when no repo file exists
    $r = Run-Toasty @("--uninstall") -Env @{ USERPROFILE = $tmpHome }
    if (Assert-OutputContains "uninstall reports copilot removal" $r.Stdout "GitHub Copilot: Removed hooks") { Pass "uninstall reports copilot removal" }
    if (-not (Test-Path $globalCfg)) { Pass "uninstall removes global config" }
    else { $script:failed++; $script:errors += "FAIL: uninstall left global config behind"; Write-Host "  FAIL: uninstall left global config" -ForegroundColor Red }
} finally {
    Set-Location $prevCwd
    Remove-Item -Recurse -Force $tmpHome -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $tmpRepo -ErrorAction SilentlyContinue
}

# Help text mentions --global
$r = Run-Toasty @("--help")
if (Assert-OutputContains "help mentions --global" $r.Stdout "--global") { Pass "help mentions --global" }
if (Assert-OutputContains "help mentions --no-session-end" $r.Stdout "--no-session-end") { Pass "help mentions --no-session-end" }

# --no-session-end skips sessionEnd: dry-run
$r = Run-Toasty @("--install", "copilot", "--global", "--no-session-end", "--dry-run")
if ((Assert-OutputContains "dry-run lists tool+prompt without sessionEnd" $r.Stdout "userPromptSubmitted, postToolUse") -and
    (Assert-OutputNotContains "dry-run skips sessionEnd command" $r.Stdout "Hook command (sessionEnd)")) {
    Pass "--no-session-end dry-run skips sessionEnd"
}

# --no-session-end on real install actually omits sessionEnd from JSON,
# and re-installing without the flag adds it back. Use a fresh tmp HOME.
$tmpHome = Join-Path ([System.IO.Path]::GetTempPath()) "toasty-nse-$(Get-Random)"
New-Item -ItemType Directory $tmpHome | Out-Null
$prevHome = $env:USERPROFILE
$env:USERPROFILE = $tmpHome
try {
    & $ExePath --install copilot --global --no-session-end 2>&1 | Out-Null
    $cfgPath = Join-Path $tmpHome ".copilot\hooks\toasty.json"
    $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
    if (-not ($cfg.hooks.PSObject.Properties.Name -contains "sessionEnd")) {
        Pass "--no-session-end omits sessionEnd from config"
    } else {
        $script:failed++
        Write-Host "  FAIL: sessionEnd present despite --no-session-end" -ForegroundColor Red
    }
    if (@($cfg.hooks.userPromptSubmitted).Count -eq 1 -and
        @($cfg.hooks.postToolUse).Count -eq 1) {
        Pass "--no-session-end keeps prompt + tool hooks"
    } else {
        $script:failed++
        Write-Host "  FAIL: --no-session-end should still install prompt + tool hooks" -ForegroundColor Red
    }

    # Re-installing WITHOUT the flag puts sessionEnd back.
    & $ExePath --install copilot --global 2>&1 | Out-Null
    $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
    if (@($cfg.hooks.sessionEnd).Count -eq 1 -and
        @($cfg.hooks.userPromptSubmitted).Count -eq 1) {
        Pass "re-install without --no-session-end restores sessionEnd"
    } else {
        $script:failed++
        Write-Host "  FAIL: re-install did not restore sessionEnd" -ForegroundColor Red
    }

    # Re-installing WITH the flag again removes the previously-added sessionEnd.
    & $ExePath --install copilot --global --no-session-end 2>&1 | Out-Null
    $cfg = Get-Content $cfgPath -Raw | ConvertFrom-Json
    if (-not ($cfg.hooks.PSObject.Properties.Name -contains "sessionEnd")) {
        Pass "--no-session-end on re-install strips existing sessionEnd"
    } else {
        $script:failed++
        Write-Host "  FAIL: --no-session-end did not strip existing sessionEnd" -ForegroundColor Red
    }
} finally {
    $env:USERPROFILE = $prevHome
    Remove-Item -Recurse -Force $tmpHome -ErrorAction SilentlyContinue
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
