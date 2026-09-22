# 波点音乐安装目录探测，被 install.ps1 / uninstall.ps1 共用。不含任何硬编码路径。
#
# 优先级：
#   1. 调用方传入的 -TargetDir
#   2. 注册表卸载项中 DisplayName 匹配「波点 / bodian」的条目
#      （先取 InstallLocation，为空则从 DisplayIcon 反推）
#   3. 正在运行的 bodian_pc.exe 进程所在目录

# 从 DisplayIcon 反推目录。该值形如 "D:\a\app.exe,0"，需先剥掉尾部图标索引与引号。
# 解析失败返回 $null，由调用方继续尝试下一条途径。
function Get-DirectoryFromDisplayIcon {
    param([string]$DisplayIcon)

    if ([string]::IsNullOrWhiteSpace($DisplayIcon)) { return $null }

    $candidate = $DisplayIcon -replace ',\s*-?\d+\s*$', ''
    $candidate = $candidate.Trim().Trim('"')

    if (-not (Test-Path -LiteralPath $candidate)) { return $null }
    # 不用 Split-Path -LiteralPath -Parent：它在 PowerShell 5.1 下会因参数集歧义报错
    $dir = [System.IO.Path]::GetDirectoryName($candidate)
    if ([string]::IsNullOrWhiteSpace($dir)) { return $null }
    return $dir
}

# 返回波点音乐安装目录的绝对路径；所有途径都失败时抛出带指引的异常。
function Resolve-BodianInstallDir {
    [CmdletBinding()]
    param([string]$TargetDir = "")

    $exeName = 'bodian_pc.exe'

    if (-not [string]::IsNullOrWhiteSpace($TargetDir)) {
        if (-not (Test-Path -LiteralPath $TargetDir)) {
            throw "参数 -TargetDir 指向的目录不存在：$TargetDir"
        }
        $resolved = (Resolve-Path -LiteralPath $TargetDir).Path
        if (-not (Test-Path -LiteralPath (Join-Path $resolved $exeName))) {
            throw "目录 $resolved 下没有 $exeName，请确认这是波点音乐的安装目录。"
        }
        Write-Host "使用 -TargetDir 指定的目录：$resolved" -ForegroundColor Gray
        return $resolved
    }

    $uninstallKeys = @(
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    foreach ($key in $uninstallKeys) {
        $entries = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -match '波点|bodian' }
        foreach ($entry in $entries) {
            foreach ($field in @($entry.InstallLocation, (Get-DirectoryFromDisplayIcon $entry.DisplayIcon))) {
                if ([string]::IsNullOrWhiteSpace($field)) { continue }
                if (-not (Test-Path -LiteralPath $field)) { continue }
                if (Test-Path -LiteralPath (Join-Path $field $exeName)) {
                    $resolved = (Resolve-Path -LiteralPath $field).Path
                    Write-Host "从注册表卸载项定位到安装目录：$resolved" -ForegroundColor Gray
                    return $resolved
                }
            }
        }
    }

    # 便携式安装通常不进注册表，但客户端在跑时能直接问出路径
    $running = Get-CimInstance Win32_Process -Filter "Name='$exeName'" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($running -and $running.ExecutablePath) {
        $dir = [System.IO.Path]::GetDirectoryName($running.ExecutablePath)
        if (-not [string]::IsNullOrWhiteSpace($dir) -and (Test-Path -LiteralPath $dir)) {
            Write-Host "从运行中的 $exeName 进程定位到安装目录：$dir" -ForegroundColor Gray
            return $dir
        }
    }

    throw @"
未能自动定位波点音乐安装目录，请显式指定：
    .\scripts\install.ps1 -TargetDir "<安装目录>"
    .\scripts\uninstall.ps1 -TargetDir "<安装目录>"
该目录下应有 bodian_pc.exe。自动探测失败通常是因为客户端为便携安装且当前未运行。
"@
}
