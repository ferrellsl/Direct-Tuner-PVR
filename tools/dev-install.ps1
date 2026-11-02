# Copies the built add-on into Kodi's add-ons folder, for testing. Kodi must be closed.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (Get-Process kodi -ErrorAction SilentlyContinue) { throw 'Close Kodi first' }
$addon = Join-Path $env:APPDATA 'Kodi\addons\pvr.directtuner'
Remove-Item $addon -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $addon | Out-Null
Copy-Item "$root\pvr.directtuner\resources" $addon -Recurse
Copy-Item "$root\pvr.directtuner\icon.png" $addon
Copy-Item "$root\build\addon.xml" $addon
Copy-Item "$root\build\pvr.directtuner.dll" $addon
Copy-Item "$root\LICENSE.md" $addon
"Installed to $addon"
