param(
    [string]$IdfPath = ""
)

$ErrorActionPreference = "Stop"
$requiredVersion = "6.0.2"
$projectRoot = Split-Path -Parent $PSScriptRoot

function Find-EspIdfPath {
    param([string]$RequestedPath)

    $candidates = [System.Collections.Generic.List[string]]::new()
    if ($RequestedPath) {
        $candidates.Add($RequestedPath)
    }
    if ($env:IDF_PATH) {
        $candidates.Add($env:IDF_PATH)
    }

    $eimConfig = "C:\Espressif\tools\eim_idf.json"
    if (Test-Path -LiteralPath $eimConfig) {
        try {
            $eim = Get-Content -Raw -LiteralPath $eimConfig | ConvertFrom-Json
            $selected = $eim.idfInstalled | Where-Object { $_.id -eq $eim.idfSelectedId } | Select-Object -First 1
            if ($selected.path) {
                $candidates.Add([string]$selected.path)
            }
            foreach ($installation in $eim.idfInstalled) {
                if ($installation.path) {
                    $candidates.Add([string]$installation.path)
                }
            }
        } catch {
            Write-Warning "无法读取 Espressif Installation Manager 配置：$($_.Exception.Message)"
        }
    }

    $candidates.Add("D:\esp\v$requiredVersion\esp-idf")
    $candidates.Add("C:\esp\v$requiredVersion\esp-idf")
    $candidates.Add("C:\Espressif\frameworks\esp-idf-v$requiredVersion")

    foreach ($candidate in $candidates | Select-Object -Unique) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate "export.ps1"))) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

$resolvedIdfPath = Find-EspIdfPath -RequestedPath $IdfPath
if (-not $resolvedIdfPath) {
    throw @"
未找到 ESP-IDF $requiredVersion。
请先用 Espressif Installation Manager 安装 ESP-IDF $requiredVersion，随后重新运行：
  . .\tools\setup.ps1
或者显式指定：
  . .\tools\setup.ps1 -IdfPath 'D:\esp\v$requiredVersion\esp-idf'
"@
}

# Espressif Installation Manager may keep the framework and tools on
# different drives and uses its own virtualenv layout. Prefer the activation
# script recorded by EIM; standard installations continue to use export.ps1.
$activationScript = $null
$eimConfig = "C:\Espressif\tools\eim_idf.json"
if (Test-Path -LiteralPath $eimConfig) {
    try {
        $eim = Get-Content -Raw -LiteralPath $eimConfig | ConvertFrom-Json
        $matchingInstallation = $eim.idfInstalled |
            Where-Object {
                $_.path -and
                ((Resolve-Path -LiteralPath $_.path -ErrorAction SilentlyContinue).Path -eq $resolvedIdfPath)
            } |
            Select-Object -First 1
        if ($matchingInstallation.idfToolsPath) {
            $env:IDF_TOOLS_PATH = [string]$matchingInstallation.idfToolsPath
        }
        if ($matchingInstallation.activationScript -and
            (Test-Path -LiteralPath $matchingInstallation.activationScript)) {
            $activationScript = [string]$matchingInstallation.activationScript
        }
    } catch {
        Write-Warning "无法匹配 EIM 工具目录，将使用 ESP-IDF 默认设置：$($_.Exception.Message)"
    }
}

if ($activationScript) {
    . $activationScript
} else {
    . (Join-Path $resolvedIdfPath "export.ps1")
}

# scripts/build.py normally launches idf.py by name. EIM installs a dotted
# idf.py.exe shim that PowerShell resolves but Python's CreateProcess may not.
# Tell the build script to invoke IDF's Python entry point explicitly.
$env:XIAOZHI_IDF_PYTHON = (Get-Command python -ErrorAction Stop).Source

$versionText = (& idf.py --version) -join " "
if ($versionText -notmatch [regex]::Escape($requiredVersion)) {
    throw "需要 ESP-IDF $requiredVersion，当前检测到：$versionText"
}

Set-Location -LiteralPath $projectRoot
Write-Host "Han Dictionary 开发环境已就绪" -ForegroundColor Green
Write-Host "Project : $projectRoot"
Write-Host "ESP-IDF : $versionText"
Write-Host "下一步  : .\tools\build.ps1"
