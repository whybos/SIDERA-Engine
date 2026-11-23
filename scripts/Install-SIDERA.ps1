<#
.SYNOPSIS
    Installs SIDERA Engine 1.0.0 to a clean user-selected directory without modifying user projects.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,
    [string]$InstallDir = "$env:LOCALAPPDATA\SIDERA_Engine_1_0_0",
    [switch]$CreateStartMenuShortcut,
    [switch]$CreateDesktopShortcut
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $SourceDir "bin\sidera.exe"))) {
    throw "Invalid SourceDir '$SourceDir': bin\sidera.exe not found."
}

Write-Host "[SIDERA Installer] Installing SIDERA Engine 1.0.0 to '$InstallDir'..."
if (Test-Path $InstallDir) {
    Remove-Item -Path $InstallDir -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
Copy-Item -Path (Join-Path $SourceDir "*") -Destination $InstallDir -Recurse -Force

$required = @(
    "bin\sidera.exe",
    "bin\sidera_editor.exe",
    "bin\vulkan-1.dll",
    "bin\shaders\triangle.vert.spv",
    "bin\shaders\ui_quad.vert.spv",
    "bin\shaders\ui_quad.frag.spv",
    "examples\rpg_reference_project\project.sideraproj",
    "docs\THIRD_PARTY_LICENSES.md",
    "scripts\Build-SideraGame.ps1"
)
foreach ($rel in $required) {
    $full = Join-Path $InstallDir $rel
    if (-not (Test-Path $full)) {
        throw "Installation verification failed: missing '$rel'"
    }
}

$shaderCount = (Get-ChildItem -Path (Join-Path $InstallDir "bin\shaders") -Filter "*.spv").Count
Write-Host "[SIDERA Installer] Verified $shaderCount compiled SPIR-V shaders (including 2D Runtime UI shaders) and binaries."

$wsh = New-Object -ComObject WScript.Shell
if ($CreateStartMenuShortcut) {
    $startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\SIDERA Engine 1.0.0"
    New-Item -ItemType Directory -Path $startMenuDir -Force | Out-Null
    $scEd = $wsh.CreateShortcut((Join-Path $startMenuDir "SIDERA Editor 1.0.0.lnk"))
    $scEd.TargetPath = Join-Path $InstallDir "bin\sidera_editor.exe"
    $scEd.WorkingDirectory = Join-Path $InstallDir "bin"
    $scEd.Save()
    $scRt = $wsh.CreateShortcut((Join-Path $startMenuDir "SIDERA Runtime 1.0.0.lnk"))
    $scRt.TargetPath = Join-Path $InstallDir "bin\sidera.exe"
    $scRt.WorkingDirectory = Join-Path $InstallDir "bin"
    $scRt.Save()
    Write-Host "[SIDERA Installer] Created Start Menu shortcuts in '$startMenuDir'."
}

if ($CreateDesktopShortcut) {
    $desktop = [Environment]::GetFolderPath("Desktop")
    $sc = $wsh.CreateShortcut((Join-Path $desktop "SIDERA Editor 1.0.0.lnk"))
    $sc.TargetPath = Join-Path $InstallDir "bin\sidera_editor.exe"
    $sc.WorkingDirectory = Join-Path $InstallDir "bin"
    $sc.Save()
    Write-Host "[SIDERA Installer] Created Desktop shortcut for SIDERA Editor 1.0.0."
}

Write-Host "[SIDERA Installer] Installation complete. User projects remain untouched outside '$InstallDir'."
