param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$destination = [System.IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $destination) { throw 'Choose a new output directory; existing packages are preserved.' }
$argsFile = Join-Path $build 'flasher_args.json'
$flash = Get-Content -Raw -LiteralPath $argsFile | ConvertFrom-Json
foreach ($file in $flash.flash_files.PSObject.Properties.Value) {
    if ([System.IO.Path]::IsPathRooted($file) -or $file -match '\.\.') { throw 'Unexpected flash file path' }
    if (-not (Test-Path -LiteralPath (Join-Path $build $file))) { throw "Missing build file: $file" }
}
New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($file in $flash.flash_files.PSObject.Properties.Value) {
    $target = Join-Path $destination $file
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath (Join-Path $build $file) -Destination $target
}
Copy-Item -LiteralPath $argsFile -Destination $destination
Copy-Item -LiteralPath (Join-Path $build 'flash_args') -Destination $destination
Copy-Item -LiteralPath (Join-Path $root 'sdkconfig') -Destination $destination
Copy-Item -LiteralPath (Join-Path $root 'assets/licenses') -Destination (Join-Path $destination 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'assets/README.md') -Destination (Join-Path $destination 'ASSET-NOTICES.md')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $destination 'FIRMWARE-LICENSE.txt')
Write-Host "Firmware package: $destination"
