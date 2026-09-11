param([string]$OutputPath = "", [string]$ContentPath = "", [switch]$Graphics)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "setup.ps1")
if (-not $OutputPath) {
    $OutputPath = Join-Path $PWD $(if ($Graphics) { 'docs/ui/graphics/lvgl' } else { 'docs/ui/rendered' })
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++ for the optional UI simulator.' }
$vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ build tools not found.' }
& (Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
cmake -S tools/ui-simulator -B build/ui-simulator -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw 'UI simulator configure failed' }
$target = if ($Graphics) { 'han_graphics_smoke' } else { 'han_ui_sim' }
cmake --build build/ui-simulator --target $target --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'UI simulator build failed' }
if ($Graphics) { & .\build\ui-simulator\han_graphics_smoke.exe $OutputPath 'content/sdcard/handict/ui/graphics' }
elseif ($ContentPath) { & .\build\ui-simulator\han_ui_sim.exe $OutputPath $ContentPath }
else { & .\build\ui-simulator\han_ui_sim.exe $OutputPath }
if ($LASTEXITCODE -ne 0) { throw 'UI simulator tests failed' }
if (Test-Path tools/ui-assets/node_modules/sharp) {
    node tools/ui-simulator/render.cjs $OutputPath
    if ($LASTEXITCODE -ne 0) { throw 'PNG render failed' }
} else {
    Write-Host 'PPM screenshots generated. For PNG run npm ci --prefix tools/ui-assets, then node tools/ui-simulator/render.cjs <output>.'
}
