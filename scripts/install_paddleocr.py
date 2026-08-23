# -*- coding: utf-8 -*-
"""下载 PaddleOCR-json v1.4.1 到 tools/paddleocr，免安装、解压即用。

流程：
  1. 经 7897 代理下载 GitHub Release 压缩包到 temp_scripts/out
  2. 用 7-Zip 便携版解压到 tools/paddleocr
  3. 裁剪多余语言包（仅保留 chi_sim, en, 繁中）

产物布局（与 CMake POST_BUILD / PaddleOcrService 约定一致）：
  tools/paddleocr/PaddleOCR-json.exe
  tools/paddleocr/models/ch_PP-OCRv3_det_infer/
  tools/paddleocr/models/ch_PP-OCRv3_rec_infer/
  tools/paddleocr/models/ppocr_keys_v1.txt
  ...
"""
import glob
import os
import shutil
import subprocess
import sys
import urllib.request

VERSION = "1.4.1"
ASSET = f"PaddleOCR-json_v{VERSION}_windows_x64.7z"
URL = f"https://github.com/hiroi-sora/PaddleOCR-json/releases/download/v{VERSION}/{ASSET}"

PROXY = "http://127.0.0.1:7897"

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
TEMP = os.path.join(ROOT, "temp_scripts")
OUT = os.path.join(TEMP, "out")
TOOLS_PPOCR = os.path.join(ROOT, "tools", "paddleocr")
TOOLS_7ZIP = os.path.join(TEMP, "tool", "7zip")


def proxy_env():
    env = os.environ.copy()
    env.setdefault("HTTP_PROXY", PROXY)
    env.setdefault("HTTPS_PROXY", PROXY)
    return env


def download(url, dest, timeout=30):
    opener = urllib.request.build_opener(
        urllib.request.ProxyHandler({"http": PROXY, "https": PROXY}))
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    with opener.open(req, timeout=timeout) as r, open(dest, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            done += len(chunk)
            pct = done * 100 // total if total else 0
            print(f"\rdownloading {done//(1<<20)}MB / {total//(1<<20)}MB ({pct}%)", end="", flush=True)
    print()
    return os.path.getsize(dest)


def find_7z():
    """定位便携 7z.exe：tools/7zip 下 Files/7-Zip 或 7-Zip 目录。"""
    for sub in ("Files", os.path.join("Files", "7-Zip"), "7-Zip", ""):
        exe = os.path.join(TOOLS_7ZIP, sub, "7z.exe")
        if os.path.exists(exe):
            return exe
    return None


def setup_7z():
    exe = find_7z()
    if exe:
        return exe
    os.makedirs(OUT, exist_ok=True)
    ZIP7_VERSION = "26.02"
    ZIP7_MSI = f"7z{ZIP7_VERSION.replace('.', '')}-x64.msi"
    ZIP7_URL = f"https://github.com/ip7z/7zip/releases/download/{ZIP7_VERSION}/{ZIP7_MSI}"
    msi = os.path.join(OUT, ZIP7_MSI)
    if not os.path.exists(msi):
        print(f"downloading 7-Zip MSI ...")
        download(ZIP7_URL, msi)
    os.makedirs(TOOLS_7ZIP, exist_ok=True)
    subprocess.run(["msiexec", "/a", msi, "/qn", f"TARGETDIR={TOOLS_7ZIP}"],
                   check=True, timeout=300)
    exe = find_7z()
    if not exe:
        sys.exit("7-Zip 解包后未找到 7z.exe，请检查 msiexec /a 输出")
    return exe


def main():
    exe = os.path.join(TOOLS_PPOCR, "PaddleOCR-json.exe")
    if os.path.exists(exe):
        print(f"PaddleOCR-json already exists at {TOOLS_PPOCR}, skip download")
        return

    os.makedirs(OUT, exist_ok=True)
    os.makedirs(TOOLS_PPOCR, exist_ok=True)

    archive = os.path.join(OUT, ASSET)
    if not os.path.exists(archive) or os.path.getsize(archive) < 80_000_000:
        print(f"downloading {ASSET} ...")
        download(URL, archive)
    else:
        print("archive already exists, skip download")

    exe7z = setup_7z()
    print("extracting with:", exe7z)
    subprocess.run([exe7z, "x", "-y", f"-o{TOOLS_PPOCR}", archive],
                   check=True, timeout=600)

    # 解压后可能多一层目录（如 PaddleOCR-json_v1.4.1_windows_x64/），把内容移上来
    subdirs = [d for d in os.listdir(TOOLS_PPOCR)
               if os.path.isdir(os.path.join(TOOLS_PPOCR, d))]
    if subdirs:
        # 取第一个子目录（通常只有一个）
        src = os.path.join(TOOLS_PPOCR, subdirs[0])
        for item in os.listdir(src):
            shutil.move(os.path.join(src, item), os.path.join(TOOLS_PPOCR, item))
        os.rmdir(src)

    if not os.path.exists(exe):
        sys.exit(f"解压失败：{exe} 不存在")

    print(f"done: {TOOLS_PPOCR}")
    print(f"PaddleOcrService 将通过 QProcess 子进程调用 PaddleOCR-json.exe（管道模式，常驻进程）")


if __name__ == "__main__":
    main()