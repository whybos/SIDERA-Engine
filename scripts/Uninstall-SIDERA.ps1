<#
.SYNOPSIS
    Cleanly uninstalls SIDERA Engine 1.0.0-rc1 from the specified installation directory.
#>
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\SIDERA_Engine_1_0_rc1"
)

$ErrorActionPreference = "Stop"

if (Test-Path $InstallDir) {
    Remove-Item -Path $InstallDir -Recurse -Force
    Write-Host "[SIDERA Uninstaller] Removed '$InstallDir'."
} else {
    Write-Host "[SIDERA Uninstaller] Directory '$InstallDir' does not exist."
}

$startMenuDir = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\SIDERA Engine 1.0"
if (Test-Path $startMenuDir) {
    Remove-Item -Path $startMenuDir -Recurse -Force
    Write-Host "[SIDERA Uninstaller] Removed Start Menu shortcuts."
}

Write-Host "[SIDERA Uninstaller] Uninstall complete."
