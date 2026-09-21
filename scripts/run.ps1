<#
.SYNOPSIS
    构建（可选）后启动 AdoLoop，并摆好运行期 PATH。

.DESCRIPTION
    打包前的 build\src\AdoLoop.exe 旁边**没有** Qt DLL：它要靠 PATH 里的 <Qt>\bin
    才能找到 Qt6Core.dll / Qt6Widgets.dll 等，裸启动会报「找不到 Qt6Core.dll」。本脚本负责：

      1) （默认）先调用 build.ps1 的构建逻辑；-SkipBuild 则直接跑已有产物；
      2) 把 <Qt>\bin 与产物目录前置到**当前进程**的 PATH（不写系统/用户环境变量，退出即失效）；
         -NoQtPath 则不碰 PATH，用来验证「发行包自带 Qt DLL」（见示例 4）；
      3) 启动 AdoLoop.exe 并把退出码原样返回；exe 参数经 -ExeArgs 透传。

    默认前台运行并等待（关窗即结束）。-NoWait 启动后立即返回。
    -Offscreen 设置 QT_QPA_PLATFORM=offscreen（无窗口平台），配合 -TimeoutSec 做
    「启动 N 秒后仍存活即视为通过」的冒烟自检（本仓库的既定验证方式）。

.PARAMETER Config
    Release（默认）/ Debug / RelWithDebInfo / MinSizeRel。

.PARAMETER BuildDir
    构建目录。默认 build。

.PARAMETER SkipBuild
    跳过构建，直接启动已有产物。

.PARAMETER Exe
    直接指定要启动的 exe（默认自动在构建目录里找 AdoLoop.exe）。
    指定后不再构建（等价于隐含 -SkipBuild）。

.PARAMETER NoQtPath
    不修改 PATH（用于验证 dist 发行包的独立性：Qt DLL 应来自 exe 自己所在目录）。

.PARAMETER NoWait
    启动后不等待，立即返回 0。

.PARAMETER Offscreen
    设置 QT_QPA_PLATFORM=offscreen（无窗口平台插件），用于无人值守自检。

.PARAMETER TimeoutSec
    等待 N 秒后强制结束进程。>0 时：超时仍在运行 → 返回 0（判定「起来了」）并打印 stderr；
    提前自行退出 → 返回 1。适合自检。

.PARAMETER ExeArgs
    透传给 AdoLoop.exe 的参数（数组；也可写成位置参数放在最后）。
    形如 --foo 的开关请用本参数传（PowerShell 会把裸的 -foo 当成本脚本自己的参数）。

.PARAMETER QtDir, QtRoot, Jobs, Generator, Clean, Target, VsInstallPath, NinjaExe, CMakeExe
    透传给构建逻辑，含义同 build.ps1。

.EXAMPLE
    .\scripts\run.ps1

.EXAMPLE
    .\scripts\run.ps1 -SkipBuild -ExeArgs @('--help')

.EXAMPLE
    .\scripts\run.ps1 -SkipBuild -Offscreen -TimeoutSec 8      # 构建树的无窗口启动自检

.EXAMPLE
    # 发行包独立启动验证：PATH 里不含 Qt，仍应能起来
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    .\scripts\run.ps1 -Exe dist\AdoLoop\AdoLoop.exe -NoQtPath -Offscreen -TimeoutSec 8
#>

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Release',

    [string]$BuildDir = 'build',
    [switch]$SkipBuild,
    [string]$Exe,
    [switch]$NoQtPath,
    [switch]$NoWait,
    [switch]$Offscreen,
    [int]$TimeoutSec = 0,
    [string]$QtDir,
    [string]$QtRoot,
    [int]$Jobs = 0,
    [string]$Generator = 'Ninja',
    [switch]$Clean,
    [string[]]$Target,
    [string]$VsInstallPath,
    [string]$NinjaExe,
    [string]$CMakeExe,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExeArgs = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

# 启动进程。用 ProcessStartInfo + ReadToEndAsync 而不是 Start-Process：
#   * Start-Process 的 -ArgumentList 不接受空集合（无参数时必须整体省略）；
#   * Start-Process + 重定向时，若子进程很快退出，返回对象的 ExitCode 取不到值
#     —— 而「启动失败要报退出码」正是我们最需要它的场景（例如缺 DLL 时 0xC0000135）。
# ReadToEndAsync 在后台线程读管道，不会因为缓冲区写满而卡住子进程，也不阻塞主线程的超时等待。
function Start-AdoLoopProcess {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [switch]$Capture
    )

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.UseShellExecute = $false          # 必须为 $false 才能重定向；子进程同样继承本会话的环境变量
    if (@($ArgumentList).Count -gt 0) {
        $psi.Arguments = ($ArgumentList -join ' ')
    }

    $errTask = $null
    $outTask = $null
    if ($Capture) {
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
    }

    $proc = [System.Diagnostics.Process]::Start($psi)
    if ($Capture) {
        $outTask = $proc.StandardOutput.ReadToEndAsync()
        $errTask = $proc.StandardError.ReadToEndAsync()
    }

    return [pscustomobject]@{ Proc = $proc; OutTask = $outTask; ErrTask = $errTask }
}

# 取异步读取结果（带超时，避免极少数情况下任务不完成而卡住）
function Get-AdoLoopCapturedText {
    param($Task, [int]$TimeoutMs = 5000)
    if ($null -eq $Task) { return '' }
    try {
        if ($Task.Wait($TimeoutMs)) { return ([string]$Task.Result).Trim() }
    } catch {
        Write-Verbose "读取子进程输出失败：$($_.Exception.Message)"
    }
    return ''
}

try {
    $repoRoot = Get-AdoLoopRepoRoot -StartDir $PSScriptRoot
    Repair-AdoLoopWindowsEnv

    Write-Step "AdoLoop 运行（$Config）"

    $buildAbs = Resolve-AdoLoopPath -Path $BuildDir -BaseDir $repoRoot
    $exePath = $null
    $qt = $null

    if (-not [string]::IsNullOrWhiteSpace($Exe)) {
        Write-Step '1/3 使用指定的可执行文件（-Exe）'
        $exePath = Resolve-AdoLoopPath -Path $Exe -BaseDir $repoRoot
        if (-not (Test-Path -LiteralPath $exePath)) {
            throw "-Exe 指定的文件不存在：$exePath（若这是构建产物，请先执行 .\scripts\build.ps1 或 just build）"
        }
        Write-Ok ("产物：$exePath（{0:N0} 字节）" -f (Get-Item -LiteralPath $exePath).Length)
    } elseif ($SkipBuild) {
        Write-Step '1/3 跳过构建（-SkipBuild）'
        foreach ($c in @(
            (Join-Path $buildAbs (Join-Path 'src\Release' 'AdoLoop.exe')),
            (Join-Path $buildAbs (Join-Path 'src' 'AdoLoop.exe'))
        )) {
            if (Test-Path -LiteralPath $c) { $exePath = [System.IO.Path]::GetFullPath($c); break }
        }
        if ($null -eq $exePath) {
            throw ("-SkipBuild 但找不到已有产物：$buildAbs\src\AdoLoop.exe`n" +
                   '        请先执行 .\scripts\build.ps1（或 just build），或去掉 -SkipBuild。')
        }
        Write-Ok "复用产物：$exePath"
    } else {
        Write-Step '1/3 构建'
        $buildOutput = @(Invoke-AdoLoopBuild `
            -RepoRoot $repoRoot `
            -Config $Config `
            -BuildDir $BuildDir `
            -Jobs $Jobs `
            -QtDir $QtDir `
            -QtRoot $QtRoot `
            -Generator $Generator `
            -Target $Target `
            -Clean:$Clean `
            -VsInstallPath $VsInstallPath `
            -NinjaExe $NinjaExe `
            -CMakeExe $CMakeExe)

        $buildResult = $null
        foreach ($item in $buildOutput) {
            if ($null -ne $item -and ($item.PSObject.Properties.Name -contains 'ExePath')) { $buildResult = $item }
        }
        if ($null -eq $buildResult) { throw '构建函数未返回结果对象（内部错误）。' }
        $exePath = $buildResult.ExePath
        $qt = $buildResult.QtDir
        if ($null -eq $exePath) {
            throw '构建完成但未找到 AdoLoop.exe（是否用了 -Target 只构建了部分目标？）。'
        }
        Write-Ok ("产物：$exePath（{0:N0} 字节）" -f $buildResult.ExeBytes)
    }

    # ---------------- PATH ----------------
    Write-Step '2/3 准备运行环境'
    $exeDir = Split-Path -Parent $exePath

    if ($NoQtPath) {
        Write-Ok '按要求不修改 PATH（-NoQtPath）：Qt DLL 必须来自 exe 自身目录'
        $qtBinInPath = @($env:PATH -split ';' | Where-Object { $_ -match '(?i)\\Qt\\' })
        if ($qtBinInPath.Count -gt 0) {
            Write-Warn "当前 PATH 中仍有 Qt 目录（$($qtBinInPath -join '; ')），独立性验证会失真"
        } else {
            Write-Ok '当前 PATH 中没有任何 Qt 目录（独立性验证有效）'
        }
        Write-Kv 'PATH 前几项' (($env:PATH -split ';' | Select-Object -First 6) -join ' ; ')
    } else {
        if ($null -eq $qt) { $qt = Resolve-AdoLoopQtDir -QtDir $QtDir -QtRoot $QtRoot }
        $qtBin = Join-Path $qt 'bin'
        # 只改当前进程环境，不写系统/用户环境变量（退出即失效）
        $env:PATH = ($qtBin + ';' + $exeDir + ';' + $env:PATH)
        Write-Ok "已把 Qt bin 前置到 PATH：$qtBin"
        Write-Info "产物目录：$exeDir"
        if (-not (Test-Path -LiteralPath (Join-Path $qtBin 'Qt6Core.dll'))) {
            Write-Warn "在 $qtBin 下没看到 Qt6Core.dll，启动可能失败"
        }
    }

    # ---------------- 启动 ----------------
    Write-Step '3/3 启动'
    Write-Kv '程序' $exePath
    Write-Kv '参数' $(if (@($ExeArgs).Count -gt 0) { ($ExeArgs -join ' ') } else { '(无)' })

    if ($Offscreen) {
        $env:QT_QPA_PLATFORM = 'offscreen'
        Write-Ok 'QT_QPA_PLATFORM=offscreen（无窗口平台）'
    }
    Write-Verbose ("启动：" + $exePath + ' ' + ($ExeArgs -join ' '))

    if ($NoWait) {
        $r = Start-AdoLoopProcess -FilePath $exePath -ArgumentList $ExeArgs
        Write-Ok "已启动（PID $($r.Proc.Id)），不等待。"
        exit 0
    }

    if ($TimeoutSec -gt 0) {
        # 冒烟自检：启动 → 等 N 秒 → 仍在运行则判定「起来了」→ 强制结束
        $r = Start-AdoLoopProcess -FilePath $exePath -ArgumentList $ExeArgs -Capture
        $proc = $r.Proc
        Write-Info "PID $($proc.Id)，等待 $TimeoutSec 秒…"

        $exited = $proc.WaitForExit($TimeoutSec * 1000)

        if ($exited) {
            $code = $proc.ExitCode
            $hex = '0x{0:X8}' -f $code
            $errText = Get-AdoLoopCapturedText -Task $r.ErrTask
            $outText = Get-AdoLoopCapturedText -Task $r.OutTask
            Write-Fail "进程在 $TimeoutSec 秒内自行退出，退出码 $code（$hex）"
            if ($code -eq -1073741515) {
                Write-Fail '退出码 0xC0000135 = STATUS_DLL_NOT_FOUND：缺少依赖 DLL（Qt6Core.dll 等）'
            }
            if (-not [string]::IsNullOrWhiteSpace($errText)) {
                Write-Info '--- stderr ---'
                Write-Host $errText -ForegroundColor DarkGray
            } else {
                Write-Info 'stderr 为空'
            }
            if (-not [string]::IsNullOrWhiteSpace($outText)) {
                Write-Info '--- stdout ---'
                Write-Host $outText -ForegroundColor DarkGray
            }
            exit 1
        }

        Write-Ok "进程在 $TimeoutSec 秒后仍存活 → 启动自检通过（现在强制结束）"
        try { $proc.Kill() } catch { Write-Verbose "结束进程：$($_.Exception.Message)" }
        try { $null = $proc.WaitForExit(10000) } catch { }
        $reason = if ($proc.HasExited) { "被脚本强制结束（Kill，PID $($proc.Id)）" } else { '未能结束，请手动检查' }

        $errText = Get-AdoLoopCapturedText -Task $r.ErrTask
        $outText = Get-AdoLoopCapturedText -Task $r.OutTask

        Write-Kv '退出方式' $reason
        if (-not [string]::IsNullOrWhiteSpace($errText)) {
            Write-Info '--- stderr ---'
            Write-Host $errText -ForegroundColor DarkGray
        } else {
            Write-Info '--- stderr ---（空，无 Qt 警告）'
        }
        if (-not [string]::IsNullOrWhiteSpace($outText)) {
            Write-Info '--- stdout ---'
            Write-Host $outText -ForegroundColor DarkGray
        }
        exit 0
    }

    # 前台运行：原样透传退出码
    $r = Start-AdoLoopProcess -FilePath $exePath -ArgumentList $ExeArgs
    $r.Proc.WaitForExit()
    $code = $r.Proc.ExitCode
    Write-Kv '退出码' "$code"
    exit $code
} catch {
    Write-Host ''
    Write-Fail $_.Exception.Message
    if ($_.ScriptStackTrace) { Write-Verbose $_.ScriptStackTrace }
    Write-Host ''
    Write-Host '运行失败。可用 -Verbose 查看详细执行过程。' -ForegroundColor Red
    exit 1
}
