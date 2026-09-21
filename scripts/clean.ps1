<#
.SYNOPSIS
    清理 AdoLoop 的生成目录（构建目录 / 发行目录 / 打包产物）。

.DESCRIPTION
    安全约束（任一不满足即拒绝执行，退出码 1）：
      1) 路径非空、非空白（-Paths 里给了空字符串也会被拒）；
      2) 绝对化后不是驱动器根（C:\、D:）或 UNC/网络共享根（\\server\share）；
      3) 必须**严格位于仓库目录之下**——不等于仓库根，也不允许借相似前缀
         （如 C:\repo-evil）绕过；路径必须按 [System.IO.Path]::GetFullPath 规范化后比较；
      4) 路径中不得包含 .git 组件；
      5) 最外层目录名不得落在保护名单（src / docs / scripts / tests / .git 等）；
      6) 只允许删除仓库根下的**一级或二级**目录（拒 build\..\src 之类穿透）；
      7) 目标是 junction / 符号链接时，其真实目标也必须在仓库内（防借链接删到仓库外）。

    校验逻辑集中在 scripts\common.ps1 的 Assert-AdoLoopDeletablePath，
    build.ps1 -Clean 与 package.ps1 清空发行目录时复用同一套校验。

.PARAMETER Paths
    要删除的目录（相对仓库根或绝对路径）。默认 build、dist。
    例如 -Paths 'build'、-Paths @('dist','build-msvc')。

.PARAMETER KeepDist
    保留 dist（等价于 -Paths build）。

.PARAMETER ListOnly
    只列出将删除的内容与体积，不实际删除（等同于 -WhatIf）。

.EXAMPLE
    .\scripts\clean.ps1

.EXAMPLE
    .\scripts\clean.ps1 -WhatIf          # 只看不删

.EXAMPLE
    .\scripts\clean.ps1 -Paths 'build'
#>

[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string[]]$Paths,
    [switch]$KeepDist,
    [switch]$ListOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $repoRoot = Get-AdoLoopRepoRoot -StartDir $PSScriptRoot

    $targets = $Paths
    if ($null -eq $targets -or @($targets).Count -eq 0) {
        $targets = if ($KeepDist) { @('build') } else { @('build', 'dist') }
    }
    $targets = @($targets)

    Write-Step 'AdoLoop 清理'
    Write-Kv '仓库' $repoRoot
    Write-Kv '目标' ($targets -join ', ')
    Write-Host ''
    Write-Host '  校验目标路径（必须位于仓库内、非根目录、非保护目录）…' -ForegroundColor Gray

    # ---------- 阶段一：全部校验通过后才动手，避免删一半中断 ----------
    $validated = New-Object System.Collections.Generic.List[object]
    $rejected = New-Object System.Collections.Generic.List[object]

    foreach ($t in $targets) {
        try {
            $safe = Assert-AdoLoopDeletablePath -Path $t -RepoRoot $repoRoot
            $relative = $safe.Substring($repoRoot.TrimEnd('\', '/').Length + 1)
            $validated.Add([pscustomobject]@{ Input = $t; Full = $safe; Relative = $relative })
            Write-Ok ("通过：'{0}' → {1}" -f $t, $safe)
        } catch {
            $rejected.Add([pscustomobject]@{ Input = $t; Reason = $_.Exception.Message })
            Write-Fail ("拒绝：'{0}' → {1}" -f $t, $_.Exception.Message)
        }
    }

    if ($validated.Count -eq 0) {
        Write-Host ''
        Write-Warn '没有任何目标通过校验，未删除任何内容。'
        if ($rejected.Count -gt 0) {
            Write-Host ''
            Write-Host '清理被全部拒绝（安全校验生效）。' -ForegroundColor Yellow
            exit 1
        }
        exit 0
    }

    # ---------- 阶段二：删除（幂等：不存在的跳过） ----------
    Write-Host ''
    $removed = 0
    $skipped = 0
    foreach ($v in $validated) {
        if ($ListOnly) {
            $stats = Get-AdoLoopDirectoryStats -Path $v.Full
            if (Test-Path -LiteralPath $v.Full) {
                Write-Info ("将删除（-ListOnly）：{0} —— {1} 个文件，{2}" -f $v.Full, $stats.Files, (Format-AdoLoopSize $stats.Bytes))
            } else {
                Write-Info "将跳过（不存在）：$($v.Full)"
            }
            continue
        }

        if (Remove-AdoLoopSafeDirectory -Path $v.Full -RepoRoot $repoRoot -Reason '清理生成目录' -Confirm:$false) {
            $removed++
        } else {
            $skipped++
        }
    }

    Write-Host ''
    Write-Step '清理完成'
    Write-Kv '已删除' "$removed 个目录"
    Write-Kv '跳过' "$skipped 个目录（不存在或未确认）"
    if ($rejected.Count -gt 0) {
        Write-Kv '被拒绝' "$($rejected.Count) 个（安全校验）"
        Write-Host ''
        Write-Warn '部分目标因安全校验被拒绝（如上所列）。'
        exit 1
    }
    exit 0
} catch {
    Write-Host ''
    Write-Fail $_.Exception.Message
    if ($_.ScriptStackTrace) { Write-Verbose $_.ScriptStackTrace }
    exit 1
}
