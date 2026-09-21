<#
    AdoLoop 构建脚本公共库（M13）

    被 build.ps1 / package.ps1 / run.ps1 / clean.ps1 点源（dot-source）加载：
        . (Join-Path $PSScriptRoot 'common.ps1')

    设计要点（为什么不再需要 .bat）：
      * MSVC 环境用 Visual Studio 自带的 Launch-VsDevShell.ps1 在**当前 PowerShell 会话**内加载
        （它内部加载 Microsoft.VisualStudio.DevShell.dll 并调用 Enter-VsDevShell）。
        这与 "调用 vcvars64.bat" 等价，但全程是 PowerShell 脚本，无需 cmd.exe。
      * Ninja 优先用 VS 自带的 Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe，
        避免依赖外部的 Strawberry/MinGW ninja。
      * PATH 中的 MinGW/Strawberry/MSYS/Cygwin 目录被显式剔除，
        否则 CMake 可能先找到 gcc 而误选 GNU 工具链（与 Qt 的 MSVC 库 ABI 不兼容）。

    本文件只定义函数/常量，不产生副作用（可安全地重复点源）。
    兼容 Windows PowerShell 5.1 与 PowerShell 7+。
#>

Set-StrictMode -Version Latest

# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------

# 发行包内桥脚本的相对目录：src/core/Tokenizer.cpp 与 src/asr/FasterWhisperAsr.cpp
# 都在 <可执行目录>\scripts\ 下查找 Python 桥，因此打包时必须放到 scripts\ 子目录。
$script:AdoLoopBridgeDirName = 'scripts'

# 构建/打包只允许写这两个前缀的顶层目录（相对仓库根）：build / build-xxx / dist / dist\xxx
# 与 .gitignore 的 build/ build-*/ dist/ 对齐；由 Assert-AdoLoopWritableOutputPath 强制校验。
$script:AdoLoopAllowedOutputTopDirs = @('build', 'dist')

# clean.ps1 拒绝删除的仓库内目录名
$script:AdoLoopProtectedTopDirs = @(
    '.git', '.github', '.vscode', 'src', 'docs', 'scripts', 'tests', 'test', 'resources', 'assets'
)

# ---------------------------------------------------------------------------
# 输出（分级彩色；非交互宿主下颜色自动被忽略）
# ---------------------------------------------------------------------------

function Write-Step {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-SubStep {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "  -- $Message" -ForegroundColor Cyan
}

function Write-Info {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "     $Message" -ForegroundColor Gray
}

function Write-Ok {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "  [OK] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "  [警告] $Message" -ForegroundColor Yellow
}

function Write-Fail {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "  [失败] $Message" -ForegroundColor Red
}

# 键值对（用于结尾摘要）
function Write-Kv {
    param(
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value
    )
    Write-Host ("     {0,-12}{1}" -f ($Key + ':'), $Value) -ForegroundColor Gray
}

function Write-Detail {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Verbose $Message
}

# ---------------------------------------------------------------------------
# 通用小工具
# ---------------------------------------------------------------------------

# 相对路径 → 绝对路径（相对仓库根解析）；不做存在性检查
function Resolve-AdoLoopPath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$BaseDir
    )
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDir $Path))
}

# CMake 喜欢正斜杠；Windows API 也接受，但字符串更干净
function ConvertTo-CMakePath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return ($Path -replace '\\', '/')
}

# 路径等价比较（忽略大小写与斜杠方向；CMakeCache 里存的是正斜杠）
function Test-AdoLoopSamePath {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$A,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$B
    )
    if ([string]::IsNullOrWhiteSpace($A) -or [string]::IsNullOrWhiteSpace($B)) { return $false }
    $na = $A.Trim().TrimEnd('\', '/') -replace '/', '\'
    $nb = $B.Trim().TrimEnd('\', '/') -replace '/', '\'
    return [string]::Equals($na, $nb, [System.StringComparison]::OrdinalIgnoreCase)
}

# 从脚本所在目录向上找仓库根（以 CMakeLists.txt 为标记）
function Get-AdoLoopRepoRoot {
    param([string]$StartDir)

    $dir = $StartDir
    if ([string]::IsNullOrWhiteSpace($dir)) { $dir = (Get-Location).ProviderPath }

    $info = New-Object System.IO.DirectoryInfo ([System.IO.Path]::GetFullPath($dir))
    while ($null -ne $info) {
        if (Test-Path -LiteralPath (Join-Path $info.FullName 'CMakeLists.txt')) {
            return $info.FullName
        }
        $info = $info.Parent
    }
    throw "找不到仓库根目录（从 '$dir' 向上未发现 CMakeLists.txt）。"
}

# 体积格式化
function Format-AdoLoopSize {
    param([Parameter(Mandatory = $true)][long]$Bytes)
    if ($Bytes -ge 1GB) { return ("{0:N2} GB" -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ("{0:N2} MB" -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ("{0:N1} KB" -f ($Bytes / 1KB)) }
    return "$Bytes B"
}

# ---------------------------------------------------------------------------
# 运行外部命令：显式检查 $LASTEXITCODE（native 命令失败不会让 $ErrorActionPreference 生效）
# ---------------------------------------------------------------------------

function Invoke-AdoLoopNative {
    <#
        .DESCRIPTION
            执行外部程序并把输出实时打到宿主上；非零退出码 → 抛出异常（由调用方转成 exit 1）。
            注意：这里刻意**不重定向** stderr，避免 PS 5.1 在
            $ErrorActionPreference='Stop' 下把 native stderr 当成终止错误。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory = $true)][string]$What
    )

    $display = "$Exe $($Arguments -join ' ')"
    Write-Verbose "执行：$display"

    & $Exe @Arguments | Out-Host

    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw "$What 失败（退出码 $code）：`n        $display"
    }
    return $code
}

# ---------------------------------------------------------------------------
# 环境变量修复
#
# 坑：某些宿主（例如从 Git Bash / MSYS 派生的进程）会把变量名带括号或大小写异常的环境变量
#     弄坏或丢弃 —— ProgramFiles(x86) 会变成 "(x86)"，ProgramFiles 会整个消失。
#     这会让 VS 自带的 Launch-VsDevShell.ps1 找不到 vswhere.exe 而直接抛错。
#     这里用 .NET 的已知文件夹 API + 注册表来恢复，属于幂等操作。
# ---------------------------------------------------------------------------

function Repair-AdoLoopWindowsEnv {
    [CmdletBinding()]
    param()

    if (-not [Environment]::Is64BitOperatingSystem) {
        throw '当前不是 64 位 Windows，无法构建 64 位 AdoLoop。'
    }
    if (-not [Environment]::Is64BitProcess) {
        throw ('当前 PowerShell 是 32 位进程（ProgramFiles 会解析到 Program Files (x86)），' +
               '请改用 64 位 PowerShell（%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe）。')
    }

    $fixed = @()

    $pf = [Environment]::GetFolderPath('ProgramFiles')
    if (-not [System.IO.Path]::IsPathRooted($env:ProgramFiles)) {
        $env:ProgramFiles = $pf
        $fixed += 'ProgramFiles'
    }

    $pf86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $currentPf86 = ${env:ProgramFiles(x86)}
    if ([string]::IsNullOrWhiteSpace($currentPf86) -or -not [System.IO.Path]::IsPathRooted($currentPf86)) {
        if ([string]::IsNullOrWhiteSpace($pf86)) {
            $pf86 = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion' `
                        -Name 'ProgramFilesDir (x86)' -ErrorAction SilentlyContinue).'ProgramFilesDir (x86)'
        }
        if ([string]::IsNullOrWhiteSpace($pf86)) {
            $pf86 = 'C:\Program Files (x86)'
        }
        Set-Item -Path 'env:ProgramFiles(x86)' -Value $pf86
        $fixed += 'ProgramFiles(x86)'
    }

    $pd = [Environment]::GetFolderPath('CommonApplicationData')
    if (-not [System.IO.Path]::IsPathRooted($env:ProgramData) -and -not [string]::IsNullOrWhiteSpace($pd)) {
        $env:ProgramData = $pd
        $fixed += 'ProgramData'
    }

    if ($fixed.Count -gt 0) {
        Write-Warn ("已修复被宿主破坏的环境变量：{0}" -f ($fixed -join ', '))
    } else {
        Write-Detail '环境变量检查：无需修复'
    }
}

# ---------------------------------------------------------------------------
# Visual Studio 定位（vswhere）
# ---------------------------------------------------------------------------

function Get-AdoLoopVsWherePath {
    <#
        .DESCRIPTION
            返回 vswhere.exe 的绝对路径；找不到则抛错。
            候选顺序：%ProgramFiles(x86)% 标准位置 → 注册表推得的 Program Files (x86) → PATH。
    #>
    [CmdletBinding()]
    param()

    $roots = New-Object System.Collections.Generic.List[string]

    $pf86 = ${env:ProgramFiles(x86)}
    if (-not [string]::IsNullOrWhiteSpace($pf86) -and [System.IO.Path]::IsPathRooted($pf86)) {
        $roots.Add($pf86)
    }
    $pf86b = [Environment]::GetFolderPath('ProgramFilesX86')
    if (-not [string]::IsNullOrWhiteSpace($pf86b)) { $roots.Add($pf86b) }
    $pf86c = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion' `
                -Name 'ProgramFilesDir (x86)' -ErrorAction SilentlyContinue).'ProgramFilesDir (x86)'
    if (-not [string]::IsNullOrWhiteSpace($pf86c)) { $roots.Add($pf86c) }
    $roots.Add('C:\Program Files (x86)')

    $seen = @{}
    foreach ($r in $roots) {
        if ([string]::IsNullOrWhiteSpace($r)) { continue }
        $cand = Join-Path $r 'Microsoft Visual Studio\Installer\vswhere.exe'
        if ($seen.ContainsKey($cand)) { continue }
        $seen[$cand] = $true
        if (Test-Path -LiteralPath $cand) {
            Write-Detail "vswhere：$cand"
            return $cand
        }
    }

    $cmd = Get-Command 'vswhere.exe' -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        Write-Detail "vswhere（PATH）：$($cmd.Source)"
        return $cmd.Source
    }

    throw ("找不到 vswhere.exe（已尝试：$($roots -join '; ') 与 PATH）。`n" +
           '        请安装 Visual Studio 2022（含「使用 C++ 的桌面开发」工作负载）或 Visual Studio 生成工具。')
}

function Get-AdoLoopVsInstances {
    <#
        .DESCRIPTION
            用 vswhere 枚举所有安装了 VC++ x64 工具集的 VS 实例，返回
            @{ Path; Version; ProductId; Label }[]，已按「完整 IDE 产品优先、版本降序」排序。
    #>
    [CmdletBinding()]
    param([string]$VsWherePath)

    if ([string]::IsNullOrWhiteSpace($VsWherePath)) {
        $VsWherePath = Get-AdoLoopVsWherePath
    }

    # 只取 ASCII 字段，避免 native 输出编码差异导致中文 displayName 乱码
    $json = & $VsWherePath -all -products '*' -prerelease -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' -format json
    if ($LASTEXITCODE -ne 0) {
        throw "vswhere 执行失败（退出码 $LASTEXITCODE）。"
    }
    $raw = ($json | Out-String)
    if ([string]::IsNullOrWhiteSpace($raw) -or $raw.Trim() -eq '[]') {
        return @()
    }

    $instances = @()
    foreach ($item in ($raw | ConvertFrom-Json)) {
        if ([string]::IsNullOrWhiteSpace($item.installationPath)) { continue }
        if (-not (Test-Path -LiteralPath $item.installationPath)) { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $item.installationPath 'Common7\Tools\Launch-VsDevShell.ps1'))) {
            continue
        }
        if (-not (Test-Path -LiteralPath (Join-Path $item.installationPath 'VC\Tools\MSVC'))) {
            continue
        }

        $productId = [string]$item.productId
        $label = $productId
        if ($productId.StartsWith('Microsoft.VisualStudio.Product.')) {
            $label = $productId.Substring('Microsoft.VisualStudio.Product.'.Length)
        }
        # 完整 IDE 产品（Community/Professional/Enterprise）比 BuildTools 更贴合开发者预期，优先选
        $isBuildTools = ($label -eq 'BuildTools')

        $instances += [pscustomobject]@{
            Path       = $item.installationPath
            Version    = [string]$item.installationVersion
            ProductId  = $productId
            Label      = $label
            BuildTools = $isBuildTools
            SortVer    = (Get-AdoLoopVersionKey $item.installationVersion)
        }
    }

    if ($instances.Count -eq 0) { return @() }

    return @($instances | Sort-Object -Property @{ Expression = 'BuildTools'; Ascending = $true },
                                                 @{ Expression = 'SortVer';    Descending = $true },
                                                 @{ Expression = 'Path';       Ascending = $true })
}

# "17.14.37531.7" → 可比较的数值键
function Get-AdoLoopVersionKey {
    param([string]$Version)
    $key = 0L
    $parts = @($Version -split '[^0-9]' | Where-Object { $_ -ne '' })
    for ($i = 0; $i -lt 4; $i++) {
        $n = 0L
        if ($i -lt $parts.Count) { [void][long]::TryParse($parts[$i], [ref]$n) }
        $key = ($key * 1000L) + $n
    }
    return $key
}

function Select-AdoLoopVsInstance {
    <#
        .DESCRIPTION
            选定要用的 VS 实例：显式 -VsInstallPath 优先，其次按
            「完整 IDE 产品优先 + 版本降序」自动挑选。
    #>
    [CmdletBinding()]
    param(
        [string]$VsInstallPath,
        [string]$VsWherePath
    )

    if (-not [string]::IsNullOrWhiteSpace($VsInstallPath)) {
        $resolved = [System.IO.Path]::GetFullPath($VsInstallPath)
        $devShell = Join-Path $resolved 'Common7\Tools\Launch-VsDevShell.ps1'
        if (-not (Test-Path -LiteralPath $devShell)) {
            throw "指定的 VS 安装目录无效（缺少 Common7\Tools\Launch-VsDevShell.ps1）：$resolved"
        }
        Write-Detail "VS（-VsInstallPath 指定）：$resolved"
        return [pscustomobject]@{
            Path = $resolved; Version = '(指定)'; Label = '(指定)'; BuildTools = $false; SortVer = 0
        }
    }

    $instances = Get-AdoLoopVsInstances -VsWherePath $VsWherePath
    if ($instances.Count -eq 0) {
        throw ("vswhere 未找到任何「已安装 VC++ x64 工具集」的 Visual Studio 2022 实例。`n" +
               '        请在 Visual Studio 安装程序中勾选「使用 C++ 的桌面开发」工作负载。')
    }

    Write-Info "发现 $($instances.Count) 个可用 VS 实例："
    foreach ($i in $instances) {
        Write-Info ("    - {0}  [{1}]  {2}" -f $i.Path, $i.Version, $i.Label)
    }
    Write-Detail "选中：$($instances[0].Path)"
    return $instances[0]
}

# ---------------------------------------------------------------------------
# 在当前会话加载 MSVC 环境（不用 vcvars64.bat）
# ---------------------------------------------------------------------------

function Enter-AdoLoopMsvcEnv {
    <#
        .DESCRIPTION
            通过 Launch-VsDevShell.ps1 在当前 PowerShell 会话内加载 MSVC 环境，
            随后剔除 PATH 中的 MinGW/Strawberry/MSYS/Cygwin，并校验 cl.exe 可用。
            幂等：重复调用只是重新加载同一套环境。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$VsInstallPath,
        [string]$Arch = 'amd64',
        [string]$HostArch = 'amd64'
    )

    $devShell = Join-Path $VsInstallPath 'Common7\Tools\Launch-VsDevShell.ps1'
    if (-not (Test-Path -LiteralPath $devShell)) {
        throw "找不到 VS 开发环境脚本：$devShell"
    }

    Write-SubStep "加载 MSVC 环境（$Arch / $HostArch）"
    Write-Info "Launch-VsDevShell.ps1：$devShell"

    # Launch-VsDevShell 内部会写非终止性错误；临时放宽 ErrorActionPreference，事后用 cl.exe 兜底校验。
    # 末尾必须 | Out-Null：Enter-VsDevShell 会往输出流写对象，泄漏出去会污染调用方的返回值。
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $devShell -VsInstallationPath $VsInstallPath -Arch $Arch -HostArch $HostArch -SkipAutomaticLocation | Out-Null
    } finally {
        $ErrorActionPreference = $savedEap
    }

    Remove-AdoLoopMinGwFromPath

    $cl = Get-Command 'cl.exe' -ErrorAction SilentlyContinue
    if ($null -eq $cl) {
        throw ("MSVC 环境加载后仍找不到 cl.exe（$devShell）。`n" +
               '        请确认该 VS 实例安装了「MSVC v143 - VS 2022 C++ x64/x86 生成工具」与 Windows SDK。')
    }
    Write-Ok "编译器：$($cl.Source)"
    if (-not [string]::IsNullOrWhiteSpace($env:VCToolsVersion)) {
        Write-Kv '工具集' $env:VCToolsVersion
    }
    if (-not [string]::IsNullOrWhiteSpace($env:WindowsSDKVersion)) {
        Write-Kv 'SDK' $env:WindowsSDKVersion.TrimEnd('\')
    }
}

function Remove-AdoLoopMinGwFromPath {
    <#
        .DESCRIPTION
            从当前会话 PATH 中剔除 MinGW / MSYS / Cygwin / Strawberry 目录。
            目的：让 CMake 在 PATH 里先看到 cl.exe 而不是 gcc，避免误选 GNU 工具链
            （gcc 与 Qt 的 msvc2022_64 库 ABI 不兼容，链接期必然失败）。
    #>
    [CmdletBinding()]
    param()

    $entries = @($env:PATH -split ';')
    $kept = New-Object System.Collections.Generic.List[string]
    $dropped = New-Object System.Collections.Generic.List[string]

    foreach ($e in $entries) {
        if ([string]::IsNullOrWhiteSpace($e)) { continue }   # 顺带清掉空项（空项语义为「当前目录」）
        if ($e -match '(?i)(mingw|msys|cygwin|strawberry)') {
            $dropped.Add($e.Trim())
            continue
        }
        $kept.Add($e.Trim())
    }

    $env:PATH = ($kept -join ';')

    if ($dropped.Count -gt 0) {
        Write-Ok "已从 PATH 剔除 $($dropped.Count) 个 MinGW/Strawberry 目录（防止 CMake 误选 gcc）"
        foreach ($d in $dropped) { Write-Info "- $d" }
    } else {
        Write-Detail 'PATH 中未发现 MinGW/Strawberry 目录'
    }
}

function Resolve-AdoLoopNinja {
    <#
        .DESCRIPTION
            优先使用 VS 自带 ninja：<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe；
            找不到再退回 PATH 中的 ninja.exe。
    #>
    [CmdletBinding()]
    param(
        [string]$VsInstallPath,
        [string]$Preferred
    )

    if (-not [string]::IsNullOrWhiteSpace($Preferred)) {
        if (-not (Test-Path -LiteralPath $Preferred)) {
            throw "指定的 ninja 不存在：$Preferred"
        }
        return [System.IO.Path]::GetFullPath($Preferred)
    }

    if (-not [string]::IsNullOrWhiteSpace($VsInstallPath)) {
        $vsNinja = Join-Path $VsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        if (Test-Path -LiteralPath $vsNinja) {
            return $vsNinja
        }
        Write-Warn "该 VS 实例未自带 ninja（$vsNinja），改用 PATH 中的 ninja"
    }

    $cmd = Get-Command 'ninja.exe' -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        return $cmd.Source
    }
    return $null
}

# ---------------------------------------------------------------------------
# CMake 定位 / Qt 定位 / 解释器检查
# ---------------------------------------------------------------------------

function Resolve-AdoLoopCMake {
    [CmdletBinding()]
    param([string]$Preferred)

    if (-not [string]::IsNullOrWhiteSpace($Preferred)) {
        if (-not (Test-Path -LiteralPath $Preferred)) { throw "指定的 cmake 不存在：$Preferred" }
        return [System.IO.Path]::GetFullPath($Preferred)
    }

    $cmd = Get-Command 'cmake.exe' -ErrorAction SilentlyContinue
    if ($null -ne $cmd) { return $cmd.Source }

    foreach ($cand in @(
        (Join-Path ${env:ProgramFiles} 'CMake\bin\cmake.exe'),
        (Join-Path (${env:ProgramFiles(x86)}) 'CMake\bin\cmake.exe')
    )) {
        if (-not [string]::IsNullOrWhiteSpace($cand) -and (Test-Path -LiteralPath $cand)) { return $cand }
    }

    throw '找不到 cmake.exe（需要 CMake 3.21+）。请安装 CMake 并确保其在 PATH 中，或用 -CMakeExe 指定。'
}

function Test-AdoLoopQtDir {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    if (-not (Test-Path -LiteralPath (Join-Path $Path 'lib\cmake\Qt6\Qt6Config.cmake'))) { return $false }
    if (-not (Test-Path -LiteralPath (Join-Path $Path 'bin\windeployqt.exe'))) { return $false }
    return $true
}

function Resolve-AdoLoopQtDir {
    <#
        .DESCRIPTION
            Qt 目录解析顺序：
              1) -QtDir 显式指定（校验，不合法直接报错）
              2) 环境变量 AdoLoopQtDir / QTDIR / Qt6_DIR（后者取其上一级）
              3) 扫描 <QtRoot>\<版本>\msvc2022_64（默认 QtRoot=C:\Qt，取版本最高者）
        QtRoot 可用环境变量 ADOLOOP_QT_ROOT 覆盖。
    #>
    [CmdletBinding()]
    param(
        [string]$QtDir,
        [string]$QtRoot
    )

    if (-not [string]::IsNullOrWhiteSpace($QtDir)) {
        $abs = [System.IO.Path]::GetFullPath($QtDir)
        if (-not (Test-AdoLoopQtDir -Path $abs)) {
            throw ("-QtDir 指向的目录不是可用的 Qt MSVC 安装：$abs`n" +
                   '        需要包含 lib\cmake\Qt6\Qt6Config.cmake 与 bin\windeployqt.exe。')
        }
        Write-Detail "Qt（-QtDir 指定）：$abs"
        return $abs
    }

    foreach ($name in @('AdoLoopQtDir', 'QTDIR', 'Qt6_DIR')) {
        $v = [Environment]::GetEnvironmentVariable($name)
        if ([string]::IsNullOrWhiteSpace($v)) { continue }
        $cand = $v
        if ($name -eq 'Qt6_DIR') { $cand = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $v)) }
        if (Test-AdoLoopQtDir -Path $cand) {
            Write-Detail "Qt（环境变量 $name）：$cand"
            return [System.IO.Path]::GetFullPath($cand)
        }
    }

    if ([string]::IsNullOrWhiteSpace($QtRoot)) {
        $QtRoot = [Environment]::GetEnvironmentVariable('ADOLOOP_QT_ROOT')
    }
    if ([string]::IsNullOrWhiteSpace($QtRoot)) { $QtRoot = 'C:\Qt' }

    $found = New-Object System.Collections.Generic.List[object]
    if (Test-Path -LiteralPath $QtRoot) {
        foreach ($verDir in @(Get-ChildItem -LiteralPath $QtRoot -Directory -ErrorAction SilentlyContinue)) {
            foreach ($kit in @('msvc2022_64', 'msvc2019_64')) {
                $cand = Join-Path $verDir.FullName $kit
                if (Test-AdoLoopQtDir -Path $cand) {
                    $found.Add([pscustomobject]@{ Path = $cand; Key = (Get-AdoLoopVersionKey $verDir.Name) })
                }
            }
        }
    }

    if ($found.Count -eq 0) {
        throw ("未能在 '$QtRoot' 下找到 Qt MSVC 安装（形如 $QtRoot\<版本>\msvc2022_64，" +
               "且需含 lib\cmake\Qt6\Qt6Config.cmake 与 bin\windeployqt.exe）。`n" +
               '        请安装 Qt 6.5+ 的 MSVC 2022 64-bit 组件，或用 -QtDir 显式指定，' +
               "或设置环境变量 ADOLOOP_QT_ROOT 指向 Qt 安装根目录。")
    }

    $best = @($found | Sort-Object -Property Key -Descending)[0]
    Write-Detail "Qt（自动探测）：$($best.Path)"
    return [System.IO.Path]::GetFullPath($best.Path)
}

function Assert-AdoLoopQtModules {
    <#
        .DESCRIPTION
            CMakeLists.txt 需要 Qt6 的 Widgets/Multimedia/Network/Concurrent 四个组件，
            缺任何一个都会在 configure 阶段失败；这里提前给出中文提示。
    #>
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$QtDir)

    $required = @('Widgets', 'Multimedia', 'Network', 'Concurrent')
    $missing = New-Object System.Collections.Generic.List[string]
    foreach ($m in $required) {
        $cfg = Join-Path $QtDir ("lib\cmake\Qt6$m\Qt6${m}Config.cmake")
        if (-not (Test-Path -LiteralPath $cfg)) { $missing.Add($m) }
    }
    if ($missing.Count -gt 0) {
        throw (("Qt 目录缺少必需组件：{0}`n        目录：{1}`n" +
                '        请在 Qt 维护工具中勾选对应组件后重试。') -f ($missing -join ', '), $QtDir)
    }
    Write-Detail "Qt 组件检查通过：$($required -join ', ')"
}

# ---------------------------------------------------------------------------
# 安全的目录删除（build.ps1 -Clean / package.ps1 / clean.ps1 共用）
# ---------------------------------------------------------------------------

function Assert-AdoLoopDeletablePath {
    <#
        .DESCRIPTION
            校验一个路径「允许被递归删除」，通过则返回规范化后的绝对路径，否则抛错。

            校验项（全部必须满足）：
              1) 路径非空、非空白；
              2) 绝对化后不是驱动器根（C:\）、不是 UNC 根（\\server\share）；
              3) 严格位于仓库根之下（不等于仓库根、不以仓库根为前缀的相似名绕过）；
              4) 路径中不含 .git 组件；
              5) 最外层目录名不在保护名单（src/docs/scripts/tests/...）；
              6) 若目标已存在且是重解析点（junction/symlink），其真实目标也必须在仓库内。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepoRoot
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw '拒绝执行：路径为空或仅含空白字符。'
    }

    $raw = $Path.Trim().Trim('"')
    if ([string]::IsNullOrWhiteSpace($raw)) {
        throw '拒绝执行：路径为空或仅含空白字符。'
    }

    $full = [System.IO.Path]::GetFullPath($raw).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($full)) {
        throw "拒绝执行：路径 '$Path' 无法规范化。"
    }

    # (2) 驱动器根 / UNC 根
    if ($full -match '^[A-Za-z]:$') {
        throw "拒绝执行：'$full' 是驱动器根目录。"
    }
    if ($full -match '^\\\\[^\\]+$' -or $full -match '^\\\\[^\\]+\\[^\\]+$') {
        throw "拒绝执行：'$full' 是 UNC/网络共享根目录。"
    }

    $root = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
    if ([string]::IsNullOrWhiteSpace($root)) {
        throw '拒绝执行：仓库根目录无法确定。'
    }

    # (3) 必须严格位于仓库根之下
    if ([string]::Equals($full, $root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "拒绝执行：'$full' 就是仓库根目录本身。"
    }
    $rootWithSep = $root + [System.IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($rootWithSep, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "拒绝执行：'$full' 不在仓库目录（$root）之下。"
    }

    # 仓库内的相对部分
    $relative = $full.Substring($rootWithSep.Length)
    $segments = @($relative -split '[\\/]' | Where-Object { $_ -ne '' })
    if ($segments.Count -eq 0) {
        throw "拒绝执行：'$full' 解析后没有有效子目录。"
    }

    # (4) .git
    foreach ($seg in $segments) {
        if ([string]::Equals($seg, '.git', [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "拒绝执行：'$full' 位于 .git 之下。"
        }
    }

    # (5) 保护名单（只看最外层）
    foreach ($p in $script:AdoLoopProtectedTopDirs) {
        if ([string]::Equals($segments[0], $p, [System.StringComparison]::OrdinalIgnoreCase)) {
            # 注意 $segments[0] 在双引号里必须写成 $($segments[0])，否则会被当成 $segments 加字面量 "[0]"
            throw "拒绝执行：'$($segments[0])' 是源码/配置目录（保护名单），不允许删除。"
        }
    }

    # 只允许删除仓库根下的顶层目录（禁止 build\..\..\ 之类穿透到深层）
    if ($segments.Count -gt 2) {
        throw "拒绝执行：'$relative' 层级过深（只允许删除仓库根下的一级或二级目录，如 build、dist\AdoLoop）。"
    }

    # (6) 重解析点（junction / symlink）：真实目标也必须在仓库内
    if (Test-Path -LiteralPath $full) {
        $item = Get-Item -LiteralPath $full -Force
        if (-not $item.PSIsContainer) {
            throw "拒绝执行：'$full' 不是目录（是文件）。"
        }
        $isReparse = (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0)
        if ($isReparse) {
            $targetField = $item.PSObject.Properties['Target']
            $target = $null
            if ($null -ne $targetField -and $null -ne $targetField.Value) {
                $target = [string]($targetField.Value | Select-Object -First 1)
            }
            if ([string]::IsNullOrWhiteSpace($target)) {
                throw "拒绝执行：'$full' 是重解析点（junction/符号链接）且无法解析其目标，出于安全不予删除。"
            }
            $realTarget = [System.IO.Path]::GetFullPath($target).TrimEnd('\', '/')
            if (-not $realTarget.StartsWith($rootWithSep, [System.StringComparison]::OrdinalIgnoreCase)) {
                Write-Warn "'$full' 是指向仓库外的重解析点 → $realTarget"
                throw "拒绝执行：'$full' 链接到仓库外的 '$realTarget'。"
            }
            Write-Detail "重解析点目标在仓库内（$realTarget），允许删除链接本身。"
        }
    }

    return $full
}

function Assert-AdoLoopWritableOutputPath {
    <#
        .DESCRIPTION
            校验一个「脚本将要写入」的路径：必须位于仓库内，且顶层目录名属于
            $script:AdoLoopAllowedOutputTopDirs（build / build-xxx / dist / dist\xxx）。
            这是「脚本只能写仓库内的 build\ / dist\」这条约束的强制执行点：
            例如 -BuildDir 'C:\temp\x' 或 -DistDir 'out' 都会被拒绝。

            返回规范化后的绝对路径。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [string]$ParamName = '路径'
    )

    $full = [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $root = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd('\', '/')
    $rootWithSep = $root + [System.IO.Path]::DirectorySeparatorChar

    if ([string]::Equals($full, $root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "$ParamName 不能是仓库根目录本身。"
    }
    if (-not $full.StartsWith($rootWithSep, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw ("$ParamName 必须在仓库目录（$root）之下，当前为：$full`n" +
               '        脚本只允许写仓库内的 build\ 与 dist\。')
    }

    $relative = $full.Substring($rootWithSep.Length)
    $top = @($relative -split '[\\/]' | Where-Object { $_ -ne '' })[0]

    $allowed = $false
    foreach ($a in $script:AdoLoopAllowedOutputTopDirs) {
        if ([string]::Equals($top, $a, [System.StringComparison]::OrdinalIgnoreCase) -or
            $top.StartsWith($a + '-', [System.StringComparison]::OrdinalIgnoreCase)) {
            $allowed = $true
            break
        }
    }
    if (-not $allowed) {
        throw (("$ParamName 的顶层目录是 '$top'，不在允许范围（{0}）内。`n" +
                '        脚本只允许写仓库内的 build\ 与 dist\。') -f ($script:AdoLoopAllowedOutputTopDirs -join ' / '))
    }

    return $full
}

function Get-AdoLoopDirectoryStats {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        return [pscustomobject]@{ Files = 0; Bytes = [long]0 }
    }
    $items = @(Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue)
    $sum = [long]0
    foreach ($i in $items) { $sum += [long]$i.Length }
    return [pscustomobject]@{ Files = $items.Count; Bytes = $sum }
}

function Remove-AdoLoopSafeDirectory {
    <#
        .DESCRIPTION
            校验通过后递归删除目录；目录不存在时静默跳过（幂等）。
    #>
    [CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$RepoRoot,
        [string]$Reason = '清理生成目录'
    )

    $safe = Assert-AdoLoopDeletablePath -Path $Path -RepoRoot $RepoRoot

    if (-not (Test-Path -LiteralPath $safe)) {
        Write-Info "跳过（不存在）：$safe"
        return $false
    }

    $stats = Get-AdoLoopDirectoryStats -Path $safe
    if ($PSCmdlet.ShouldProcess($safe, "$Reason（$($stats.Files) 个文件，$(Format-AdoLoopSize $stats.Bytes)）")) {
        Remove-Item -LiteralPath $safe -Recurse -Force -ErrorAction Stop
        Write-Ok "已删除：$safe（$($stats.Files) 个文件，$(Format-AdoLoopSize $stats.Bytes)）"
        return $true
    }
    Write-Warn "已跳过（WhatIf）：$safe"
    return $false
}

# ---------------------------------------------------------------------------
# CMake 缓存读取（用于「已配置过」的一致性检查）
# ---------------------------------------------------------------------------

function Read-AdoLoopCMakeCache {
    [CmdletBinding()]
    param([Parameter(Mandatory = $true)][string]$BuildDir)

    $map = @{}
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache)) { return $map }

    foreach ($line in [System.IO.File]::ReadAllLines($cache)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line.StartsWith('//') -or $line.StartsWith('#')) { continue }
        $i = $line.IndexOf('=')
        if ($i -lt 1) { continue }
        $k = $line.Substring(0, $i)
        $v = $line.Substring($i + 1)
        $c = $k.IndexOf(':')
        if ($c -gt 0) { $k = $k.Substring(0, $c) }
        $map[$k] = $v
    }
    return $map
}

# 判断生成器是否为多配置（Visual Studio / Xcode）
function Test-AdoLoopMultiConfigGenerator {
    param([Parameter(Mandatory = $true)][string]$Generator)
    return ($Generator -match '(?i)^(Visual Studio|Xcode)')
}

# ---------------------------------------------------------------------------
# 核心：构建（configure + build）
# ---------------------------------------------------------------------------

function Invoke-AdoLoopBuild {
    <#
        .SYNOPSIS
            configure + build AdoLoop，返回产物信息对象。

        .DESCRIPTION
            幂等：已有的 build 目录会复用（增量构建）；-Clean 会先删除再全量构建。
            生成器/源码目录与缓存不一致时给出明确提示（而不是让 cmake 抛原始错误）。

        .OUTPUTS
            [pscustomobject] 含 RepoRoot/BuildDir/Config/Generator/ExePath/ExeBytes/ExeSha256/Configured/Built
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$RepoRoot,

        [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
        [string]$Config = 'Release',

        [string]$BuildDir = 'build',
        [int]$Jobs = 0,
        [string]$QtDir,
        [string]$QtRoot,
        [string]$Generator = 'Ninja',
        [string[]]$Target,
        [switch]$Clean,
        [string]$VsInstallPath,
        [string]$CMakeExe,
        [string]$NinjaExe,
        [switch]$SkipEnvSetup
    )

    $buildAbs = Resolve-AdoLoopPath -Path $BuildDir -BaseDir $RepoRoot
    # 构建目录必须落在仓库内的 build\ / build-xxx\（拒绝把 build 指到仓库外或 src\ 等）
    $null = Assert-AdoLoopWritableOutputPath -Path $buildAbs -RepoRoot $RepoRoot -ParamName '-BuildDir'

    Write-Step "1/4 准备工具链"

    $cmake = Resolve-AdoLoopCMake -Preferred $CMakeExe
    Write-Ok "CMake：$cmake"
    $cmakeVersion = (& $cmake --version | Select-Object -First 1)
    Write-Kv 'CMake 版本' $cmakeVersion

    $qt = Resolve-AdoLoopQtDir -QtDir $QtDir -QtRoot $QtRoot
    Assert-AdoLoopQtModules -QtDir $qt
    Write-Ok "Qt：$qt"

    $vsPath = $null
    $ninja = $null
    if (-not $SkipEnvSetup -or ($Generator -match '(?i)ninja')) {
        $vs = Select-AdoLoopVsInstance -VsInstallPath $VsInstallPath
        $vsPath = $vs.Path
        Write-Ok "Visual Studio：$vsPath（$($vs.Label) $($vs.Version)）"

        if (-not $SkipEnvSetup) {
            Enter-AdoLoopMsvcEnv -VsInstallPath $vsPath
        }
    }

    if ($Generator -match '(?i)ninja') {
        $ninja = Resolve-AdoLoopNinja -VsInstallPath $vsPath -Preferred $NinjaExe
        if ($null -eq $ninja) {
            throw "找不到 ninja.exe（VS 自带位置与 PATH 都没有）。请用 -NinjaExe 指定，或改用 -Generator 'Visual Studio 17 2022'。"
        }
        Write-Ok "Ninja：$ninja"
    }

    if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }
    Write-Ok "并行度：-j $Jobs"

    # ---------------- 清理 ----------------
    if ($Clean) {
        Write-Step '2/4 清理构建目录（-Clean）'
        $null = Remove-AdoLoopSafeDirectory -Path $buildAbs -RepoRoot $RepoRoot -Reason '构建前清理'
    } else {
        Write-Step '2/4 复用已有构建目录（未指定 -Clean）'
        Write-Info "构建目录：$buildAbs"
    }

    $isMultiConfig = Test-AdoLoopMultiConfigGenerator -Generator $Generator

    # ---------------- configure 一致性检查 ----------------
    $cachePath = Join-Path $buildAbs 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cachePath) {
        $cache = Read-AdoLoopCMakeCache -BuildDir $buildAbs
        $cachedGen = $cache['CMAKE_GENERATOR']
        $cachedSrc = $cache['CMAKE_HOME_DIRECTORY']
        if (-not [string]::IsNullOrWhiteSpace($cachedGen) -and $cachedGen -ne $Generator) {
            throw ("构建目录已用生成器 '$cachedGen' 配置，与本次 '$Generator' 不一致。`n" +
                   "        请加 -Clean 重新配置，或换一个 -BuildDir。")
        }
        if (-not [string]::IsNullOrWhiteSpace($cachedSrc) -and
            -not (Test-AdoLoopSamePath -A $cachedSrc -B $RepoRoot)) {
            throw ("构建目录属于另一个源码目录：$cachedSrc`n        请加 -Clean 重新配置。")
        }
        Write-Info '检测到已有配置，将做增量 reconfigure + build'
    }

    # ---------------- configure ----------------
    Write-Step "3/4 configure（$Generator / $Config）"

    # 注意：数组字面量里必须把「字符串拼接」用括号整体括起来，
    #       否则 PowerShell 会把它当成两个独立元素（曾导致 -DCMAKE_PREFIX_PATH= 与路径被拆开）
    $cfgArgs = @(
        '-S', (ConvertTo-CMakePath $RepoRoot),
        '-B', (ConvertTo-CMakePath $buildAbs),
        '-G', $Generator,
        ("-DCMAKE_PREFIX_PATH=" + (ConvertTo-CMakePath $qt))
    )

    if (-not [string]::IsNullOrWhiteSpace($ninja)) {
        $cfgArgs += ("-DCMAKE_MAKE_PROGRAM=" + (ConvertTo-CMakePath $ninja))
    }
    if (-not $isMultiConfig) {
        $cfgArgs += "-DCMAKE_BUILD_TYPE=$Config"
    }

    Write-Info ("cmake " + ($cfgArgs -join ' '))
    $null = Invoke-AdoLoopNative -Exe $cmake -Arguments $cfgArgs -What 'CMake configure'

    # 兜底：确认 CMake 真的选了 MSVC（而不是被 PATH 里的 gcc 抢走）
    $cache = Read-AdoLoopCMakeCache -BuildDir $buildAbs
    $cxxCompiler = [string]$cache['CMAKE_CXX_COMPILER']
    if (-not [string]::IsNullOrWhiteSpace($cxxCompiler)) {
        if ($cxxCompiler -notmatch '(?i)cl\.exe$') {
            throw ("CMake 选到的 C++ 编译器不是 MSVC：$cxxCompiler`n" +
                   '        请检查 PATH 中是否有排在 cl.exe 之前的 gcc/g++，或加 -Clean 重新配置。')
        }
        Write-Ok "C++ 编译器：$cxxCompiler"
    }

    # ---------------- build ----------------
    Write-Step "4/4 build（-j $Jobs）"

    $buildArgs = @('--build', (ConvertTo-CMakePath $buildAbs))
    if ($isMultiConfig) { $buildArgs += @('--config', $Config) }
    $buildArgs += @('-j', "$Jobs")
    if ($null -ne $Target -and @($Target).Count -gt 0) {
        foreach ($t in $Target) { $buildArgs += @('--target', $t) }
    }

    Write-Info ("cmake " + ($buildArgs -join ' '))
    $null = Invoke-AdoLoopNative -Exe $cmake -Arguments $buildArgs -What 'CMake build'

    # ---------------- 产物 ----------------
    $exeName = 'AdoLoop.exe'
    $candidates = @()
    if ($isMultiConfig) {
        $candidates += (Join-Path $buildAbs (Join-Path 'src' (Join-Path $Config $exeName)))
    }
    $candidates += (Join-Path $buildAbs (Join-Path 'src' $exeName))
    # 指定了 -Target 时可能没有 exe
    $exe = $null
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { $exe = [System.IO.Path]::GetFullPath($c); break }
    }

    $result = [pscustomobject]@{
        RepoRoot    = $RepoRoot
        BuildDir    = $buildAbs
        Config      = $Config
        Generator   = $Generator
        QtDir       = $qt
        VsPath      = $vsPath
        NinjaExe    = $ninja
        CMakeExe    = $cmake
        Jobs        = $Jobs
        ExePath     = $exe
        ExeBytes    = [long]0
        ExeSha256   = ''
    }

    if ($null -ne $exe) {
        $result.ExeBytes = [long](Get-Item -LiteralPath $exe).Length
        $result.ExeSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    return $result
}

function Write-AdoLoopBuildSummary {
    param([Parameter(Mandatory = $true)]$Result)

    Write-Step '构建完成'
    Write-Kv '构建目录' $Result.BuildDir
    Write-Kv '配置' "$($Result.Config) / $($Result.Generator) / -j $($Result.Jobs)"

    if ($null -eq $Result.ExePath) {
        Write-Warn "未在构建目录下找到 AdoLoop.exe（可能指定了 -Target 只构建了部分目标）。"
        return
    }

    Write-Kv '产物' $Result.ExePath
    Write-Kv '字节数' ("{0:N0} 字节（{1}）" -f $Result.ExeBytes, (Format-AdoLoopSize $Result.ExeBytes))
    Write-Kv 'sha256' $Result.ExeSha256
    Write-Ok '构建成功'
}
