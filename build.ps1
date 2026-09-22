# BodianSMTCPlugin 构建脚本
# 用法：.\build.ps1 [-MsvcRoot "<路径>"] [-CMakeBin "<路径>"]
# 工具链探测逻辑在 init_env.ps1，本脚本只透传参数。
[CmdletBinding()]
param(
    [string]$MsvcRoot = "",
    [string]$CMakeBin = ""
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

. "$scriptDir\init_env.ps1" -MsvcRoot $MsvcRoot -CMakeBin $CMakeBin

# MSVC 对树外头文件按绝对路径写入 __FILE__，工具链装在自定义位置时会把本机目录
# 编进 DLL。此处按工具链实际位置裁剪，使发布产物不泄漏构建机路径。
$cxxFlags = ""
if ($env:WindowsSdkBinPath) {
    $sdkBin = $env:WindowsSdkBinPath.TrimEnd('\')
    $toolchainRoot = Split-Path (Split-Path (Split-Path $sdkBin -Parent) -Parent) -Parent
    if ($toolchainRoot -and (Test-Path -LiteralPath $toolchainRoot)) {
        $cxxFlags = "/d1trimfile:$toolchainRoot"
    }
}

$buildDir = "$scriptDir\build"
if (!(Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}

Write-Host "Configuring CMake project..." -ForegroundColor Cyan
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="cl.exe" -DCMAKE_CXX_COMPILER="cl.exe" "-DCMAKE_CXX_FLAGS=$cxxFlags"

Write-Host "Building project with Ninja..." -ForegroundColor Cyan
cmake --build build --config Release

$outputDll = "$buildDir\media_key_detector_windows_plugin.dll"
if (Test-Path $outputDll) {
    $size = (Get-Item $outputDll).Length
    Write-Host "`nSUCCESS! Built: $outputDll ($size bytes)" -ForegroundColor Green
} else {
    Write-Error "Build failed: $outputDll not found."
}
