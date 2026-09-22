# BodianSMTCPlugin 安装脚本
# 用法：.\scripts\install.ps1 [-TargetDir "<安装目录>"]
# 目录探测逻辑见 scripts\common.ps1
[CmdletBinding()]
param(
    [string]$TargetDir = ""
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir
$builtDll = "$projectRoot\build\media_key_detector_windows_plugin.dll"

if (!(Test-Path $builtDll)) {
    Write-Error "Build output not found at $builtDll. Please run build.ps1 first!"
}

. "$scriptDir\common.ps1"
$TargetDir = Resolve-BodianInstallDir -TargetDir $TargetDir

# 1. 结束正在运行的客户端，否则 DLL 被占用无法替换
$proc = Get-Process "bodian_pc" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Warning "Bodian Music (bodian_pc.exe) is currently running!"
    Write-Host "Attempting to close bodian_pc gracefully..." -ForegroundColor Yellow
    $proc.CloseMainWindow() | Out-Null
    Start-Sleep -Seconds 2
    $proc = Get-Process "bodian_pc" -ErrorAction SilentlyContinue
    if ($proc) {
        Write-Host "Force terminating bodian_pc to unlock DLL..." -ForegroundColor Yellow
        Stop-Process -Name "bodian_pc" -Force
        Start-Sleep -Seconds 1
    }
}

$targetDll = "$TargetDir\media_key_detector_windows_plugin.dll"
$origDll = "$TargetDir\media_key_detector_windows_plugin_orig.dll"

# 2. 备份原版 DLL：只备份一次，已存在则保留，避免把代理 DLL 误当原版备份
if (!(Test-Path $origDll)) {
    if (Test-Path $targetDll) {
        Write-Host "Backing up original DLL to: $origDll" -ForegroundColor Cyan
        Move-Item -Path $targetDll -Destination $origDll -Force
    } else {
        Write-Error "Original DLL not found at $targetDll!"
    }
} else {
    Write-Host "Original backup already exists at $origDll, preserving." -ForegroundColor Gray
}

# 3. 部署代理 DLL
Write-Host "Deploying BodianSMTCPlugin proxy DLL..." -ForegroundColor Cyan
Copy-Item -Path $builtDll -Destination $targetDll -Force

Write-Host "`nSUCCESS! BodianSMTCPlugin has been installed to $TargetDir." -ForegroundColor Green
Write-Host "You can now launch Bodian Music and enjoy Windows SMTC & AF Media Bar integration!" -ForegroundColor Green
