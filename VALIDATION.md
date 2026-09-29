# Toasty Installer — Manual Validation Checklist

This checklist must be completed by a maintainer before promoting the PR out of draft
and merging the installer scripts.  Run each scenario on real hardware; VMs are
acceptable for WSL and Git Bash steps.

---

## Prerequisites

- A Windows machine with PowerShell 5.1+ and PowerShell 7+ available.
- Git for Windows (Git Bash) installed.
- WSL2 with a default distro (Ubuntu recommended) and interop **enabled**.
- A published GitHub release with signed `toasty-x64.exe` (and optionally `toasty-x64.exe.sha256`).
- Internet access.

---

## 1. PowerShell installer (`install.ps1`)

### 1.1 Fresh install (PowerShell 5.1)

```powershell
# In a fresh PowerShell 5.1 session — nothing in ~/.toasty
irm https://raw.githubusercontent.com/shanselman/toasty/main/install.ps1 | iex
```

**Expected:**
- [ ] Downloads latest release asset for detected architecture (x64 or arm64).
- [ ] Authenticode signature check passes (shows signer subject).
- [ ] `~/.toasty/toasty.exe` exists.
- [ ] `~/.toasty` is in the current session `$env:PATH`.
- [ ] `~/.toasty` is in the persistent user PATH (verify via `[Environment]::GetEnvironmentVariable("PATH","User")`).
- [ ] `toasty "Hello"` shows a Windows toast notification.
- [ ] Opening a **new** PowerShell window: `toasty --version` works.

### 1.2 Fresh install (PowerShell 7+)

Repeat 1.1 in `pwsh`.  Same expected results.

### 1.3 Version-pinned install

```powershell
$toastyVersion = "v0.5"; irm https://raw.githubusercontent.com/shanselman/toasty/main/install.ps1 | iex
```

**Expected:**
- [ ] Installs the pinned version, not latest.
- [ ] `toasty --version` outputs the pinned version.

### 1.4 Up-to-date (no-op)

Re-run the installer when the latest version is already installed.

**Expected:**
- [ ] Prints "already installed and up to date" and exits cleanly.
- [ ] Does **not** re-download or overwrite the binary.

### 1.5 Upgrade

Install an older version (see 1.3), then re-run without `$toastyVersion`.

**Expected:**
- [ ] Shows "Upgrading vX.Y → vX.Z".
- [ ] `toasty --version` reflects the new version after install.

### 1.6 Signature verification failure

Modify the downloaded temp file to corrupt the binary before the signature check
(use `Set-Content $env:TEMP\toasty-x64.exe -Value "bad" -Encoding Byte` or similar).

> This step requires a locally modified copy of `install.ps1` that skips the download
> and uses a pre-staged corrupted file.

**Expected:**
- [ ] Installer prints "Signature verification failed" in red.
- [ ] Corrupted temp file is deleted.
- [ ] `~/.toasty/toasty.exe` is **not** updated.

### 1.7 PATH idempotency

Run the installer twice.

**Expected:**
- [ ] `~/.toasty` appears **once** in the persistent user PATH.
- [ ] No duplicate entries.

---

## 2. Bash installer — Git Bash (`install.sh`)

### 2.1 Fresh install

```bash
# In a fresh Git Bash session
curl -fsSL https://raw.githubusercontent.com/shanselman/toasty/main/install.sh | bash
```

**Expected:**
- [ ] Environment detected as `gitbash`.
- [ ] `INSTALL_DIR` resolves using `cygpath` to a Unix-style path under the Windows
      user profile (e.g. `/c/Users/<name>/.toasty`).
- [ ] If release ships `toasty-x64.exe.sha256`: SHA256 check passes.
- [ ] If no checksum published: warning printed, install proceeds.
- [ ] `~/.toasty/toasty.exe` exists.
- [ ] `~/.bashrc` (or first existing profile) contains `export PATH=…/.toasty`.
- [ ] Current session: `toasty "Hello"` fires a toast.
- [ ] Fresh shell (`exec bash --login`): `toasty "Hello"` works without re-running
      the installer.

### 2.2 Version-pinned install

```bash
TOASTY_VERSION=v0.5 curl -fsSL https://raw.githubusercontent.com/shanselman/toasty/main/install.sh | bash
```

**Expected:**
- [ ] Installs specified version.
- [ ] `toasty --version` matches.

### 2.3 Up-to-date (no-op)

**Expected:**
- [ ] Prints "already installed and up to date".

### 2.4 Upgrade

Install older version, then re-run without `TOASTY_VERSION`.

**Expected:**
- [ ] Shows "Upgrading vX.Y → vX.Z".

### 2.5 SHA256 mismatch (integrity gate)

Stage a corrupted binary at the temp path used by the script and supply a mismatched
checksum file.

**Expected:**
- [ ] Installer prints "SHA256 checksum mismatch — aborting!".
- [ ] Corrupted temp file is removed.
- [ ] `~/.toasty/toasty.exe` is **not** updated.

### 2.6 PATH idempotency

Run installer twice.

**Expected:**
- [ ] `~/.toasty` appears **once** in the relevant shell profile.

---

## 3. Bash installer — WSL (`install.sh`)

### 3.1 Fresh install (WSL interop enabled)

```bash
# In WSL — USERPROFILE must be set (usually auto-inherited from Windows)
echo "$USERPROFILE"   # must be non-empty, e.g. C:\Users\<name>
curl -fsSL https://raw.githubusercontent.com/shanselman/toasty/main/install.sh | bash
```

**Expected:**
- [ ] Environment detected as `wsl`.
- [ ] `INSTALL_DIR` resolves via `wslpath` to a path under the Windows filesystem
      (e.g. `/mnt/c/Users/<name>/.toasty`), **not** under `/home`.
- [ ] `toasty.exe` copied to that path; `toasty` wrapper script created alongside it.
- [ ] SHA256 verification same as Git Bash (2.1).
- [ ] `~/.bashrc` PATH entry uses the `/mnt/c/…/.toasty` path.
- [ ] Current session: `toasty "Hello"` fires a toast (no `.exe` needed thanks to wrapper).
- [ ] Fresh shell (`exec bash --login`): `toasty "Hello"` works.

### 3.2 WSL interop disabled / USERPROFILE unset

Temporarily clear `USERPROFILE` before running:

```bash
unset USERPROFILE
curl -fsSL https://raw.githubusercontent.com/shanselman/toasty/main/install.sh | bash
```

**Expected:**
- [ ] Warning message printed about Linux filesystem install.
- [ ] Install continues to `$HOME/.toasty`.
- [ ] Warning about needing WSL interop is shown.

### 3.3 Version-pinned, up-to-date, upgrade, idempotency

Repeat 2.2 – 2.6 in WSL.  Expected results are identical.

---

## 4. Uninstall (all environments)

No uninstall script exists yet; document the manual steps.

```powershell
# PowerShell
Remove-Item -Recurse -Force ~/.toasty
# Remove from user PATH manually (System Properties → Environment Variables)
```

```bash
# Git Bash / WSL
rm -rf ~/.toasty
# Remove the export line from ~/.bashrc (or relevant profile)
```

**Expected:**
- [ ] No stale PATH entries remain after the above steps.
- [ ] Re-running the installer does a clean fresh install.

---

## Sign-off

| Environment      | Tester | Date | Result |
|------------------|--------|------|--------|
| PowerShell 5.1   |        |      |        |
| PowerShell 7+    |        |      |        |
| Git Bash         |        |      |        |
| WSL2 (Ubuntu)    |        |      |        |
