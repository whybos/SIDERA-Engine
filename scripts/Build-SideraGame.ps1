<#
.SYNOPSIS
    Builds a standalone, distributable game package from a SIDERA game project
    (strictly separate from the SIDERA Engine distribution ZIP and Editor binaries).
.PARAMETER ProjectDir
    Path to the game project directory containing project.sideraproj.
.PARAMETER EngineBinDir
    Path to SIDERA Engine bin directory containing sidera.exe, vulkan-1.dll, and shaders/.
.PARAMETER OutputDir
    Target directory for the standalone distributable game build.
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [Parameter(Mandatory = $true)]
    [string]$EngineBinDir,
    [Parameter(Mandatory = $true)]
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path (Join-Path $ProjectDir "project.sideraproj"))) {
    throw "Invalid ProjectDir '$ProjectDir': project.sideraproj not found."
}
if (-not (Test-Path (Join-Path $EngineBinDir "sidera.exe"))) {
    throw "Invalid EngineBinDir '$EngineBinDir': sidera.exe not found."
}

Write-Host "[SIDERA Game Builder] Packaging standalone game from '$ProjectDir' -> '$OutputDir'..."
if (Test-Path $OutputDir) { Remove-Item -Path $OutputDir -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $OutputDir "shaders") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputDir "game") -Force | Out-Null

# Copy ONLY Runtime files (never Editor binaries or build intermediates)
Copy-Item -Path (Join-Path $EngineBinDir "sidera.exe") -Destination $OutputDir -Force
$vkDll = Join-Path $EngineBinDir "vulkan-1.dll"
if (Test-Path $vkDll) { Copy-Item -Path $vkDll -Destination $OutputDir -Force }
Copy-Item -Path (Join-Path $EngineBinDir "shaders\*.spv") -Destination (Join-Path $OutputDir "shaders") -Force

# Copy Game Project data
Copy-Item -Path (Join-Path $ProjectDir "*") -Destination (Join-Path $OutputDir "game") -Recurse -Force

# Write standalone game launcher script
$launcher = @"
param([switch]`$SmokeTest)
Push-Location `$PSScriptRoot
if (`$SmokeTest) {
    .\sidera.exe --smoke-test --render-path forward_plus
} else {
    .\sidera.exe --render-path forward_plus
}
`$code = `$LASTEXITCODE
Pop-Location
exit `$code
"@
Set-Content -Path (Join-Path $OutputDir "RunGame.ps1") -Value $launcher -Encoding UTF8

$shaderCount = (Get-ChildItem -Path (Join-Path $OutputDir "shaders") -Filter "*.spv").Count
Write-Host "[SIDERA Game Builder] Standalone game package ready ($shaderCount shaders, zero Editor/build files)."
