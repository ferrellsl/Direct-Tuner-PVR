# Builds the add-on, and with -Package makes the installable zip in dist\.
# Set KODI_INCLUDE_DIR to xbmc/addons/kodi-dev-kit/include in a Kodi 21 source tree.
param([switch]$Package, [string]$KodiInclude = $env:KODI_INCLUDE_DIR)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vcvars = Join-Path (& $vswhere -latest -products * -property installationPath) 'VC\Auxiliary\Build\vcvars64.bat'
$build = Join-Path $root 'build'
cmd /c "`"$vcvars`" >nul && cmake -S `"$root`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `"-DKODI_INCLUDE_DIR=$KodiInclude`" && cmake --build `"$build`""
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not $Package) { return }

$version = (Select-String -Path "$root\CMakeLists.txt" -Pattern 'VERSION (\d+\.\d+\.\d+)\)').Matches[0].Groups[1].Value
$stage = Join-Path $root 'dist\stage'
$addon = Join-Path $stage 'pvr.directtuner'
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $addon | Out-Null
Copy-Item "$root\pvr.directtuner\resources" $addon -Recurse
Copy-Item "$root\pvr.directtuner\icon.png" $addon
Copy-Item "$build\addon.xml" $addon
Copy-Item "$build\pvr.directtuner.dll" $addon
Copy-Item "$root\LICENSE.md" $addon
$zip = Join-Path $root "dist\pvr.directtuner-$version.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
# Kodi's zip reader wants forward slashes, which Compress-Archive doesn't always write.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, 'Create')
foreach ($file in Get-ChildItem $stage -Recurse -File) {
  $name = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
  [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $name) | Out-Null
}
$archive.Dispose()
Remove-Item $stage -Recurse -Force
"Built $zip"
