# AdoLoop —— just 入口层（薄封装）
#
# 设计约定（与 docs/03-工程结构与构建.md 第 2 节一致）：
#   * 只做「入口 + 参数透传」：构建/运行/打包/清理的实现全部留在 scripts\*.ps1 里，
#     justfile 不复制任何构建逻辑；
#   * 命令一律交给 Windows PowerShell 执行，不调用 cmd.exe，不依赖任何 .bat / .cmd；
#   * 职责分离：`just build` 负责构建，`just run` 只运行已有产物（恒传 -SkipBuild），
#     产物不存在时直接以非零退出码报错，绝不回落到构建。
#
# 额外参数用 `--` 透传到底层脚本（`--` 可省略；just 会把它一起交给变长参数，命令里已剥掉）：
#   just build   -- -Config Debug -Jobs 4
#   just run     -- -Offscreen -TimeoutSec 5
#   just package -- -SkipBuild -Zip
# 注意：参数值里不要带空格（just 的变长参数按空格拼接，引号无法原样保留）。

# 命令交给 Windows PowerShell（不依赖默认 shell 恰好是 cmd 还是 sh）
set windows-shell := ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# 列出全部可用命令（直接执行 `just` 时的默认行为）
default:
	@& '{{just_executable()}}' --list --justfile '{{justfile()}}'

# 构建（默认 Release → build\src\AdoLoop.exe）；参数透传给 scripts\build.ps1
build *args:
	& '{{justfile_directory()}}/scripts/build.ps1' {{trim_start_matches(args, "--")}}; exit $LASTEXITCODE

# 只运行已有构建产物（build\src\AdoLoop.exe）——恒传 -SkipBuild，绝不触发构建
run *args:
	& '{{justfile_directory()}}/scripts/run.ps1' -SkipBuild {{trim_start_matches(args, "--")}}; exit $LASTEXITCODE

# 运行发行包产物（dist\AdoLoop\AdoLoop.exe）——不注入 Qt 路径，按「自包含」语义验证
run-dist *args:
	& '{{justfile_directory()}}/scripts/run.ps1' -SkipBuild -Exe '{{justfile_directory()}}/dist/AdoLoop/AdoLoop.exe' -NoQtPath {{trim_start_matches(args, "--")}}; exit $LASTEXITCODE

# 无窗口启动自检（offscreen，8 秒后仍存活即通过）——只运行，不构建
smoke *args:
	& '{{justfile_directory()}}/scripts/run.ps1' -SkipBuild -Offscreen -TimeoutSec 8 {{trim_start_matches(args, "--")}}; exit $LASTEXITCODE

# 打包便携发行包 dist\AdoLoop\（默认先构建；加 `-- -SkipBuild` 复用已有产物）
package *args:
	& '{{justfile_directory()}}/scripts/package.ps1' {{trim_start_matches(args, "--")}}; exit $LASTEXITCODE

# 清理生成目录 build\ 与 dist\（带路径安全校验）
clean *args:
	& '{{justfile_directory()}}/scripts/clean.ps1' {{trim_start_matches(args, "--")}}; exit $LASTEXITCODE

# 清理后全量重建（clean + build；额外参数只传给 build）
rebuild *args:
	& '{{justfile_directory()}}/scripts/clean.ps1'; exit $LASTEXITCODE
	& '{{justfile_directory()}}/scripts/build.ps1' {{trim_start_matches(args, "--")}}; exit $LASTEXITCODE
