# -*- coding: utf-8 -*-
"""启动/内存性能基线采样（发版流程用，防劣化有据可查）。

流程：
  1. 结束现有 PopTools.exe / RapidOCR-json.exe（引擎迁移前旧名 PaddleOCR-json.exe
     也一并清理；/T 杀进程树，进程不存在时静默跳过）
  2. 启动 build/Release/PopTools.exe，WaitForInputIdle 等待托盘就绪，计启动耗时
  3. 稳定 2 秒后采样主进程工作集内存；若 OCR 子进程已拉起则一并列出
  4. 结果打印为 JSON 并追加到 temp_scripts/out/perf_baseline.jsonl（一行一条）

用法：
  python scripts/perf_baseline.py [--exe 路径] [--jsonl 路径]

依赖：标准库（ctypes/subprocess），无第三方；.venv 或任意 Python 3.8+ 可跑。
数字解读：启动耗时正常百毫秒级；主进程内存 ~35MB 量级；OCR 子进程 ~775MB
（RapidOCR-json.exe，懒启动，未用过 OCR 时为 0）。连续两次运行取第二次为温启动参考。
"""
import argparse
import ctypes
import json
import os
import subprocess
import sys
import time
from ctypes import wintypes
from datetime import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_EXE = os.path.join(ROOT, "build", "Release", "PopTools.exe")
DEFAULT_JSONL = os.path.join(ROOT, "temp_scripts", "out", "perf_baseline.jsonl")

kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi
user32 = ctypes.windll.user32


class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("PageFaultCount", wintypes.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]


def kill_existing():
    """结束现有实例（冷启动基线需要；P-008：主进程与 OCR 子进程都要杀）。
    R5-D1：引擎已迁移 RapidOCR-json.exe（RapidOcrService.cpp 启动名），
    只杀旧名会让上一会话的 OCR 子进程变孤儿（~770MB 常驻）跨轮累积；
    /T 杀进程树；旧名 PaddleOCR-json.exe 保留兜底清理。进程不存在时
    taskkill 非零退出，check=False 静默容错"""
    for name in ("PopTools.exe", "RapidOCR-json.exe", "PaddleOCR-json.exe"):
        subprocess.run(["taskkill", "/F", "/T", "/IM", name],
                       capture_output=True, check=False)
    time.sleep(0.6)


def working_set_mb(pid):
    """进程工作集内存（MB）；进程不存在返回 None"""
    h = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return None
    try:
        pmc = PROCESS_MEMORY_COUNTERS()
        pmc.cb = ctypes.sizeof(pmc)
        if psapi.GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb):
            return round(pmc.WorkingSetSize / (1024 * 1024), 1)
        return None
    finally:
        kernel32.CloseHandle(h)


def ocr_child_mb():
    """RapidOCR-json 子进程内存（存在则返回 MB，否则 0）——经 tasklist 查询。
    R5-D1：引擎迁移后原 PaddleOCR-json 查询恒 0.0，基线观测完全失效。
    中文 Windows 的 tasklist 输出是 GBK（见踩坑 P-020）：按 bytes 捕获后
    以 gbk + errors=replace 解码，不能用 text=True 的默认 UTF-8"""
    r = subprocess.run(["tasklist", "/FI", "IMAGENAME eq RapidOCR-json.exe",
                        "/FO", "CSV", "/NH"], capture_output=True)
    for line in (r.stdout or b"").decode("gbk", errors="replace").splitlines():
        if "RapidOCR-json.exe" in line:
            try:
                kb = int(line.split('","')[-1].replace('"', "")
                         .replace(".", "").replace(",", "").replace(" K", "").strip())
                return round(kb / 1024, 1)
            except (ValueError, IndexError):
                pass
    return 0.0


def main():
    ap = argparse.ArgumentParser(description="PopTools 启动/内存性能基线采样")
    ap.add_argument("--exe", default=DEFAULT_EXE, help="待测 exe 路径")
    ap.add_argument("--jsonl", default=DEFAULT_JSONL, help="结果追加文件（JSONL）")
    args = ap.parse_args()

    if not os.path.exists(args.exe):
        sys.exit(f"exe 不存在：{args.exe}（先构建 Release）")

    kill_existing()

    t0 = time.perf_counter()
    proc = subprocess.Popen([args.exe])
    # WaitForInputIdle：消息队列建立并空闲 ≈ 托盘就绪；超时（15s）记 -1
    rc = user32.WaitForInputIdle(int(proc._handle), 15000)
    startup_ms = round((time.perf_counter() - t0) * 1000) if rc == 0 else -1

    time.sleep(2.0)  # 稳定期：让 QSS/图标等首帧资源全部落地
    record = {
        "time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "startup_ms": startup_ms,
        "main_ws_mb": working_set_mb(proc.pid),
        "ocr_ws_mb": ocr_child_mb(),
        "exe_mb": round(os.path.getsize(args.exe) / (1024 * 1024), 2),
    }

    print(json.dumps(record, ensure_ascii=False))
    os.makedirs(os.path.dirname(args.jsonl), exist_ok=True)
    with open(args.jsonl, "a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\n")
    print(f"appended: {args.jsonl}")


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # 现场保留：报错不清进程，便于排查
        print("ERROR:", e, file=sys.stderr)
        raise
