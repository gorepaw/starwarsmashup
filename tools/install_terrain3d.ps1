# Installs a locally built Terrain3D addon into game/addons/terrain_3d (gitignored).
#
#   tools\install_terrain3d.ps1 -Source E:\godot\terrain3d-1.1.0
#
# Build Terrain3D first, in its own checkout, against our double-precision API dump
# (see docs/BUILDING.md). Like swm.gdextension, the installed .gdextension lists only
# double-precision libraries, so a single-precision editor refuses to load it.
param(
    [Parameter(Mandatory = $true)][string]$Source
)
$ErrorActionPreference = "Stop"

$addon = Join-Path $Source "project\addons\terrain_3d"
$dll = Join-Path $addon "bin\libterrain.windows.debug.x86_64.dll"
if (-not (Test-Path $dll)) {
    throw "No debug library at $dll. Build Terrain3D first."
}
$target = Join-Path $PSScriptRoot "..\game\addons\terrain_3d"
if (Test-Path $target) {
    Remove-Item -Recurse -Force $target
}
New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
Copy-Item -Recurse $addon $target
Get-ChildItem (Join-Path $target "bin") -Include *.exp, *.lib, *.pdb -Recurse | Remove-Item

$gdextension = (Resolve-Path (Join-Path $target "terrain.gdextension")).Path
$text = [IO.File]::ReadAllText($gdextension)
$libraries = @"
[libraries]

; Double-precision builds only (installed by tools/install_terrain3d.ps1).
windows.debug.double.x86_64 = "res://addons/terrain_3d/bin/libterrain.windows.debug.x86_64.dll"
windows.release.double.x86_64 = "res://addons/terrain_3d/bin/libterrain.windows.release.x86_64.dll"
"@
$text = $text -replace "(?s)\[libraries\].*", $libraries
[IO.File]::WriteAllText($gdextension, $text + "`n")
Write-Output "installed Terrain3D into $((Resolve-Path $target).Path)"
