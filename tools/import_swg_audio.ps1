# Extracts the Galaxies music and ambience the open world plays
# (game/audio/swg_audio_files.txt) from your Galaxies archives into
# game/imported_assets/audio/swg/, where game/audio/world_audio.gd finds them.
# Game data from your own install: it's gitignored, never committed.
#
#   tools\import_swg_audio.ps1 -Archives E:\swg_te\extracted\Star_Wars_files
#
# -Archives is the directory of .tre archives swgtool reads (unencrypted ones;
# never Restoration's). -Swgtool defaults to the Release build.
param(
    [Parameter(Mandatory = $true)][string]$Archives,
    [string]$Swgtool = ""
)
$ErrorActionPreference = "Stop"
if ($Swgtool -eq "") {
    # (Windows PowerShell has no $PSScriptRoot in parameter defaults.)
    $Swgtool = Join-Path $PSScriptRoot "..\build\msvc\tools\Release\swgtool.exe"
}

if (-not (Test-Path $Swgtool)) {
    throw "No swgtool at $Swgtool. Build it first: cmake --build build\msvc --config Release --target swgtool"
}
$list = Join-Path $PSScriptRoot "..\game\audio\swg_audio_files.txt"
$out = Join-Path $PSScriptRoot "..\game\imported_assets\audio\swg"
New-Item -ItemType Directory -Force $out | Out-Null

$done = 0
$failed = @()
foreach ($line in Get-Content $list) {
    $path = $line.Trim()
    if ($path -eq "" -or $path.StartsWith("#")) { continue }
    $target = Join-Path $out (Split-Path $path -Leaf)
    & $Swgtool extract $Archives $path $target
    if ($LASTEXITCODE -eq 0 -and (Test-Path $target)) {
        $done++
    } else {
        $failed += $path
    }
}
Write-Output "extracted $done file(s) to $out"
if ($failed.Count -gt 0) {
    Write-Output "not found: $($failed -join ', ')"
    exit 1
}
