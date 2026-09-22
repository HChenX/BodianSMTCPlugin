<#
    把 MSVC、Windows SDK、CMake、Ninja 注入当前进程的 PATH / INCLUDE / LIB。
    不修改系统环境变量；由 build.ps1 点源调用。

    工具链定位优先级（命中即止）：
      1. -MsvcRoot 参数
      2. 本目录下的 toolchain.local.ps1（已被 .gitignore 排除）
      3. 环境变量 BODIAN_MSVC_ROOT
      4. vswhere 定位已安装的 Visual Studio / Build Tools，导入 vcvars64.bat
      5. 常见标准安装路径

    1/2/3/5 视为"便携式工具链根目录"，其下需有：
      VC\Tools\MSVC\<版本>\
      Windows Kits\10\{Include,Lib,bin}\<版本>\
    MSVC 与 SDK 版本号自动取最新，无需手工维护。

    用法：
      .\build.ps1 -MsvcRoot "<根目录>" -CMakeBin "<含 cmake.exe 和 ninja.exe 的目录>"
      $env:BODIAN_MSVC_ROOT = "<根目录>"    # 或写进 toolchain.local.ps1
#>
[CmdletBinding()]
param(
    [string]$MsvcRoot = "",
    [string]$CMakeBin = ""
)

# 本脚本被点源，运行在调用者作用域内，故不碰 $ErrorActionPreference：
# 否则原生程序的 stderr（如 CMake Warning）会在调用方被算作终止错误。
# 失败一律用 throw，它不受 ErrorActionPreference 影响。

# 取目录下版本号最大的子目录。按 System.Version 比较，避免字符串排序把 14.9 判为大于 14.51。
function Get-LatestVersionDir {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return $null }

    $found = @()
    foreach ($dir in (Get-ChildItem -LiteralPath $Path -Directory -ErrorAction SilentlyContinue)) {
        $ver = $null
        if ([version]::TryParse($dir.Name.TrimEnd('\'), [ref]$ver)) {
            $found += [pscustomobject]@{ Path = $dir.FullName; Version = $ver }
        }
    }
    if ($found.Count -eq 0) { return $null }

    return ($found | Sort-Object Version -Descending | Select-Object -First 1).Path
}

# 回读 vcvars64.bat 设置的环境变量并导入，省得重写微软那套目录推导。
function Import-VcVarsEnvironment {
    param([Parameter(Mandatory)][string]$VcVarsPath)

    $output = cmd.exe /c "`"$VcVarsPath`" >nul 2>&1 && set"
    $imported = 0
    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path ("env:" + $matches[1]) -Value $matches[2] -ErrorAction SilentlyContinue
            $imported++
        }
    }
    return ($imported -gt 0)
}

# 把目录前插进指定环境变量，跳过不存在的路径并去重（允许重复点源）。
function Add-ToEnvironmentPath {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Directories
    )

    $current = [System.Environment]::GetEnvironmentVariable($Name)
    $segments = @()

    foreach ($dir in $Directories) {
        if ([string]::IsNullOrWhiteSpace($dir)) { continue }
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        $full = $dir.TrimEnd('\')
        if ($segments -notcontains $full) { $segments += $full }
    }

    if ($current) {
        foreach ($existing in ($current -split ';')) {
            if ([string]::IsNullOrWhiteSpace($existing)) { continue }
            $full = $existing.TrimEnd('\')
            if ($segments -notcontains $full) { $segments += $full }
        }
    }

    [System.Environment]::SetEnvironmentVariable($Name, ($segments -join ';'))
}

# 本机覆盖文件优先于环境变量
$localOverride = Join-Path $PSScriptRoot 'toolchain.local.ps1'
if (Test-Path -LiteralPath $localOverride) {
    . $localOverride
}

$resolvedBy = ""
$portableRoot = ""
$usedVcVars = $false

if ($MsvcRoot) {
    if (-not (Test-Path -LiteralPath $MsvcRoot)) {
        throw "参数 -MsvcRoot 指向的目录不存在：$MsvcRoot"
    }
    $portableRoot = $MsvcRoot
    $resolvedBy = "参数 -MsvcRoot"
}

if (-not $portableRoot -and $env:BODIAN_MSVC_ROOT) {
    if (Test-Path -LiteralPath $env:BODIAN_MSVC_ROOT) {
        $portableRoot = $env:BODIAN_MSVC_ROOT
        $resolvedBy = "环境变量 BODIAN_MSVC_ROOT"
    } else {
        Write-Warning "环境变量 BODIAN_MSVC_ROOT 指向的目录不存在，已忽略：$($env:BODIAN_MSVC_ROOT)"
    }
}

if (-not $portableRoot) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vsInstall = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null | Select-Object -First 1
        if ($vsInstall) {
            $vcvars = Join-Path $vsInstall 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path -LiteralPath $vcvars) {
                $usedVcVars = Import-VcVarsEnvironment -VcVarsPath $vcvars
                if ($usedVcVars) { $resolvedBy = "vswhere 定位到 Visual Studio：$vsInstall" }
            }
        }
    }
}

if (-not $portableRoot -and -not $usedVcVars) {
    $standardRoots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\BuildTools'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Professional')
    )
    foreach ($root in $standardRoots) {
        if ([string]::IsNullOrWhiteSpace($root)) { continue }
        $vcvars = Join-Path $root 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path -LiteralPath $vcvars) {
            $usedVcVars = Import-VcVarsEnvironment -VcVarsPath $vcvars
            if ($usedVcVars) {
                $resolvedBy = "常见安装路径：$root"
                break
            }
        }
    }
}

if (-not $portableRoot -and -not $usedVcVars) {
    throw @"
未能定位 MSVC 工具链，请任选一种方式指定：

  1) .\build.ps1 -MsvcRoot "<工具链根目录>"
  2) 写 toolchain.local.ps1（已被 .gitignore 排除）：
       `$env:BODIAN_MSVC_ROOT = "<工具链根目录>"
       `$env:BODIAN_CMAKE_BIN = "<含 cmake.exe 和 ninja.exe 的目录>"
  3) 安装 Visual Studio 2022 或 Build Tools 的"使用 C++ 的桌面开发"工作负载
"@
}

# 便携式工具链：按目录约定自行推导
if ($portableRoot -and -not $usedVcVars) {
    $vcTools = Get-LatestVersionDir (Join-Path $portableRoot 'VC\Tools\MSVC')
    if (-not $vcTools) {
        throw "在 $portableRoot\VC\Tools\MSVC 下找不到 MSVC 版本目录，请确认 -MsvcRoot 指向的是工具链根目录。"
    }

    $sdkIncludeRoot = Join-Path $portableRoot 'Windows Kits\10\Include'
    $sdkLibRoot     = Join-Path $portableRoot 'Windows Kits\10\Lib'
    $sdkBinRoot     = Join-Path $portableRoot 'Windows Kits\10\bin'

    $sdkInclude = Get-LatestVersionDir $sdkIncludeRoot
    $sdkLib     = Get-LatestVersionDir $sdkLibRoot
    $sdkBin     = Get-LatestVersionDir $sdkBinRoot

    if (-not $sdkInclude -or -not $sdkLib) {
        throw "在 $portableRoot\Windows Kits\10 下找不到 Windows SDK 的 Include/Lib 版本目录，请确认工具链完整。"
    }

    $env:VCToolsInstallDir  = "$vcTools\"
    $env:VCToolsVersion     = Split-Path $vcTools -Leaf
    $env:WindowsSdkBinPath  = "$sdkBinRoot\"
    if ($sdkInclude) { $env:WindowsSDKVersion = (Split-Path $sdkInclude -Leaf) + '\' }

    Add-ToEnvironmentPath -Name 'PATH' -Directories @(
        (Join-Path $vcTools 'bin\Hostx64\x64'),
        (Join-Path $sdkBin 'x64'),
        (Join-Path $sdkBin 'x64\ucrt')
    )

    Add-ToEnvironmentPath -Name 'INCLUDE' -Directories @(
        (Join-Path $vcTools 'include'),
        (Join-Path $sdkInclude 'ucrt'),
        (Join-Path $sdkInclude 'shared'),
        (Join-Path $sdkInclude 'um'),
        (Join-Path $sdkInclude 'winrt'),
        (Join-Path $sdkInclude 'cppwinrt')
    )

    Add-ToEnvironmentPath -Name 'LIB' -Directories @(
        (Join-Path $vcTools 'lib\x64'),
        (Join-Path $sdkLib 'ucrt\x64'),
        (Join-Path $sdkLib 'um\x64')
    )
}

# CMake / Ninja
if ($CMakeBin) {
    if (-not (Test-Path -LiteralPath $CMakeBin)) {
        throw "参数 -CMakeBin 指向的目录不存在：$CMakeBin"
    }
    Add-ToEnvironmentPath -Name 'PATH' -Directories @($CMakeBin)
} elseif ($env:BODIAN_CMAKE_BIN) {
    if (Test-Path -LiteralPath $env:BODIAN_CMAKE_BIN) {
        Add-ToEnvironmentPath -Name 'PATH' -Directories @($env:BODIAN_CMAKE_BIN)
    } else {
        Write-Warning "环境变量 BODIAN_CMAKE_BIN 指向的目录不存在，已忽略：$($env:BODIAN_CMAKE_BIN)"
    }
}

if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    Add-ToEnvironmentPath -Name 'PATH' -Directories @(
        (Join-Path $env:ProgramFiles 'CMake\bin'),
        (Join-Path ${env:ProgramFiles(x86)} 'CMake\bin'),
        (Join-Path $env:LOCALAPPDATA 'Programs\CMake\bin')
    )
}

$cmakeCmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmakeCmd) {
    throw "未找到 cmake.exe。请用 -CMakeBin 或 `$env:BODIAN_CMAKE_BIN 指定其所在目录，或安装 CMake 并加入系统 PATH。"
}

$ninjaCmd = Get-Command ninja.exe -ErrorAction SilentlyContinue
if (-not $ninjaCmd) {
    throw "已找到 cmake（$($cmakeCmd.Source)）但未找到 ninja.exe。请用 -CMakeBin 指向同时含两者的目录，或单独安装 Ninja。"
}

$clCmd = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $clCmd) {
    throw "环境已设置但 cl.exe 仍不可用，工具链路径可能不完整。"
}

Write-Host "MSVC + Windows SDK + CMake + Ninja 环境已就绪（来源：$resolvedBy）" -ForegroundColor Green
Write-Host "  cl.exe    : $($clCmd.Source)" -ForegroundColor DarkGray
Write-Host "  cmake.exe : $($cmakeCmd.Source)  [$(cmake --version | Select-Object -First 1)]" -ForegroundColor DarkGray
Write-Host "  ninja.exe : $($ninjaCmd.Source)" -ForegroundColor DarkGray
