param(
    [ValidateSet("Legacy", "P4X")]
    [string]$HardwareRevision = "Legacy",
    [string]$IdfPath = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "setup.ps1") -IdfPath $IdfPath

$variant = if ($HardwareRevision -eq "P4X") {
    "m5stack-tab5-han-dictionary-p4x"
} else {
    "m5stack-tab5-han-dictionary"
}

Write-Host "正在构建 $variant ..." -ForegroundColor Cyan
python .\scripts\build.py m5stack/tab5 --name $variant --language zh-CN --wake-word nihaoxiaozhi
if ($LASTEXITCODE -ne 0) {
    throw "构建失败，退出码：$LASTEXITCODE"
}

Write-Host "构建完成：$variant" -ForegroundColor Green
Write-Host "固件目录：$PWD\build"
