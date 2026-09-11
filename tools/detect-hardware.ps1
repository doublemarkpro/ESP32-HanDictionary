param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$IdfPath = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "setup.ps1") -IdfPath $IdfPath

$output = (& esptool.py --chip esp32p4 --port $Port chip_id 2>&1) -join "`n"
Write-Host $output

if ($output -match "revision v([0-9]+)(?:\.([0-9]+))?") {
    $major = [int]$Matches[1]
    if ($major -ge 3) {
        Write-Host "建议构建参数：-HardwareRevision P4X" -ForegroundColor Green
    } else {
        Write-Host "建议构建参数：-HardwareRevision Legacy" -ForegroundColor Green
    }
} else {
    Write-Warning "没有从输出中识别出芯片修订号。请保留以上完整输出供人工确认。"
}
