<#
.SYNOPSIS
    Cleanly uninstalls SIDERA Engine 1.0.0 without deleting user-created projects or saved games.
#>
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\SIDERA_Engine_1_0_0"
)

$ErrorActionPreference = "Stop"

if (Test-Path $InstallDir) {
    Remove-Item -Path $InstallDir -Recurse -Force
    Write-Host "[SIDERA Uninstaller] Removed engine installation '$InstallDir'."
} else {
    Write-Host "[SIDERA Uninstaller] Directory '$InstallDir' does not exist."
}

$startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\SIDERA Engine 1.0.0"
if (Test-Path $startMenuDir) {
    Remove-Item -Path $startMenuDir -Recurse -Force
    Write-Host "[SIDERA Uninstaller] Removed Start Menu shortcuts."
}

$desktopSc = Join-Path ([Environment]::GetFolderPath("Desktop")) "SIDERA Editor 1.0.0.lnk"
if (Test-Path $desktopSc) {
    Remove-Item -Path $desktopSc -Force
    Write-Host "[SIDERA Uninstaller] Removed Desktop shortcut."
}

Write-Host "[SIDERA Uninstaller] Uninstall complete. User projects and saves outside '$InstallDir' were preserved."
