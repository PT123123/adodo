#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""faster-whisper 桥接脚本：供 AdoLoop 的 FasterWhisperAsr 以子进程调用。

协议（stdout 逐行 JSON）：
    {"progress": 0.35}                       # 进度（0..1）
    {"result": [ {"start":ms,"end":ms,"text":str,
                  "words":[{"word":str,"start":ms,"end":ms}]} ]}   # 最终结果

用法:
    python faster_whisper_bridge.py <media> [--model small] [--language en]
            [--beam 5] [--cache DIR] [--device auto|cpu|cuda]
"""
import argparse
import json
import sys


def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("media")
    parser.add_argument("--model", default="small")
    parser.add_argument("--language", default=None)
    parser.add_argument("--beam", type=int, default=5)
    parser.add_argument("--cache", default="")
    parser.add_argument("--device", default="auto")
    args = parser.parse_args()

    try:
        from faster_whisper import WhisperModel
    except ImportError:
        emit({"error": "未安装 faster-whisper：pip install faster-whisper"})
        return 2

    try:
        device = "cpu"
        if args.device == "auto":
            try:
                import torch  # noqa: F401
                if torch.cuda.is_available():
                    device = "cuda"
            except Exception:
                device = "cpu"
        else:
            device = args.device

        model = WhisperModel(args.model, device=device, compute_type="auto")
        lang = args.language or None

        segments_iter, info = model.transcribe(
            args.media,
            language=lang,
            beam_size=args.beam,
            word_timestamps=True,
            vad_filter=True,
        )

        segments = []
        # faster-whisper 返回生成器，逐段消费以报告进度
        for seg in segments_iter:
            words = []
            if seg.words:
                for w in seg.words:
                    words.append({
                        "word": w.word,
                        "start": int(round(w.start * 1000)),
                        "end": int(round(w.end * 1000)),
                    })
            segments.append({
                "start": int(round(seg.start * 1000)),
                "end": int(round(seg.end * 1000)),
                "text": seg.text.strip(),
                "words": words,
            })
            if info.duration > 0:
                emit({"progress": min(1.0, seg.end / info.duration)})
            else:
                emit({"progress": 0.0})

        emit({"result": segments})
        return 0
    except Exception as exc:  # noqa: BLE001
        emit({"error": str(exc)})
        return 1


if __name__ == "__main__":
    sys.exit(main())
