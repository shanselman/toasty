# install.ps1 - Toasty installer for Windows
# Usage: irm https://raw.githubusercontent.com/shanselman/toasty/main/install.ps1 | iex
# Or pin a version first: $toastyVersion="v0.6"; irm https://raw.githubusercontent.com/shanselman/toasty/main/install.ps1 | iex

function Install-Toasty {
    $ErrorActionPreference = "Stop"

    $repo       = "shanselman/toasty"
    $installDir = Join-Path $env:USERPROFILE ".toasty"
    $exeName    = "toasty.exe"
    $exePath    = Join-Path $installDir $exeName

    # Allow callers to pin a version by setting $toastyVersion before piping
    $version = if (Get-Variable -Name toastyVersion -Scope Global -ErrorAction SilentlyContinue) {
        $global:toastyVersion
    } else { "latest" }

    # ── helpers ──────────────────────────────────────────────────────────────────

    function Write-Step { param($msg) Write-Host "  $msg" -ForegroundColor DarkGray }
    function Write-Ok   { param($msg) Write-Host "  $msg" -ForegroundColor Green }
    function Write-Fail { param($msg) Write-Host "  $msg" -ForegroundColor Red }

    # ── detect architecture ───────────────────────────────────────────────────────

    $isArm64 = [System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -eq `
               [System.Runtime.InteropServices.Architecture]::Arm64
    $arch = if ($isArm64) { "arm64" } else { "x64" }

    Write-Host ""
    Write-Host "Toasty Installer" -ForegroundColor Cyan
    Write-Host "────────────────────────────────────────"
    Write-Step "Architecture : $arch"
    Write-Step "Install dir  : $installDir"
    Write-Host ""

    # ── fetch release info ────────────────────────────────────────────────────────

    try {
        $releaseUrl = if ($version -eq "latest") {
            "https://api.github.com/repos/$repo/releases/latest"
        } else {
            "https://api.github.com/repos/$repo/releases/tags/$version"
        }

        $release   = Invoke-RestMethod -Uri $releaseUrl -Headers @{ "User-Agent" = "toasty-installer" }
        $tag       = $release.tag_name
        $assetName = "toasty-$arch.exe"
        $asset     = $release.assets | Where-Object { $_.name -eq $assetName } | Select-Object -First 1

        if (-not $asset) {
            Write-Fail "Asset '$assetName' not found in release $tag."
            return
        }
    } catch {
        Write-Fail "Failed to fetch release information: $_"
        return
    }

    # ── check for existing install ────────────────────────────────────────────────

    if (Test-Path $exePath) {
        try {
            $current = & $exePath --version 2>&1
            if ($current -match "toasty v(.+)") {
                $installedTag = "v$($Matches[1].Trim())"
                if ($installedTag -eq $tag) {
                    Write-Ok "toasty $tag is already installed and up to date."
                    Write-Host ""
                    return
                }
                Write-Step "Upgrading $installedTag → $tag ..."
            }
        } catch {
            Write-Step "Installing $tag ..."
        }
    } else {
        Write-Step "Installing $tag ..."
    }

    # ── download ──────────────────────────────────────────────────────────────────

    $tempFile = Join-Path $env:TEMP $assetName
    try {
        Write-Step "Downloading $assetName ..."
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tempFile -UseBasicParsing
    } catch {
        Write-Fail "Download failed: $_"
        return
    }

    # ── verify Authenticode signature ─────────────────────────────────────────────

    try {
        $sig = Get-AuthenticodeSignature -FilePath $tempFile
        if ($sig.Status -ne "Valid") {
            Remove-Item $tempFile -Force -ErrorAction SilentlyContinue
            Write-Fail "Signature verification failed (status: $($sig.Status)). Aborting."
            return
        }
        Write-Step "Signature valid : $($sig.SignerCertificate.Subject)"
    } catch {
        Remove-Item $tempFile -Force -ErrorAction SilentlyContinue
        Write-Fail "Signature check failed: $_"
        return
    }

    # ── install ───────────────────────────────────────────────────────────────────

    try {
        if (-not (Test-Path $installDir)) {
            New-Item -ItemType Directory -Path $installDir -Force | Out-Null
        }
        Copy-Item $tempFile $exePath -Force
        Remove-Item $tempFile -Force -ErrorAction SilentlyContinue
    } catch {
        Remove-Item $tempFile -Force -ErrorAction SilentlyContinue
        Write-Fail "Install failed: $_"
        return
    }

    # ── update PATH (persistent) ──────────────────────────────────────────────────

    $userPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if ($userPath -notlike "*$installDir*") {
        $newPath = if ([string]::IsNullOrEmpty($userPath)) {
            $installDir
        } else {
            "$($userPath.TrimEnd(';'));$installDir"
        }
        [Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
        Write-Step "Added $installDir to user PATH"
    }

    # ── update PATH (current session) ────────────────────────────────────────────

    if ($env:PATH -notlike "*$installDir*") {
        $env:PATH = "$env:PATH;$installDir"
    }

    # ── summary ───────────────────────────────────────────────────────────────────

    Write-Host ""
    Write-Host "────────────────────────────────────────"
    Write-Ok "toasty $tag installed successfully!"
    Write-Host "  Location : $exePath"
    Write-Host ""
    Write-Host "Try it:"
    Write-Host '  toasty "Hello from toasty!"' -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Install hooks for your AI agent:"
    Write-Host "  toasty --install" -ForegroundColor Cyan
    Write-Host ""
}

Install-Toasty
