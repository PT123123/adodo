# 07 ASR 引擎集成

## 1. 抽象与双引擎

```
AsrEngine（接口）
 ├── start(media, Options) / cancel / isRunning
 ├── isAvailable(reason*)
 └── 信号：progress(fraction, stage) / finished(AsrSegment[]) / failed(err)
```

| 引擎 | 通道 | 依赖 | 备注 |
| --- | --- | --- | --- |
| WhisperCppAsr | whisper-cli 子进程 | whisper.cpp 构建产物 + 模型文件 | `-oj -of base -s` 输出 JSON；进度走 stderr（v1 不细粒度解析，发一次起始进度） |
| FasterWhisperAsr | python 桥子进程 | python3 + faster-whisper | 桥协议见下，进度逐段上报 |

## 2. faster-whisper 桥协议（stdout 逐行 JSON）

```
{"progress": 0.42}
{"result": [{"start":1200,"end":3400,"text":"Hello world.","words":[{"word":"Hello","start":1200,"end":1600}]}]}
```

桥脚本：`scripts/faster_whisper_bridge.py`（自动选择 CPU/CUDA、VAD 过滤、词级时间戳）。

## 3. AsrTaskManager（队列 + 缓存）

- 单任务串行；`transcribe()` 返回 taskId，进度/结果按 taskId 回传。
- **缓存**：`<cacheDir>/asr-<sha1(media)>.json`，二次打开同一文件直接命中，
  不重复转写。
- 取消：队列任务直接移除；运行中任务 kill 进程并走统一失败流程。
- 引擎不可用（缺 whisper-cli / python）→ 明确错误提示，不影响其他功能。

## 4. 词级时间戳的意义

- 抠词听写：自动复读单词区间。
- 实时字幕取词：当前词高亮。
- 弹幕复习：生词在其所在词/句播放时发射。
- 原句闪卡：挖空词对应区间可复听。

whisper.cpp 无词时间戳时降级为 `Sentence::estimatedWords()` 字符占比插值。

## 5. 待环境验证项

- whisper-cli 参数 `-s` 在目标版本的输出 JSON 是否含 `words`（不影响流程，仅影响精度）。
- faster-whisper 桥在无 torch 环境的 CPU 回退。
