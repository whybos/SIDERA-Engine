<#
.SYNOPSIS
    Installs SIDERA Engine 1.0.0-rc1 from a distribution package or build directory to a clean target folder.
.PARAMETER SourceDir
    Path to the unpacked distribution directory (or CMake install prefix).
.PARAMETER InstallDir
    Target installation directory (default: $env:LOCALAPPDATA\SIDERA_Engine_1_0_rc1).
.PARAMETER CreateStartMenuShortcut
    Creates a Start Menu shortcut for SIDERA Runtime.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,
    [string]$InstallDir = "$env:LOCALAPPDATA\SIDERA_Engine_1_0_rc1",
    [switch]$CreateStartMenuShortcut
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $SourceDir "bin\sidera.exe"))) {
    throw "Invalid SourceDir '$SourceDir': bin\sidera.exe not found."
}

Write-Host "[SIDERA Installer] Installing SIDERA Engine 1.0.0-rc1 to '$InstallDir'..."
if (Test-Path $InstallDir) {
    Remove-Item -Path $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
Copy-Item -Path (Join-Path $SourceDir "*") -Destination $InstallDir -Recurse -Force

# Verify essential files
$required = @(
    "bin\sidera.exe",
    "bin\sidera_editor.exe",
    "bin\vulkan-1.dll",
    "bin\shaders\triangle.vert.spv",
    "examples\rpg_reference_project\project.sideraproj",
    "docs\THIRD_PARTY_LICENSES.md"
)
foreach ($rel in $required) {
    $full = Join-Path $InstallDir $rel
    if (-not (Test-Path $full)) {
        throw "Installation verification failed: missing '$rel'"
    }
}

$shaderCount = (Get-ChildItem -Path (Join-Path $InstallDir "bin\shaders") -Filter "*.spv").Count
Write-Host "[SIDERA Installer] Verified $shaderCount compiled SPIR-V shaders and required runtime binaries."

if ($CreateStartMenuShortcut) {
    $startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\SIDERA Engine 1.0"
    New-Item -ItemType Directory -Path $startMenuDir -Force | Out-Null
    $wsh = New-Object -ComObject WScript.Shell
    $sc = $wsh.CreateShortcut((Join-Path $startMenuDir "SIDERA Engine 1.0.lnk"))
    $sc.TargetPath = Join-Path $InstallDir "bin\sidera.exe"
    $sc.WorkingDirectory = Join-Path $InstallDir "bin"
    $sc.Save()
    Write-Host "[SIDERA Installer] Created Start Menu shortcut in '$startMenuDir'."
}

Write-Host "[SIDERA Installer] Installation complete."
