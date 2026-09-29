#!/usr/bin/env bash
# install.sh - Toasty installer for Windows (Git Bash / WSL)
# Usage: curl -fsSL https://raw.githubusercontent.com/shanselman/toasty/main/install.sh | bash
# Or with a specific version: TOASTY_VERSION=v0.6 bash install.sh

set -euo pipefail

REPO="shanselman/toasty"
EXE_NAME="toasty.exe"
VERSION="${TOASTY_VERSION:-latest}"

# ── colors ───────────────────────────────────────────────────────────────────

reset="\033[0m"; cyan="\033[36m"; green="\033[32m"; red="\033[31m"; gray="\033[90m"; yellow="\033[33m"
step()  { echo -e "  ${gray}$*${reset}"; }
ok()    { echo -e "  ${green}$*${reset}"; }
fail()  { echo -e "  ${red}$*${reset}"; }
warn()  { echo -e "  ${yellow}Warning: $*${reset}"; }

# ── detect runtime environment ────────────────────────────────────────────────

# Detect Git Bash / MSYS2 / Cygwin first (checked via uname), then WSL.
# toasty is Windows-only; exit if neither environment is detected.
_uname="$(uname -s 2>/dev/null || true)"
if [[ "$_uname" == MINGW* ]] || [[ "$_uname" == MSYS* ]] || [[ "$_uname" == CYGWIN* ]]; then
    ENV_TYPE="gitbash"
elif [[ -n "${WSL_DISTRO_NAME:-}" ]] || grep -qi "microsoft\|wsl" /proc/version 2>/dev/null; then
    ENV_TYPE="wsl"
elif command -v cmd.exe &>/dev/null; then
    # Fallback for Windows shell environments (e.g. MSYS2 older builds) that do
    # not set MINGW*/MSYS in uname but still have cmd.exe on PATH via interop.
    # PATH conversion defaults to cygpath if available, otherwise $HOME.
    ENV_TYPE="gitbash"
else
    fail "toasty is a Windows application."
    fail "Please run this script in Git Bash, WSL, or use install.ps1 in PowerShell."
    exit 1
fi

# ── resolve install directory with proper path conversion ─────────────────────
#
# toasty.exe must live on the Windows filesystem so it can call the Windows
# notification APIs.  We therefore resolve INSTALL_DIR relative to the Windows
# user profile, using the appropriate path-conversion utility for each shell.
#
#   Git Bash / MSYS2:  cygpath -u converts "C:\Users\…" → "/c/Users/…"
#   WSL:               wslpath   converts "C:\Users\…" → "/mnt/c/Users/…"

if [[ "$ENV_TYPE" == "gitbash" ]]; then
    if command -v cygpath &>/dev/null && [[ -n "${USERPROFILE:-}" ]]; then
        INSTALL_DIR="$(cygpath -u "$USERPROFILE")/.toasty"
    else
        # $HOME in Git Bash is already the Unix-style Windows profile path
        INSTALL_DIR="$HOME/.toasty"
    fi
elif [[ "$ENV_TYPE" == "wsl" ]]; then
    if command -v wslpath &>/dev/null && [[ -n "${USERPROFILE:-}" ]]; then
        INSTALL_DIR="$(wslpath "$USERPROFILE")/.toasty"
    else
        INSTALL_DIR="$HOME/.toasty"
        warn "USERPROFILE not set or wslpath unavailable; installing to Linux filesystem."
        warn "WSL interop must be enabled for toasty.exe to run from $HOME/.toasty."
    fi
fi

EXE_PATH="$INSTALL_DIR/$EXE_NAME"

# ── detect architecture ───────────────────────────────────────────────────────

detect_arch() {
    local machine
    machine="$(uname -m 2>/dev/null || echo x86_64)"
    case "$machine" in
        aarch64|arm64) echo "arm64" ;;
        *)             echo "x64"   ;;
    esac
}

ARCH="$(detect_arch)"

echo ""
echo -e "${cyan}Toasty Installer${reset}"
echo "────────────────────────────────────────"
step "Environment  : $ENV_TYPE"
step "Architecture : $ARCH"
step "Install dir  : $INSTALL_DIR"
echo ""

# ── require curl ─────────────────────────────────────────────────────────────

if ! command -v curl &>/dev/null; then
    fail "curl is required but not found. Please install curl and try again."
    exit 1
fi

# ── fetch release info ────────────────────────────────────────────────────────

if [[ "$VERSION" == "latest" ]]; then
    RELEASE_URL="https://api.github.com/repos/$REPO/releases/latest"
else
    RELEASE_URL="https://api.github.com/repos/$REPO/releases/tags/$VERSION"
fi

step "Fetching release info ..."
release_json="$(curl -fsSL -H "User-Agent: toasty-installer" "$RELEASE_URL")"

tag="$(echo "$release_json" | grep -o '"tag_name":"[^"]*"' | head -1 | cut -d'"' -f4)"
asset_name="toasty-${ARCH}.exe"
download_url="$(echo "$release_json" | grep -o "\"browser_download_url\":\"[^\"]*${asset_name}\"" | head -1 | cut -d'"' -f4)"

if [[ -z "$download_url" ]]; then
    fail "Could not find $asset_name in release $tag."
    exit 1
fi

# ── check for existing install ────────────────────────────────────────────────

if [[ -f "$EXE_PATH" ]]; then
    installed_tag="$("$EXE_PATH" --version 2>/dev/null | grep -o 'v[0-9][^ ]*' | head -1 || true)"
    if [[ "$installed_tag" == "$tag" ]]; then
        ok "toasty $tag is already installed and up to date."
        echo ""
        exit 0
    fi
    if [[ -n "$installed_tag" ]]; then
        step "Upgrading $installed_tag → $tag ..."
    else
        step "Installing $tag ..."
    fi
else
    step "Installing $tag ..."
fi

# ── download ──────────────────────────────────────────────────────────────────

tmp_dir="${TMPDIR:-/tmp}"
tmp_file="$tmp_dir/toasty_$$.exe"
tmp_sha="$tmp_dir/toasty_$$.sha256"
trap 'rm -f "$tmp_file" "$tmp_sha"' EXIT

step "Downloading $asset_name ..."
if ! curl -fsSL --progress-bar "$download_url" -o "$tmp_file"; then
    fail "Download failed."
    exit 1
fi

# ── SHA256 integrity verification ─────────────────────────────────────────────
#
# We look for a per-file checksum (<asset>.sha256) or a combined SHA256SUMS
# file published alongside the release binary.  Verifying the checksum provides
# an integrity guarantee beyond HTTPS transport alone.
#
# If no checksum file is found in the release assets the installer warns the
# user and proceeds; the release maintainer should publish checksums.

checksum_name="${asset_name}.sha256"
checksum_url="$(echo "$release_json" | grep -o "\"browser_download_url\":\"[^\"]*${checksum_name}\"" | head -1 | cut -d'"' -f4 || true)"

# Fallback: look for a combined SHA256SUMS file
if [[ -z "$checksum_url" ]]; then
    checksum_url="$(echo "$release_json" | grep -o '"browser_download_url":"[^"]*SHA256SUMS"' | head -1 | cut -d'"' -f4 || true)"
fi

if [[ -n "$checksum_url" ]]; then
    step "Verifying SHA256 checksum ..."
    if curl -fsSL -H "User-Agent: toasty-installer" "$checksum_url" -o "$tmp_sha" 2>/dev/null; then
        # Checksum file may be "<hash>  <filename>" or just "<hash>"
        if grep -qi "${asset_name}" "$tmp_sha" 2>/dev/null; then
            expected_sha="$(grep -i "${asset_name}" "$tmp_sha" | awk '{print $1}')"
        else
            expected_sha="$(awk 'NR==1{print $1}' "$tmp_sha")"
        fi
        actual_sha="$(sha256sum "$tmp_file" | awk '{print $1}')"
        # Use tr for lowercase comparison — compatible with bash 3.x
        if [[ "$(echo "$expected_sha" | tr '[:upper:]' '[:lower:]')" != "$(echo "$actual_sha" | tr '[:upper:]' '[:lower:]')" ]]; then
            fail "SHA256 checksum mismatch — aborting!"
            fail "  Expected : $expected_sha"
            fail "  Actual   : $actual_sha"
            exit 1  # trap handles temp file cleanup
        fi
        ok "SHA256 verified ($actual_sha)"
    else
        warn "Checksum file download failed; proceeding with HTTPS-only trust."
    fi
else
    warn "No checksum file found in release assets."
    warn "Integrity relies on HTTPS transport. Publish ${checksum_name} in future releases."
fi

# ── install ───────────────────────────────────────────────────────────────────

mkdir -p "$INSTALL_DIR"
cp "$tmp_file" "$EXE_PATH"
chmod +x "$EXE_PATH"

# ── WSL: install a thin wrapper so 'toasty' works without the .exe suffix ─────
#
# In WSL, PATH entries pointing at Windows binaries require calling the file
# with the .exe extension.  A small shell wrapper lets users run 'toasty'
# (no extension) and is automatically sourced via PATH in a fresh shell.

if [[ "$ENV_TYPE" == "wsl" ]]; then
    wrapper="$INSTALL_DIR/toasty"
    cat > "$wrapper" << 'WRAPPER'
#!/usr/bin/env bash
# WSL wrapper: delegates to the native Windows binary via WSL interop
exec "$(dirname "$(realpath "$0")")/toasty.exe" "$@"
WRAPPER
    chmod +x "$wrapper"
    step "Created toasty wrapper for WSL (no .exe suffix needed)"
fi

# ── update PATH (shell profile) ───────────────────────────────────────────────

path_line="export PATH=\"\$PATH:$INSTALL_DIR\""
profile_updated=false

for profile in "$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.profile"; do
    if [[ -f "$profile" ]]; then
        if ! grep -qF "$INSTALL_DIR" "$profile" 2>/dev/null; then
            printf '\n# Toasty - toast notification CLI\n%s\n' "$path_line" >> "$profile"
            step "Added $INSTALL_DIR to PATH in $profile"
        fi
        profile_updated=true
        break
    fi
done

# If no profile file exists yet, create ~/.bashrc
if [[ "$profile_updated" == false ]]; then
    printf '# Toasty - toast notification CLI\n%s\n' "$path_line" > "$HOME/.bashrc"
    step "Created $HOME/.bashrc with PATH entry"
fi

# ── update PATH (current session) ────────────────────────────────────────────

export PATH="$PATH:$INSTALL_DIR"

# ── summary ───────────────────────────────────────────────────────────────────

echo ""
echo "────────────────────────────────────────"
ok "toasty $tag installed successfully!"
echo "  Location : $EXE_PATH"
echo ""
echo "Try it (reload your shell or run: source ~/.bashrc):"
echo -e "  ${cyan}toasty \"Hello from toasty!\"${reset}"
echo ""
echo "Install hooks for your AI agent:"
echo -e "  ${cyan}toasty --install${reset}"
echo ""
