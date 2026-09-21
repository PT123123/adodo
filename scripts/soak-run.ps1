<#
.SYNOPSIS
    AdoLoop 启动期崩溃复现（soak）：按维度矩阵重复启动，抓到崩溃立即停下并保留现场。

.DESCRIPTION
    只做「启动 + 短观察」，不做任何界面交互。定位目标见 docs/11「M15 崩溃定位」：
    Release 版启动偶发 abort（WER: BEX64 / Qt6Core.dll / c0000409 / 数据 7 = FAST_FAIL_FATAL_APP_EXIT）。

    覆盖的维度（参数可自由组合）：
      * 产物       -Product dist|build|both   发行包 dist\AdoLoop\AdoLoop.exe / 构建树 build\src\AdoLoop.exe
      * 显示后端   -Platform offscreen|minimal|inherit
                   offscreen/minimal 无窗口；inherit = 不带 QT_QPA_PLATFORM（**会创建真实窗口**，
                   需 -AllowGuiWindow 显式确认）。
      * Qt 在 PATH -QtPathMode auto|on|off    auto = build 前置 Qt bin（同 run.ps1）、dist 不前置（同 run-dist）
      * 工作目录   -WorkDir repo|exe|temp
      * 顺序/并发  -Concurrent（每轮 PerRound 个实例同时起）
      * 系统负载   -Interleave none|build|package|cpu（每轮之前制造负载：构建、打包、CPU 占满）
      * 观察时长   -TimeoutSec（启动期崩溃，默认 4 秒足够）

    每次运行的观测量都留存：退出码、stdout/stderr 原文、该 pid 的日志行、terminate.log 增量；
    抓到崩溃时把新增 WER 报告（Report.wer）一并复制到现场目录。

    崩溃判定：退出码 0xC0000409 / 0xC0000005 / 0xC0000135，或该 pid 的日志出现 [fatal]。
    默认抓到一次即停并保留现场；-KeepGoing 跑完矩阵。

.PARAMETER Product
    dist / build / both（默认 both）。

.PARAMETER Platform
    offscreen（默认）/ minimal / inherit。inherit 会创建真实窗口，必须同时给 -AllowGuiWindow。

.PARAMETER Rounds
    轮数（默认 4）。每轮内启动 -PerRound 次。

.PARAMETER PerRound
    每轮启动次数（默认 8）。配合 -Concurrent 即「同时起 N 个」。

.PARAMETER Concurrent
    同一轮内并发启动（默认顺序）。

.PARAMETER TimeoutSec
    每次启动的观察秒数（默认 4）：到期仍存活即判定「启动成功」并强制结束。

.PARAMETER QtPathMode
    auto（默认）/ on / off：是否把 <Qt>\bin 前置到子进程 PATH。

.PARAMETER WorkDir
    repo（默认，仓库根）/ exe（产物目录）/ temp（%TEMP%）。

.PARAMETER Interleave
    none（默认）/ build / package / cpu：每轮之前制造的系统负载。

.PARAMETER ArtifactDir
    现场目录（默认 %TEMP%\adoloop-soak\<yyyyMMdd-HHmmss>）——**不写仓库**。

.PARAMETER DataDir
    AdoLoop 数据目录（默认从 %APPDATA%\AdoLoop\AdoLoop\adoloop.ini 的 dataDir 读取）。

.PARAMETER DeployPlatformPluginToDist
    发行包只带 platforms\qwindows.dll，没有 offscreen/minimal 插件；Qt 找不到时会
    **回退到真实窗口平台**。加这个开关会从 Qt 安装目录把所需平台插件复制进
    <dist>\platforms\（只补插件，不动 exe/DLL），让 dist 能真正无窗口跑。

.PARAMETER FallbackPluginPath
    兜底平台插件目录：把 q<Platform>.dll 放进 <该目录>\platforms\ 并设置 QT_PLUGIN_PATH。
    用于 -Interleave package 这类「dist 会被清空重建」的场景：即使 dist 里的平台插件
    被删掉，Qt 仍能从兜底目录找到无窗口插件，不会回退到真实窗口。

.PARAMETER AllowGuiWindow
    显式确认「允许创建真实窗口」（-Platform inherit，或 as-shipped dist 回退时）。
    不给则该组合直接报错，避免无意中弹出界面。

.PARAMETER KeepGoing
    抓到崩溃后继续跑完矩阵（默认抓到即停）。

.PARAMETER MaxRuns
    总启动次数上限（默认 0 = 不限）。

.EXAMPLE
    # 顺序基线：构建树 + offscreen，4 轮 × 8 次
    .\scripts\soak-run.ps1 -Product build

.EXAMPLE
    # 发行包 + 全并发 + 打包并发干扰（最接近「构建/打包窗口」的条件）
    .\scripts\soak-run.ps1 -Product dist -Concurrent -Rounds 6 -PerRound 8 -Interleave package -DeployPlatformPluginToDist

.EXAMPLE
    # 顺序跑发行包（as-shipped，会创建真实窗口 → 需人工确认）
    .\scripts\soak-run.ps1 -Product dist -Platform inherit -AllowGuiWindow
#>

[CmdletBinding()]
param(
    [ValidateSet('dist', 'build', 'both')]
    [string]$Product = 'both',

    [ValidateSet('offscreen', 'minimal', 'inherit')]
    [string]$Platform = 'offscreen',

    [int]$Rounds = 4,
    [int]$PerRound = 8,
    [switch]$Concurrent,
    [int]$TimeoutSec = 4,

    [ValidateSet('auto', 'on', 'off')]
    [string]$QtPathMode = 'auto',

    [ValidateSet('repo', 'exe', 'temp')]
    [string]$WorkDir = 'repo',

    [ValidateSet('none', 'build', 'package', 'cpu')]
    [string]$Interleave = 'none',

    [string]$ArtifactDir,
    [string]$DataDir,

    [switch]$DeployPlatformPluginToDist,
    [string]$FallbackPluginPath,
    [switch]$AllowGuiWindow,
    [switch]$KeepGoing,
    [int]$MaxRuns = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

$script:ExitAbort      = -1073740791  # 0xC0000409 STATUS_STACK_BUFFER_OVERRUN（Qt qAbort / __fastfail）
$script:ExitAccessViol = -1073741819  # 0xC0000005
$script:ExitDllMissing = -1073741515  # 0xC0000135
$script:CrashSceneN    = 0

# ---------------------------------------------------------------------------
# 小工具
# ---------------------------------------------------------------------------

function Get-AdoLoopDataDirFromIni {
    param([string]$Override)
    if (-not [string]::IsNullOrWhiteSpace($Override)) {
        return [System.IO.Path]::GetFullPath($Override)
    }
    $ini = Join-Path $env:APPDATA 'AdoLoop\AdoLoop\adoloop.ini'
    if (Test-Path -LiteralPath $ini) {
        foreach ($line in [System.IO.File]::ReadAllLines($ini)) {
            if ($line -match '^\s*dataDir\s*=\s*(.+?)\s*$') {
                return [System.IO.Path]::GetFullPath($Matches[1].Replace('/', '\'))
            }
        }
    }
    return [System.IO.Path]::GetFullPath((Join-Path $env:APPDATA 'AdoLoop\AdoLoop\AdoLoop'))
}

# 读日志「上次偏移之后」的新增内容（按字节偏移，避免整文件重复读）
function Read-AdoLoopLogDelta {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ref]$Offset
    )
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    $len = (Get-Item -LiteralPath $Path).Length
    if ($len -lt $Offset.Value) { $Offset.Value = 0 }   # 滚动/重建
    if ($len -eq $Offset.Value) { return '' }
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
                                 [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $null = $fs.Seek($Offset.Value, [System.IO.SeekOrigin]::Begin)
        $sr = New-Object System.IO.StreamReader($fs, [System.Text.Encoding]::UTF8)
        $text = $sr.ReadToEnd()
        $Offset.Value = $fs.Length
        return $text
    } finally { $fs.Dispose() }
}

# 并发轮里按 pid 切分日志（每条记录都带 [pid]）
function Select-AdoLoopPidLines {
    param([string]$Text, [int]$PidValue)
    if ([string]::IsNullOrWhiteSpace($Text)) { return '' }
    $tag = "[$PidValue]"
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line.Contains($tag)) { $out.Add($line) }
    }
    return ($out -join [Environment]::NewLine)
}

function Get-AdoLoopWerDirs {
    $dir = Join-Path $env:ProgramData 'Microsoft\Windows\WER\ReportArchive'
    if (-not (Test-Path -LiteralPath $dir)) { return @() }
    return @(Get-ChildItem -LiteralPath $dir -Directory -Filter 'AppCrash_AdoLoop.exe_*' -ErrorAction SilentlyContinue)
}

function Save-AdoLoopNewWerReports {
    param([Parameter(Mandatory = $true)][string]$DestDir)
    $copied = @()
    foreach ($d in (Get-AdoLoopWerDirs)) {
        $dst = Join-Path $DestDir ("wer-" + $d.Name + ".wer")
        if (Test-Path -LiteralPath $dst) { continue }
        $src = Join-Path $d.FullName 'Report.wer'
        if (Test-Path -LiteralPath $src) {
            Copy-Item -LiteralPath $src -Destination $dst -Force
            $copied += $dst
        }
    }
    return $copied
}

function Start-AdoLoopSoakProcess {
    param(
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $ExePath
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.WorkingDirectory = $WorkingDirectory

    # exe 可能正被打包清空重建（-Interleave package）：起不来不算崩溃，单独记一笔
    try {
        $proc = [System.Diagnostics.Process]::Start($psi)
    } catch {
        return [pscustomobject]@{
            Proc = $null; PidValue = 0; Started = $false; StartError = $_.Exception.Message
            Exited = $true; Killed = $false; Code = $null; OutText = ''; ErrText = ''
            HasWindow = $false; WindowTitle = ''
        }
    }
    return [pscustomobject]@{
        Proc     = $proc
        PidValue = $proc.Id
        Started  = $true
        StartError = ''
        OutTask  = $proc.StandardOutput.ReadToEndAsync()
        ErrTask  = $proc.StandardError.ReadToEndAsync()
        Exited   = $false
        Killed   = $false
        Code     = $null
        OutText  = ''
        ErrText  = ''
        HasWindow = $false
        WindowTitle = ''
    }
}

function Wait-AdoLoopLaunch {
    <#
        .DESCRIPTION
            等到 Deadline（同一批共享同一个截止时刻）→ 仍存活则强制结束。结果写回 Launch 对象。
            共享截止时刻很关键：否则并发一批 8 个会变成「依次各等 TimeoutSec」= 8 倍时长。
    #>
    param(
        [Parameter(Mandatory = $true)]$Launch,
        [Parameter(Mandatory = $true)][datetime]$Deadline
    )
    if (-not $Launch.Started) { return }
    $proc = $Launch.Proc
    $remainMs = [int][Math]::Max(0, ($Deadline - (Get-Date)).TotalMilliseconds)
    $exited = $proc.WaitForExit($remainMs)
    # 结束前记录「有没有原生窗口」：offscreen/minimal 下出现窗口 = 多半是 Qt 的原生消息框
    # （例如平台插件失败时的弹窗），说明这一次启动既没崩也不是健康的无窗口运行。
    try {
        $proc.Refresh()
        $Launch.HasWindow = ($proc.MainWindowHandle -ne 0)
        $Launch.WindowTitle = [string]$proc.MainWindowTitle
    } catch {
        $Launch.HasWindow = $false
        $Launch.WindowTitle = ''
    }
    if (-not $exited) {
        try { $proc.Kill() } catch { }
        try { $null = $proc.WaitForExit(5000) } catch { }
        $Launch.Killed = $true
    }
    try { if ($proc.HasExited) { $Launch.Code = $proc.ExitCode } } catch { }
    try { $Launch.Exited = $proc.HasExited } catch { }
    try { $Launch.OutText = if ($Launch.OutTask.Wait(3000)) { [string]$Launch.OutTask.Result } else { '' } } catch { }
    try { $Launch.ErrText = if ($Launch.ErrTask.Wait(3000)) { [string]$Launch.ErrTask.Result } else { '' } } catch { }
}

function Get-AdoLoopVerdict {
    <#
        .DESCRIPTION
            判定一次启动的结果：
              ok        观察期内存活（脚本强制结束）
              crash     **目标崩溃**：0xC0000409 / 0xC0000005，或日志出现 [fatal]
              dllmissing 0xC0000135 缺依赖 DLL —— 打包把 dist 清空重建时的正常现象，不是目标
              notstarted exe 不存在/起不来（同样只在 dist 被打包清空的窗口里出现）
              exit      其他退出码
    #>
    param(
        [Parameter(Mandatory = $true)]$Launch,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$PidLines
    )
    if (-not $Launch.Started) {
        return [pscustomobject]@{ Kind = 'notstarted'; Code = $null
            Note = ('进程未启动：' + $Launch.StartError) }
    }
    $logFatal = ($PidLines -match '\[fatal\]')
    if ($Launch.Killed) {
        if ($logFatal) {
            return [pscustomobject]@{ Kind = 'crash'; Code = $Launch.Code
                Note = '观察期内日志出现 [fatal]（进程随后 abort 终止，被脚本收尾）' }
        }
        return [pscustomobject]@{ Kind = 'ok'; Code = $Launch.Code; Note = '存活到观察期结束（脚本强制结束）' }
    }
    if ($null -ne $Launch.Code -and $Launch.Code -eq $script:ExitDllMissing) {
        return [pscustomobject]@{ Kind = 'dllmissing'; Code = $Launch.Code
            Note = '0xC0000135 缺依赖 DLL（dist 正被打包清空重建时的正常现象）' }
    }
    if ($null -ne $Launch.Code -and ($Launch.Code -eq $script:ExitAbort -or $Launch.Code -eq $script:ExitAccessViol)) {
        return [pscustomobject]@{ Kind = 'crash'; Code = $Launch.Code
            Note = ('崩溃退出码 0x{0:X8}' -f $Launch.Code) }
    }
    if ($null -ne $Launch.Code -and $Launch.Code -eq 0) {
        return [pscustomobject]@{ Kind = 'exit'; Code = 0; Note = '自行正常退出' }
    }
    if ($logFatal) {
        return [pscustomobject]@{ Kind = 'crash'; Code = $Launch.Code; Note = '日志出现 [fatal]' }
    }
    return [pscustomobject]@{ Kind = 'exit'; Code = $Launch.Code; Note = '未到观察时长即退出（非已知崩溃码）' }
}

# 保留现场：环境、退出码、输出、日志增量、terminate 增量
function Save-AdoLoopCrashScene {
    param(
        [Parameter(Mandatory = $true)]$Row,
        [Parameter(Mandatory = $true)]$Launch,
        [Parameter(Mandatory = $true)][string]$PidLines,
        [Parameter(Mandatory = $true)][string]$TermDelta,
        [Parameter(Mandatory = $true)][string]$ArtifactDir,
        [Parameter(Mandatory = $true)][string]$PlatformSet,
        [Parameter(Mandatory = $true)][string]$PathHead
    )
    $script:CrashSceneN++
    $dir = Join-Path $ArtifactDir ("crash-{0:00}-{1}-{2}" -f $script:CrashSceneN, $Row.Product, $Launch.PidValue)
    $null = New-Item -ItemType Directory -Path $dir -Force

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("时间          : " + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
    $lines.Add("产物          : " + $Row.Product)
    $lines.Add("pid           : " + $Launch.PidValue)
    $lines.Add("退出码        : " + $(if ($null -eq $Launch.Code) { '(取不到)' } else { "$($Launch.Code)  0x$('{0:X8}' -f $Launch.Code)" }))
    $lines.Add("判定          : " + $Row.Note)
    $lines.Add("QT_QPA_PLATFORM: " + $PlatformSet)
    $lines.Add("PATH 前几项   : " + $PathHead)
    $lines.Add("工作目录      : " + $Row.Cwd)
    $lines.Add("")
    $lines.Add("--- 该 pid 的日志增量（<dataDir>\logs\adoloop.log）---")
    $lines.Add($(if ([string]::IsNullOrWhiteSpace($PidLines)) { '(无)' } else { $PidLines }))
    $lines.Add("")
    $lines.Add("--- terminate.log 增量 ---")
    $lines.Add($(if ([string]::IsNullOrWhiteSpace($TermDelta)) { '(无)' } else { $TermDelta }))
    $lines.Add("")
    $lines.Add("--- stderr ---")
    $lines.Add($(if ([string]::IsNullOrWhiteSpace($Launch.ErrText)) { '(空)' } else { $Launch.ErrText }))
    $lines.Add("")
    $lines.Add("--- stdout ---")
    $lines.Add($(if ([string]::IsNullOrWhiteSpace($Launch.OutText)) { '(空)' } else { $Launch.OutText }))

    $scenePath = Join-Path $dir 'scene.txt'
    [System.IO.File]::WriteAllLines($scenePath, $lines, (New-Object System.Text.UTF8Encoding($false)))
    return $scenePath
}

# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

try {
    $repoRoot = Get-AdoLoopRepoRoot -StartDir $PSScriptRoot
    Repair-AdoLoopWindowsEnv

    $buildExe = Join-Path $repoRoot 'build\src\AdoLoop.exe'
    $distAbs = Join-Path $repoRoot 'dist\AdoLoop'
    $distExe = Join-Path $distAbs 'AdoLoop.exe'

    if ([string]::IsNullOrWhiteSpace($ArtifactDir)) {
        $ArtifactDir = Join-Path $env:TEMP (Join-Path 'adoloop-soak' (Get-Date -Format 'yyyyMMdd-HHmmss'))
    }
    $null = New-Item -ItemType Directory -Path $ArtifactDir -Force

    $dataDir = Get-AdoLoopDataDirFromIni -Override $DataDir
    $logPath = Join-Path $dataDir 'logs\adoloop.log'
    $termPath = Join-Path $dataDir 'logs\terminate.log'

    Write-Step 'AdoLoop 启动 soak（M15 崩溃复现）'
    Write-Kv '产物' $Product
    Write-Kv '显示后端' $(if ($Platform -eq 'inherit') { 'inherit（真实窗口）' } else { $Platform })
    Write-Kv '规模' ("$Rounds 轮 × $PerRound 次" + $(if ($Concurrent) { '（并发）' } else { '（顺序）' }))
    Write-Kv '观察时长' "$TimeoutSec 秒/次"
    Write-Kv 'Qt 在 PATH' $QtPathMode
    Write-Kv '工作目录' $WorkDir
    Write-Kv '系统负载' $Interleave
    Write-Kv '数据目录' $dataDir
    Write-Kv '日志' $logPath
    Write-Kv '现场目录' $ArtifactDir

    if ($Platform -eq 'inherit' -and -not $AllowGuiWindow) {
        throw ('-Platform inherit 会创建真实窗口（本仓库约定不启动带界面的 GUI）。' +
               '确认要跑请显式加 -AllowGuiWindow。')
    }

    $qt = Resolve-AdoLoopQtDir -QtDir $null -QtRoot $null
    $qtBin = Join-Path $qt 'bin'

    $targets = @()
    if ($Product -eq 'build' -or $Product -eq 'both') {
        if (-not (Test-Path -LiteralPath $buildExe)) { throw "构建树产物不存在：$buildExe（先 just build）" }
        $targets += [pscustomobject]@{ Name = 'build'; Exe = $buildExe; Dir = (Split-Path -Parent $buildExe) }
    }
    if ($Product -eq 'dist' -or $Product -eq 'both') {
        if (-not (Test-Path -LiteralPath $distExe)) { throw "发行包产物不存在：$distExe（先 just package）" }
        $targets += [pscustomobject]@{ Name = 'dist'; Exe = $distExe; Dir = $distAbs }
    }

    # 平台插件可用性：决定这一轮到底是不是真的无窗口
    $pluginFile = ''
    if ($Platform -ne 'inherit') {
        $pluginFile = Join-Path $qt ("plugins\platforms\q" + $Platform + ".dll")
        if (-not (Test-Path -LiteralPath $pluginFile)) {
            throw "Qt 安装目录里没有平台插件：$pluginFile"
        }
        # 兜底目录（QT_PLUGIN_PATH）：dist 被 -Interleave package 清空重建时仍然无窗口
        if (-not [string]::IsNullOrWhiteSpace($FallbackPluginPath)) {
            $fbDir = [System.IO.Path]::GetFullPath($FallbackPluginPath)
            $null = New-Item -ItemType Directory -Path (Join-Path $fbDir 'platforms') -Force
            Copy-Item -LiteralPath $pluginFile -Destination (Join-Path $fbDir ("platforms\q" + $Platform + ".dll")) -Force
            Write-Warn "兜底平台插件目录：$fbDir（通过 QT_PLUGIN_PATH 生效，dist 被清空也不会弹窗）"
        }
        foreach ($t in $targets) {
            $local = Join-Path $t.Dir ("platforms\q" + $Platform + ".dll")
            if (Test-Path -LiteralPath $local) { continue }
            if ($t.Name -eq 'dist') {
                if (-not [string]::IsNullOrWhiteSpace($FallbackPluginPath)) {
                    Write-Info "dist 缺 q$Platform 插件，但有 QT_PLUGIN_PATH 兜底 → 仍然无窗口"
                } elseif ($DeployPlatformPluginToDist) {
                    $null = New-Item -ItemType Directory -Path (Join-Path $t.Dir 'platforms') -Force
                    Copy-Item -LiteralPath $pluginFile -Destination $local -Force
                    Write-Warn "发行包缺少 q$Platform 平台插件：已从 Qt 安装目录补入 $local（仅为无窗口实验，未改 exe/DLL）"
                } elseif ($AllowGuiWindow) {
                    Write-Warn "发行包缺少 q$Platform 平台插件：Qt 会回退到真实窗口平台（已允许 -AllowGuiWindow）"
                } else {
                    throw ("发行包只带 platforms\qwindows.dll，没有 q$Platform 插件；Qt 会回退到真实窗口平台（会弹窗）。" +
                           "请加 -DeployPlatformPluginToDist（补插件）或 -FallbackPluginPath（兜底目录）或 -AllowGuiWindow（同意弹窗）后重试。")
                }
            }
        }
    }

    $savedPath = $env:PATH
    $savedQpa = $env:QT_QPA_PLATFORM
    if ($Platform -eq 'inherit') {
        Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    } else {
        $env:QT_QPA_PLATFORM = $Platform
    }
    if (-not [string]::IsNullOrWhiteSpace($FallbackPluginPath)) {
        $env:QT_PLUGIN_PATH = [System.IO.Path]::GetFullPath($FallbackPluginPath)
    }

    $logOffset = 0L
    if (Test-Path -LiteralPath $logPath) { $logOffset = (Get-Item -LiteralPath $logPath).Length }
    $termOffset = 0L
    if (Test-Path -LiteralPath $termPath) { $termOffset = (Get-Item -LiteralPath $termPath).Length }
    $werBefore = @(Get-AdoLoopWerDirs).Count

    $results = New-Object System.Collections.Generic.List[object]
    $totalRuns = 0
    $crashRuns = 0
    $stop = $false
    $loadProcs = @()

    for ($round = 1; $round -le $Rounds -and -not $stop; $round++) {
        # 每轮之前制造系统负载（贴近「构建/打包窗口」的条件）
        if ($Interleave -ne 'none' -and $round -gt 1) {
            if ($Interleave -eq 'cpu') {
                if (@($loadProcs).Count -eq 0) {
                    Write-Info '启动 2 个忙等进程制造 CPU 负载'
                    $burn = '$end=(Get-Date).AddSeconds(900); while((Get-Date) -lt $end){ $null = [math]::Sqrt(2.0) }'
                    $loadProcs = @(
                        (Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile', '-Command', $burn) -PassThru -WindowStyle Hidden),
                        (Start-Process -FilePath 'powershell.exe' -ArgumentList @('-NoProfile', '-Command', $burn) -PassThru -WindowStyle Hidden)
                    )
                }
            } elseif ($Interleave -eq 'build' -or $Interleave -eq 'package') {
                $scriptName = if ($Interleave -eq 'build') { 'build.ps1' } else { 'package.ps1' }
                $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $scriptName))
                if ($Interleave -eq 'package') { $args += '-SkipBuild' }
                Write-SubStep "第 $round 轮前：后台跑 $scriptName（制造负载/文件替换窗口）"
                $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $args -PassThru -WindowStyle Hidden
                $loadProcs += $p
            }
        }

        foreach ($t in $targets) {
            if ($stop) { break }
            if ($MaxRuns -gt 0 -and $totalRuns -ge $MaxRuns) { $stop = $true; break }

            if ($QtPathMode -eq 'on' -or ($QtPathMode -eq 'auto' -and $t.Name -eq 'build')) {
                $env:PATH = $qtBin + ';' + $t.Dir + ';' + $savedPath
            } else {
                $env:PATH = $savedPath
            }
            $pathHead = (($env:PATH -split ';' | Select-Object -First 4) -join ' ; ')

            $cwd = switch ($WorkDir) {
                'repo' { $repoRoot }
                'exe'  { $t.Dir }
                'temp' { $env:TEMP }
            }

            Write-SubStep ("第 $round 轮 · $($t.Name) · " + $(if ($Concurrent) { "$PerRound 并发" } else { '顺序' }) + " · cwd=$cwd")

            # 顺序：一批 1 个，跑满 PerRound 次；并发：一批 PerRound 个
            $remaining = $PerRound
            while ($remaining -gt 0 -and -not $stop) {
                $batch = if ($Concurrent) { $remaining } else { 1 }
                if ($MaxRuns -gt 0) {
                    $allowed = $MaxRuns - $totalRuns
                    if ($allowed -le 0) { $stop = $true; break }
                    if ($batch -gt $allowed) { $batch = $allowed }
                }

                $launches = New-Object System.Collections.Generic.List[object]
                for ($i = 1; $i -le $batch; $i++) {
                    $launches.Add((Start-AdoLoopSoakProcess -ExePath $t.Exe -WorkingDirectory $cwd))
                }
                $deadline = (Get-Date).AddSeconds($TimeoutSec)

                # 1) 先把这一批收干净（共享截止时刻）；2) 再取日志（此时 [fatal] 一定已落盘）；3) 判定
                foreach ($l in $launches) { Wait-AdoLoopLaunch -Launch $l -Deadline $deadline }
                $logDelta = Read-AdoLoopLogDelta -Path $logPath -Offset ([ref]$logOffset)
                $termDelta = Read-AdoLoopLogDelta -Path $termPath -Offset ([ref]$termOffset)

                foreach ($l in $launches) {
                    $pidLines = Select-AdoLoopPidLines -Text $logDelta -PidValue $l.PidValue
                    $verdict = Get-AdoLoopVerdict -Launch $l -PidLines $pidLines
                    # 无窗口后端下冒出原生窗口 → 单独记一类（疑似 Qt 原生消息框，既非正常也非崩溃）
                    if ($verdict.Kind -eq 'ok' -and $l.HasWindow -and $Platform -ne 'inherit') {
                        $verdict = [pscustomobject]@{ Kind = 'window'; Code = $verdict.Code
                            Note = ('无窗口后端下出现原生窗口（疑似 Qt 原生消息框）：标题="' + $l.WindowTitle + '"') }
                    }
                    $totalRuns++
                    $row = [pscustomobject]@{
                        Round = $round; Product = $t.Name; Platform = $Platform; Pid = $l.PidValue
                        Cwd = $cwd; Kind = $verdict.Kind; ExitCode = $verdict.Code
                        Note = $verdict.Note; LogLines = (@($pidLines -split "`r?`n" | Where-Object { $_ -ne '' })).Count
                    }
                    $results.Add($row)
                    if ($verdict.Kind -eq 'crash') {
                        $crashRuns++
                        $scene = Save-AdoLoopCrashScene -Row $row -Launch $l -PidLines $pidLines -TermDelta $termDelta `
                            -ArtifactDir $ArtifactDir -PlatformSet $(if ($Platform -eq 'inherit') { '(未设置)' } else { $Platform }) `
                            -PathHead $pathHead
                        Write-Fail "崩溃：$($t.Name) pid=$($l.PidValue) $($verdict.Note)"
                        Write-Info "现场：$scene"
                        Write-Info "该 pid 日志：$(if ([string]::IsNullOrWhiteSpace($pidLines)) { '(无)' } else { $pidLines })"
                        if (-not $KeepGoing) { $stop = $true }
                    }
                }

                $remaining -= $batch
            }

            if ($stop) { break }
        }
    }

    foreach ($p in $loadProcs) { try { $p.Kill() } catch { } }
    $env:PATH = $savedPath
    if ($null -eq $savedQpa) { Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
    else { $env:QT_QPA_PLATFORM = $savedQpa }
    Remove-Item Env:\QT_PLUGIN_PATH -ErrorAction SilentlyContinue

    $werAfter = @(Get-AdoLoopWerDirs).Count
    $newWer = @(Save-AdoLoopNewWerReports -DestDir $ArtifactDir)

    $csv = Join-Path $ArtifactDir 'results.csv'
    $results | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8

    Write-Step 'soak 结果'
    Write-Kv '总启动次数' $totalRuns
    Write-Kv '目标崩溃次数' $crashRuns
    Write-Kv '正常启动' (@($results | Where-Object { $_.Kind -eq 'ok' }).Count)
    Write-Kv '出现原生窗口' (@($results | Where-Object { $_.Kind -eq 'window' }).Count)
    Write-Kv '缺 DLL(0xC0000135)' (@($results | Where-Object { $_.Kind -eq 'dllmissing' }).Count)
    Write-Kv '未启动' (@($results | Where-Object { $_.Kind -eq 'notstarted' }).Count)
    Write-Kv '其他退出' (@($results | Where-Object { $_.Kind -eq 'exit' }).Count)
    Write-Kv '新增 WER 报告' ($werAfter - $werBefore)
    Write-Kv '结果明细' $csv
    Write-Kv '现场目录' $ArtifactDir

    if ($totalRuns -gt 0) {
        $results | Group-Object Product, Platform, Kind | ForEach-Object {
            Write-Kv ($_.Name -replace ', ', '/') $_.Count
        }
    }

    if ($crashRuns -gt 0) {
        Write-Fail "抓到 $crashRuns 次崩溃 —— 现场（日志增量/退出码/WER）在 $ArtifactDir"
        exit 2
    }
    Write-Ok "本轮 $totalRuns 次启动未复现崩溃"
    exit 0
} catch {
    Write-Host ''
    Write-Fail $_.Exception.Message
    if ($_.ScriptStackTrace) { Write-Verbose $_.ScriptStackTrace }
    exit 1
}
