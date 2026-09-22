# BodianSMTCPlugin 卸载脚本
# 用法：.\scripts\uninstall.ps1 [-TargetDir "<安装目录>"]
# 目录探测逻辑见 scripts\common.ps1
[CmdletBinding()]
param(
    [string]$TargetDir = ""
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# 必须在结束进程之前完成探测，否则"运行中进程"这条途径就失效了
. "$scriptDir\common.ps1"
$TargetDir = Resolve-BodianInstallDir -TargetDir $TargetDir

# 1. 结束正在运行的客户端，否则 DLL 被占用无法还原
$proc = Get-Process "bodian_pc" -ErrorAction SilentlyContinue
if ($proc) {
    Write-Warning "Bodian Music (bodian_pc.exe) is currently running!"
    Write-Host "Closing bodian_pc..." -ForegroundColor Yellow
    Stop-Process -Name "bodian_pc" -Force
    Start-Sleep -Seconds 1
}

$targetDll = "$TargetDir\media_key_detector_windows_plugin.dll"
$origDll = "$TargetDir\media_key_detector_windows_plugin_orig.dll"

# 2. 还原原版 DLL
if (Test-Path $origDll) {
    Write-Host "Restoring original DLL..." -ForegroundColor Cyan
    Remove-Item -Path $targetDll -Force -ErrorAction SilentlyContinue
    Move-Item -Path $origDll -Destination $targetDll -Force
    Write-Host "`nSUCCESS! Original plugin restored. BodianSMTCPlugin uninstalled." -ForegroundColor Green
} else {
    Write-Warning "Original backup DLL ($origDll) not found. Nothing restored."
}
