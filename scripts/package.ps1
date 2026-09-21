<#
.SYNOPSIS
    AdoLoop 便携发行包打包（windeployqt + 桥脚本 + MSVC 运行库）。

.DESCRIPTION
    流程：
      1) 复用 build.ps1 的构建逻辑（可用 -SkipBuild 直接复用已有产物）；
      2) 把 AdoLoop.exe 复制到发行目录（默认 dist\AdoLoop\）；
      3) 用 windeployqt 部署 Qt6 运行库与插件（平台插件 platforms\qwindows.dll、
         多媒体插件 multimedia\*、图像/图标/样式/TLS 等）；
      4) 把仓库 scripts\*.py（faster-whisper 桥、日语分词桥）复制到
         <发行目录>\scripts\ —— 与 src/core/Tokenizer.cpp::defaultBridgeScriptPath() 和
         src/asr/FasterWhisperAsr.cpp::bridgeScriptPath() 的查找位置（<可执行目录>\scripts\）一致；
      5) 复制 MSVC 运行库（vcruntime140*.dll / msvcp140*.dll），
         使发行包在**没有装 VC++ 可再发行组件**的机器上也能启动（可用 -SkipCompilerRuntime 关掉）；
      6) 校验关键文件、列出顶层内容与总大小；-Zip 时额外打一个 zip。

    默认按 windeployqt 的完整输出部署（宁可多带，保证「不依赖 Qt 安装目录进 PATH」）；
    体积敏感时可用 -SkipOpenGlSw / -SkipD3DCompiler / -SkipTranslations 精简。

.PARAMETER Config
    Release（默认）/ Debug / RelWithDebInfo / MinSizeRel，需与已有的构建配置一致。

.PARAMETER BuildDir
    构建目录。默认 build。

.PARAMETER DistDir
    发行目录（相对仓库根或绝对路径，必须位于仓库内）。默认 dist\AdoLoop。

.PARAMETER SkipBuild
    跳过构建，直接部署 <BuildDir>\src\AdoLoop.exe。

.PARAMETER Zip
    额外生成 zip 压缩包（默认 <DistDir 同级>\AdoLoop-<版本>-win64.zip）。

.PARAMETER ZipPath
    自定义 zip 输出路径。

.PARAMETER SkipTranslations
    不部署 Qt 翻译文件（qt_zh_CN.qm 等，约 5 MB）。关掉后 Qt 自带对话框会退回英文。

.PARAMETER SkipOpenGlSw
    不部署 opengl32sw.dll（软件 OpenGL 回退，约 20 MB）。
    仅当目标机器都装了可用的显卡驱动时才建议开启。

.PARAMETER SkipD3DCompiler
    不部署 d3dcompiler_47.dll / dxcompiler.dll / dxil.dll（约 25 MB）。

.PARAMETER SkipCompilerRuntime
    不部署 MSVC 运行库（目标机器需自行安装 VC++ 可再发行组件）。

.PARAMETER Jobs, QtDir, QtRoot, Generator, Clean, Target, VsInstallPath, NinjaExe, CMakeExe
    透传给构建逻辑，含义同 build.ps1。注意 -Clean 会先全量重建。

.EXAMPLE
    .\scripts\package.ps1

.EXAMPLE
    .\scripts\package.ps1 -SkipBuild -Zip

.EXAMPLE
    .\scripts\package.ps1 -Clean -SkipOpenGlSw -SkipD3DCompiler
#>

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Release',

    [string]$BuildDir = 'build',
    [string]$DistDir = 'dist\AdoLoop',
    [switch]$SkipBuild,
    [switch]$Zip,
    [string]$ZipPath,
    [switch]$SkipTranslations,
    [switch]$SkipOpenGlSw,
    [switch]$SkipD3DCompiler,
    [switch]$SkipCompilerRuntime,
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

# 从 CMakeLists.txt 的 project(... VERSION x.y.z) 取版本号，仅用于 zip 命名
function Get-AdoLoopVersion {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    $top = Join-Path $RepoRoot 'CMakeLists.txt'
    if (Test-Path -LiteralPath $top) {
        foreach ($line in [System.IO.File]::ReadAllLines($top)) {
            if ($line -match 'project\s*\(\s*AdoLoop\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)') {
                return $Matches[1]
            }
        }
    }
    return '0.0.0'
}

try {
    $repoRoot = Get-AdoLoopRepoRoot -StartDir $PSScriptRoot
    Repair-AdoLoopWindowsEnv

    Write-Step "AdoLoop 打包（$Config）"
    Write-Kv '仓库' $repoRoot

    # ---------------- 1. 构建 ----------------
    $exe = $null
    $buildAbs = Resolve-AdoLoopPath -Path $BuildDir -BaseDir $repoRoot
    $qt = $null
    $vsPath = $null

    if ($SkipBuild) {
        Write-Step '1/5 跳过构建（-SkipBuild）'
        $exeCandidates = @(
            (Join-Path $buildAbs (Join-Path 'src\Release' 'AdoLoop.exe')),
            (Join-Path $buildAbs (Join-Path 'src' 'AdoLoop.exe'))
        )
        foreach ($c in $exeCandidates) {
            if (Test-Path -LiteralPath $c) { $exe = [System.IO.Path]::GetFullPath($c); break }
        }
        if ($null -eq $exe) {
            throw ("-SkipBuild 但找不到已有产物（已尝试：$($exeCandidates -join '; ')）。`n" +
                   '        请先执行 .\scripts\build.ps1（或去掉 -SkipBuild）。')
        }
        Write-Ok "复用产物：$exe"
        $qt = Resolve-AdoLoopQtDir -QtDir $QtDir -QtRoot $QtRoot
        Assert-AdoLoopQtModules -QtDir $qt
        Write-Ok "Qt：$qt"
    } else {
        Write-Step '1/5 构建'
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

        $exe = $buildResult.ExePath
        $qt = $buildResult.QtDir
        $vsPath = $buildResult.VsPath
        if ($null -eq $exe) {
            throw '构建完成但未找到 AdoLoop.exe（是否用了 -Target 只构建了部分目标？）。'
        }
        Write-Ok ("产物：$exe（{0:N0} 字节，sha256 {1}）" -f $buildResult.ExeBytes, $buildResult.ExeSha256)
    }

    $exeBytes = [long](Get-Item -LiteralPath $exe).Length
    $exeSha = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    $windeployqt = Join-Path $qt 'bin\windeployqt.exe'
    if (-not (Test-Path -LiteralPath $windeployqt)) {
        throw "找不到 windeployqt.exe：$windeployqt"
    }

    # ---------------- 2. 准备发行目录 ----------------
    $distAbs = Resolve-AdoLoopPath -Path $DistDir -BaseDir $repoRoot
    $null = Assert-AdoLoopWritableOutputPath -Path $distAbs -RepoRoot $repoRoot -ParamName '-DistDir'
    $null = Assert-AdoLoopDeletablePath -Path $distAbs -RepoRoot $repoRoot

    Write-Step '2/5 准备发行目录'
    Write-Kv '发行目录' $distAbs
    if (Test-Path -LiteralPath $distAbs) {
        $stats = Get-AdoLoopDirectoryStats -Path $distAbs
        Write-Info "目标已存在（$($stats.Files) 个文件，$(Format-AdoLoopSize $stats.Bytes)），将先清空以保证幂等"
        $null = Remove-AdoLoopSafeDirectory -Path $distAbs -RepoRoot $repoRoot -Reason '重新打包前清空发行目录'
    }
    $null = New-Item -ItemType Directory -Path $distAbs -Force
    Write-Ok '发行目录就绪'

    $distExe = Join-Path $distAbs 'AdoLoop.exe'
    Copy-Item -LiteralPath $exe -Destination $distExe -Force
    Write-Ok ("已复制主程序：$distExe（{0:N0} 字节）" -f $exeBytes)

    # ---------------- 3. windeployqt ----------------
    Write-Step '3/5 部署 Qt 运行库与插件（windeployqt）'

    $logDir = $buildAbs
    if (-not (Test-Path -LiteralPath $logDir)) { $null = New-Item -ItemType Directory -Path $logDir -Force }
    $logPath = Join-Path $logDir "windeployqt-$Config.log"
    $null = Assert-AdoLoopWritableOutputPath -Path $logDir -RepoRoot $repoRoot -ParamName '部署日志目录'

    $wdArgs = @(
        "--$($Config.ToLowerInvariant().Replace('relwithdebinfo', 'release').Replace('minsizerel', 'release'))",
        '--dir', $distAbs,
        '--verbose', '1'
    )
    if ($SkipTranslations) { $wdArgs += '--no-translations' }
    if ($SkipOpenGlSw)     { $wdArgs += '--no-opengl-sw' }
    if ($SkipD3DCompiler)  { $wdArgs += @('--no-system-d3d-compiler', '--no-system-dxc-compiler') }
    $wdArgs += $distExe

    Write-Info ("windeployqt " + ($wdArgs -join ' '))
    Write-Info "日志：$logPath"

    # windeployqt 会往 stderr 写进度；PS 5.1 在 Stop 策略下会把 native stderr 当终止错误，故临时放宽
    $savedEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $windeployqt @wdArgs 2>&1 |
            ForEach-Object { $_.ToString() } |
            Tee-Object -FilePath $logPath |
            Out-Host
        $wdCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedEap
    }

    if ($wdCode -ne 0) {
        Write-Fail "windeployqt 退出码 $wdCode（原始输出见 $logPath）"
        throw 'windeployqt 部署失败。'
    }
    Write-Ok 'windeployqt 完成'

    # windeployqt 可能在 exe 同级再建一个子目录；把可能存在的 .qml 等无关内容忽略
    if (-not (Test-Path -LiteralPath (Join-Path $distAbs 'platforms\qwindows.dll'))) {
        throw ("windeployqt 结束后仍缺少平台插件 platforms\qwindows.dll。`n" +
               "        windeployqt 输出见：$logPath")
    }

    # ---------------- 4. 桥脚本 + MSVC 运行库 ----------------
    Write-Step '4/5 复制 Python 桥脚本与 MSVC 运行库'

    # 4a) scripts\*.py —— 必须放在 <可执行目录>\scripts\
    #     （Tokenizer::defaultBridgeScriptPath() / FasterWhisperAsr::bridgeScriptPath() 的查找位置）
    $srcScriptDir = Join-Path $repoRoot 'scripts'
    $dstScriptDir = Join-Path $distAbs $script:AdoLoopBridgeDirName
    $pyFiles = @(Get-ChildItem -LiteralPath $srcScriptDir -Filter '*.py' -File -ErrorAction SilentlyContinue)
    if ($pyFiles.Count -eq 0) {
        Write-Warn "仓库 scripts\ 下没有 .py 桥脚本，跳过"
    } else {
        $null = New-Item -ItemType Directory -Path $dstScriptDir -Force
        foreach ($f in $pyFiles) {
            Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dstScriptDir $f.Name) -Force
            Write-Info ("- scripts\{0}（{1:N0} 字节）" -f $f.Name, $f.Length)
        }
        Write-Ok "已复制 $($pyFiles.Count) 个桥脚本 → $dstScriptDir"
    }

    # 4b) MSVC 运行库：优先从 VS 的 VC\Redist 取松散 DLL（--compiler-runtime 找不到时会漏）
    $crtCopied = @()
    if (-not $SkipCompilerRuntime) {
        $crtNames = @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll',
                      'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll')
        $crtDir = $null

        # -SkipBuild 时前面没跑过构建，$vsPath 还是空的 —— 这里补一次 vswhere 定位
        if ([string]::IsNullOrWhiteSpace($vsPath)) {
            try {
                $vsPath = (Select-AdoLoopVsInstance -VsInstallPath $VsInstallPath).Path
                Write-Detail "MSVC 运行库来源 VS：$vsPath"
            } catch {
                Write-Warn "定位 Visual Studio 失败，无法复制 MSVC 运行库：$($_.Exception.Message)"
            }
        }

        if (-not [string]::IsNullOrWhiteSpace($vsPath)) {
            $redistRoot = Join-Path $vsPath 'VC\Redist\MSVC'
            if (Test-Path -LiteralPath $redistRoot) {
                $verDirs = @(Get-ChildItem -LiteralPath $redistRoot -Directory -ErrorAction SilentlyContinue |
                             Where-Object { $_.Name -match '^[0-9]' } |
                             Sort-Object -Property Name -Descending)
                foreach ($d in $verDirs) {
                    $cand = Join-Path $d.FullName 'x64\Microsoft.VC143.CRT'
                    if (Test-Path -LiteralPath $cand) { $crtDir = $cand; break }
                }
            }
        }

        if ($null -eq $crtDir) {
            Write-Warn '未找到 VS 的 VC\Redist\MSVC\<版本>\x64\Microsoft.VC143.CRT 目录，跳过 MSVC 运行库复制'
            Write-Warn '（目标机器需要自行安装 VC++ 可再发行组件；可用 -VsInstallPath 指定 VS 目录后重试）'
        } else {
            foreach ($n in $crtNames) {
                $src = Join-Path $crtDir $n
                if (Test-Path -LiteralPath $src) {
                    Copy-Item -LiteralPath $src -Destination (Join-Path $distAbs $n) -Force
                    $crtCopied += $n
                }
            }
            if ($crtCopied.Count -gt 0) {
                Write-Ok "MSVC 运行库已复制（$($crtCopied.Count) 个）：$($crtCopied -join ', ')"
            } else {
                Write-Warn "在 $crtDir 下没找到任何预期的运行库 DLL"
            }
        }
    } else {
        Write-Info '按要求跳过 MSVC 运行库（-SkipCompilerRuntime）'
    }

    # ---------------- 5. 校验与汇总 ----------------
    Write-Step '5/5 校验发行包'

    $checks = @(
        [pscustomobject]@{ Name = '主程序';                Path = 'AdoLoop.exe';              Required = $true },
        [pscustomobject]@{ Name = 'Qt6 Core';              Path = 'Qt6Core.dll';              Required = $true },
        [pscustomobject]@{ Name = 'Qt6 Widgets';           Path = 'Qt6Widgets.dll';           Required = $true },
        [pscustomobject]@{ Name = 'Qt6 Multimedia';        Path = 'Qt6Multimedia.dll';        Required = $true },
        [pscustomobject]@{ Name = 'Qt6 Network';           Path = 'Qt6Network.dll';           Required = $true },
        [pscustomobject]@{ Name = '平台插件';              Path = 'platforms\qwindows.dll';   Required = $true }
    )
    $failures = @()
    foreach ($c in $checks) {
        $p = Join-Path $distAbs $c.Path
        if (Test-Path -LiteralPath $p) {
            Write-Ok ("{0,-14} {1}（{2:N0} 字节）" -f $c.Name, $c.Path, (Get-Item -LiteralPath $p).Length)
        } else {
            Write-Fail ("{0,-14} 缺失：{1}" -f $c.Name, $c.Path)
            $failures += $c.Path
        }
    }

    # 多媒体插件（Qt Multimedia 的后端插件）
    $mmDir = Join-Path $distAbs 'multimedia'
    $mmPlugins = @()
    if (Test-Path -LiteralPath $mmDir) {
        $mmPlugins = @(Get-ChildItem -LiteralPath $mmDir -Filter '*.dll' -File -ErrorAction SilentlyContinue)
    }
    if ($mmPlugins.Count -gt 0) {
        Write-Ok ("多媒体插件      multimedia\（{0} 个）：{1}" -f $mmPlugins.Count, (($mmPlugins | ForEach-Object { $_.Name }) -join ', '))
    } else {
        Write-Fail '多媒体插件      缺失：multimedia\*.dll'
        $failures += 'multimedia\*.dll'
    }

    # 桥脚本
    if ($pyFiles.Count -gt 0) {
        $missingPy = @()
        foreach ($f in $pyFiles) {
            if (-not (Test-Path -LiteralPath (Join-Path $dstScriptDir $f.Name))) { $missingPy += $f.Name }
        }
        if ($missingPy.Count -eq 0) {
            Write-Ok ("桥脚本          scripts\（{0} 个）：{1}" -f $pyFiles.Count, (($pyFiles | ForEach-Object { $_.Name }) -join ', '))
        } else {
            Write-Fail ("桥脚本          缺失：{0}" -f ($missingPy -join ', '))
            $failures += $missingPy
        }
    }

    # MSVC 运行库
    if (-not $SkipCompilerRuntime) {
        $crtOk = @($crtCopied | Where-Object { Test-Path -LiteralPath (Join-Path $distAbs $_) })
        if ($crtOk.Count -gt 0) {
            Write-Ok ("MSVC 运行库     {0} 个" -f $crtOk.Count)
        } else {
            Write-Warn 'MSVC 运行库     未随包提供（目标机器需装 VC++ 可再发行组件）'
        }
    }

    # 顶层内容 + 统计
    Write-Step '发行包内容'
    $topItems = @(Get-ChildItem -LiteralPath $distAbs -Force | Sort-Object -Property @{Expression={$_.PSIsContainer}; Descending=$true}, Name)
    foreach ($t in $topItems) {
        if ($t.PSIsContainer) {
            $sub = Get-AdoLoopDirectoryStats -Path $t.FullName
            Write-Info ("{0,-20} <目录> {1,4} 个文件 {2,12}" -f $t.Name, $sub.Files, (Format-AdoLoopSize $sub.Bytes))
        } else {
            Write-Info ("{0,-20}        {1,12}" -f $t.Name, (Format-AdoLoopSize $t.Length))
        }
    }

    $total = Get-AdoLoopDirectoryStats -Path $distAbs
    Write-Host ''
    Write-Kv '发行目录' $distAbs
    Write-Kv '顶层条目' "$($topItems.Count) 项"
    Write-Kv '文件总数' "$($total.Files) 个"
    Write-Kv '总大小' ("{0:N0} 字节（{1}）" -f $total.Bytes, (Format-AdoLoopSize $total.Bytes))
    Write-Kv '主程序' ("{0:N0} 字节" -f $exeBytes)
    Write-Kv 'sha256' $exeSha
    Write-Kv '部署日志' $logPath

    if ($failures.Count -gt 0) {
        throw ("发行包校验失败，缺少：{0}" -f ($failures -join ', '))
    }

    # ---------------- 可选 zip ----------------
    if ($Zip) {
        Write-Step '打包 zip'
        $version = Get-AdoLoopVersion -RepoRoot $repoRoot
        if ([string]::IsNullOrWhiteSpace($ZipPath)) {
            $zipParent = Split-Path -Parent $distAbs
            $zipPath = Join-Path $zipParent ("AdoLoop-$version-win64-" + $Config.ToLowerInvariant() + '.zip')
        } else {
            $zipPath = Resolve-AdoLoopPath -Path $ZipPath -BaseDir $repoRoot
        }
        $null = Assert-AdoLoopWritableOutputPath -Path $zipPath -RepoRoot $repoRoot -ParamName '-ZipPath'

        if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
        Compress-Archive -Path (Join-Path $distAbs '*') -DestinationPath $zipPath -CompressionLevel Optimal -Force

        $zipBytes = [long](Get-Item -LiteralPath $zipPath).Length
        $zipSha = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        Write-Kv 'zip 路径' $zipPath
        Write-Kv 'zip 大小' ("{0:N0} 字节（{1}）" -f $zipBytes, (Format-AdoLoopSize $zipBytes))
        Write-Kv 'zip sha256' $zipSha
        Write-Ok 'zip 生成成功'
    }

    Write-Host ''
    Write-Ok '打包完成'
    exit 0
} catch {
    Write-Host ''
    Write-Fail $_.Exception.Message
    if ($_.ScriptStackTrace) { Write-Verbose $_.ScriptStackTrace }
    Write-Host ''
    Write-Host '打包失败。可用 -Verbose 查看详细执行过程。' -ForegroundColor Red
    exit 1
}
