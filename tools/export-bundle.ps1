param(
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $projectRoot "dist\ESP32-HanDictionary.bundle"
}

$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
git -C $projectRoot bundle create $OutputPath --all
if ($LASTEXITCODE -ne 0) {
    throw "Git bundle 导出失败，退出码：$LASTEXITCODE"
}

Write-Host "已生成可迁移仓库：$OutputPath" -ForegroundColor Green
Write-Host "在另一台电脑执行：git clone '$OutputPath' ESP32-HanDictionary"
