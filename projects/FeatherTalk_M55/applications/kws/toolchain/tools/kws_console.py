"""kws_console.py — 把 stdout/stderr 钉在 UTF-8 上。

为什么需要：Windows 下 stdout 被重定向到文件时按本地代码页（GBK）编码，报表里只要出现
GBK 没有的字符（⇒、✓、✗）就抛 UnicodeEncodeError。10-19 的板上矩阵跑了 21 分钟，
数据全在，只在**最后一行汇总**上崩掉，JSON 整个丢了；map_size_report.py 同样栽在 ✓。
编码问题不该由每条 print 语句小心，这里一次性封掉。
"""
import sys


def force_utf8():
    for name in ("stdout", "stderr"):
        s = getattr(sys, name, None)
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass      # 已被上层重定向成非 TextIOWrapper（如 pytest 捕获）时不动它
