<#
.SYNOPSIS
    AdoLoop 一键构建（MSVC + Qt6 + Ninja，纯 PowerShell）。

.DESCRIPTION
    从零完成 configure + build，不需要 cmd.exe，也不需要任何 .bat：

      1) 用 vswhere 定位 Visual Studio（含 VC++ x64 工具集）；
      2) 用 VS 自带的 Launch-VsDevShell.ps1 在**当前会话**加载 MSVC 环境
         （等价于 vcvars64.bat 的效果，但它是 PowerShell 脚本，内部加载
          Microsoft.VisualStudio.DevShell.dll 并调用 Enter-VsDevShell）；
      3) 剔除 PATH 中的 MinGW/Strawberry/MSYS/Cygwin，避免 CMake 误选 gcc；
      4) 自动探测 Qt（默认 C:\Qt\<版本>\msvc2022_64，取最高版本）；
      5) 优先使用 VS 自带 ninja，找不到再退回 PATH；
      6) configure + build，并在结束时输出产物路径、字节数与 sha256。

    任何一步失败都会以非零退出码结束。

.PARAMETER Config
    构建配置：Debug / Release / RelWithDebInfo / MinSizeRel。默认 Release。

.PARAMETER BuildDir
    构建目录（相对仓库根或绝对路径，必须位于仓库内）。默认 build。

.PARAMETER Jobs
    并行度。默认 = 逻辑 CPU 核数。

.PARAMETER QtDir
    Qt 的 MSVC 安装目录（含 bin\windeployqt.exe 与 lib\cmake\Qt6）。
    默认自动探测（C:\Qt\<版本>\msvc2022_64，取版本最高者）。

.PARAMETER Generator
    CMake 生成器。默认 Ninja；也可用 'Visual Studio 17 2022'（此时忽略 ninja 参数）。

.PARAMETER Clean
    构建前删除构建目录（全量 clean build）。默认增量。

.PARAMETER Target
    只构建指定目标（可多次给出），例如 -Target AdoLoop。

.PARAMETER VsInstallPath
    显式指定 Visual Studio 安装目录（默认由 vswhere 自动挑选）。

.PARAMETER NinjaExe
    显式指定 ninja.exe（默认用 VS 自带的那个）。

.PARAMETER CMakeExe
    显式指定 cmake.exe（默认从 PATH / Program Files 探测）。

.EXAMPLE
    .\scripts\build.ps1

.EXAMPLE
    .\scripts\build.ps1 -Clean -Config Debug -Jobs 4

.EXAMPLE
    .\scripts\build.ps1 -QtDir 'D:\Qt\6.8.3\msvc2022_64' -Verbose
#>

[CmdletBinding()]
param(
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
    [string]$NinjaExe,
    [string]$CMakeExe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'common.ps1')

try {
    $repoRoot = Get-AdoLoopRepoRoot -StartDir $PSScriptRoot
    Repair-AdoLoopWindowsEnv

    Write-Step "AdoLoop 构建（$Config）"
    Write-Kv '仓库' $repoRoot
    Write-Kv '脚本' $PSCommandPath
    Write-Kv '主机' "PowerShell $($PSVersionTable.PSVersion) ($($PSVersionTable.PSEdition))"

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

    # 取「最后一个带 ExePath 属性的对象」：即使将来某处误把内容泄进输出流，摘要也不会读错对象
    $result = $null
    foreach ($item in $buildOutput) {
        if ($null -ne $item -and ($item.PSObject.Properties.Name -contains 'ExePath')) { $result = $item }
    }
    if ($null -eq $result) {
        throw '构建函数未返回结果对象（内部错误）。'
    }

    Write-AdoLoopBuildSummary -Result $result
    exit 0
} catch {
    Write-Host ''
    Write-Fail $_.Exception.Message
    if ($_.ScriptStackTrace) {
        Write-Verbose $_.ScriptStackTrace
    }
    Write-Host ''
    Write-Host '构建失败。可用 -Verbose 查看详细执行过程。' -ForegroundColor Red
    exit 1
}
