#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""日语分词桥接脚本（可选增强）：供 AdoLoop 的 core/Tokenizer 以子进程调用。

依赖（任选其一，缺失则输出 error，AdoLoop 自动降级回内置启发式分词）：
    pip install janome          # 纯 Python，安装最省事
    pip install fugashi[unidic-lite]

协议：stdin 逐行 JSON 请求，stdout 逐行 JSON 响应（与 faster_whisper_bridge.py 同风格）
    请求： {"text": "私は学生です"}
    响应： {"tokens": [ {"surface":"私","reading":"わたし","lemma":"私","begin":0}, ... ]}
    失败： {"error": "未安装 janome/fugashi：pip install janome"}

说明：
    - reading 统一转平假名（janome/fugashi 原生为片假名），与有道词典的假名风格一致；
    - begin 为该词在原文中的字符偏移（Python 以码点计数，与 Qt 的 UTF-16 码元在
      BMP 内一致；本脚本输出的 surface 由 AdoLoop 侧再做一次 indexOf 校验与兜底）；
    - 标点/空白也作为 token 输出，由 AdoLoop 侧决定是否丢弃。

用法（一般不用手工调用，由程序自动拉起）：
    python japanese_tokenizer_bridge.py
"""
import json
import sys

_TOKENIZER = None
_TOKENIZER_NAME = ""
_IMPORT_ERROR = ""


def _katakana_to_hiragana(s):
    out = []
    for ch in s:
        code = ord(ch)
        if 0x30A1 <= code <= 0x30F6:
            out.append(chr(code - 0x60))
        else:
            out.append(ch)
    return "".join(out)


def _init_tokenizer():
    """惰性初始化：优先 janome，其次 fugashi。"""
    global _TOKENIZER, _TOKENIZER_NAME, _IMPORT_ERROR
    if _TOKENIZER is not None or _IMPORT_ERROR:
        return

    try:
        from janome.tokenizer import Tokenizer  # type: ignore

        _TOKENIZER = Tokenizer()
        _TOKENIZER_NAME = "janome"
        return
    except Exception as exc:  # noqa: BLE001
        janome_err = str(exc)

    try:
        import fugashi  # type: ignore

        _TOKENIZER = fugashi.Tagger()
        _TOKENIZER_NAME = "fugashi"
        return
    except Exception as exc:  # noqa: BLE001
        _IMPORT_ERROR = (
            "未安装 janome/fugashi 分词库（pip install janome）。"
            f"janome: {janome_err} / fugashi: {exc}"
        )


def _tokens_janome(text):
    toks = []
    begin = 0
    for token in _TOKENIZER.tokenize(text):
        surface = token.surface
        feats = token.extra if isinstance(token.extra, dict) else {}
        reading = feats.get("reading") or ""
        if not reading:
            # janome 的 parts: 品詞,品詞細分類1,品詞細分類2,品詞細分類3,活用型,活用形,原形,読み,発音
            try:
                reading = token.part_of_speech.split(",")[7]
            except Exception:  # noqa: BLE001
                reading = ""
        if reading in ("*", ""):
            reading = surface
        lemma = token.base_form if token.base_form and token.base_form != "*" else surface
        toks.append({
            "surface": surface,
            "reading": _katakana_to_hiragana(reading),
            "lemma": lemma,
            "begin": begin,
        })
        begin += len(surface)
    return toks


def _tokens_fugashi(text):
    toks = []
    begin = 0
    for word in _TOKENIZER(text):
        surface = word.surface
        feat = word.feature
        kana = getattr(feat, "kana", None) or getattr(feat, "pron", None) or surface
        lemma = getattr(feat, "lemma", None) or surface
        toks.append({
            "surface": surface,
            "reading": _katakana_to_hiragana(kana),
            "lemma": lemma,
            "begin": begin,
        })
        begin += len(surface)
    return toks


def tokenize(text):
    _init_tokenizer()
    if _IMPORT_ERROR:
        return {"error": _IMPORT_ERROR}
    try:
        if _TOKENIZER_NAME == "janome":
            return {"tokens": _tokens_janome(text)}
        return {"tokens": _tokens_fugashi(text)}
    except Exception as exc:  # noqa: BLE001
        return {"error": str(exc)}


def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main():
    if hasattr(sys.stdin, "reconfigure"):
        try:
            sys.stdin.reconfigure(encoding="utf-8")
        except Exception:  # noqa: BLE001
            pass
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception:  # noqa: BLE001
            emit({"error": "请求不是合法 JSON"})
            continue
        text = req.get("text", "")
        if not text:
            emit({"tokens": []})
            continue
        emit(tokenize(text))
    return 0


if __name__ == "__main__":
    sys.exit(main())
