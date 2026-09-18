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
# WakeNet9l improves recall for quickly spoken wake phrases. This matters for the children's
# dictionary and costs only a small model-size increase on the ESP32-P4/PSRAM target.
python .\scripts\build.py m5stack/tab5 --name $variant --language zh-CN --wake-word wn9l_nihaoxiaozhi_tts3
if ($LASTEXITCODE -ne 0) {
    throw "构建失败，退出码：$LASTEXITCODE"
}

python .\tools\capacity_report.py
if ($LASTEXITCODE -ne 0) {
    throw "容量检查失败：修复分区溢出或应用安全余量后再打包。"
}

Write-Host "构建完成：$variant" -ForegroundColor Green
Write-Host "固件目录：$PWD\build"
