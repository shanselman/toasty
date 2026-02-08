# Submitting Toasty to Windows Package Manager (winget)

This document explains how to submit Toasty to the official [winget-pkgs](https://github.com/microsoft/winget-pkgs) repository.

## Prerequisites

- A GitHub account
- Fork of the [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) repository

## Submission Process

### 1. Fork the winget-pkgs Repository

Visit https://github.com/microsoft/winget-pkgs and click "Fork" to create your own copy.

### 2. Copy the Manifest Files

Copy the entire `manifests/s/shanselman/toasty/0.6/` directory from this repository to your fork of winget-pkgs at the same path:

```
winget-pkgs/
└── manifests/
    └── s/
        └── shanselman/
            └── toasty/
                └── 0.6/
                    ├── shanselman.toasty.yaml
                    ├── shanselman.toasty.installer.yaml
                    └── shanselman.toasty.locale.en-US.yaml
```

### 3. Validate the Manifest (Optional but Recommended)

Install the [winget client](https://learn.microsoft.com/en-us/windows/package-manager/winget/) and validate the manifest locally:

```cmd
winget validate --manifest manifests/s/shanselman/toasty/0.6/
```

You can also test the installation locally:

```cmd
winget install --manifest manifests/s/shanselman/toasty/0.6/
```

### 4. Create a Pull Request

1. Commit your changes to your fork
2. Create a pull request from your fork to the main winget-pkgs repository
3. Title the PR: `New package: shanselman.toasty version 0.6`
4. Fill in the PR template with relevant information

### 5. Wait for Automated Validation

The winget-pkgs repository has automated validation that will:
- Check manifest formatting
- Verify SHA256 hashes
- Validate URLs
- Run security scans

### 6. Address Any Feedback

Maintainers may request changes. Address them and push updates to your PR branch.

### 7. Merge and Publish

Once approved, maintainers will merge your PR and Toasty will be available via winget within 24 hours!

## Future Updates

For subsequent versions (e.g., v0.7), create a new directory `0.7/` with updated manifest files and submit another PR.

## Automated Tools

Microsoft provides tools to help with manifest creation:

- **wingetcreate**: https://github.com/microsoft/winget-create
  ```cmd
  wingetcreate new https://github.com/shanselman/toasty/releases/download/v0.6/toasty-x64.exe
  ```

- **YAMLCreate.ps1**: Available in the winget-pkgs repository
  ```powershell
  ./Tools/YAMLCreate.ps1
  ```

## Resources

- [Windows Package Manager Documentation](https://learn.microsoft.com/en-us/windows/package-manager/)
- [winget-pkgs Contribution Guidelines](https://github.com/microsoft/winget-pkgs/blob/master/CONTRIBUTING.md)
- [Manifest Schema Documentation](https://github.com/microsoft/winget-pkgs/tree/master/doc/manifest)
