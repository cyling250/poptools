# -*- coding: utf-8 -*-
"""下载 Tesseract 5.5.3 (UB-Mannheim Windows x64) 到 tools/tesseract，全程免安装。

流程：
  1. 经 7897 代理下载官方 NSIS 安装器到 temp_scripts/out
  2. 用 7-Zip 便携版（Windows 自带 msiexec /a 从官方 MSI 解包，不装系统）解压安装器
     → tools/tesseract（NSIS 格式 RAR 不支持，7-Zip 是正解）
  3. 清理安装器残留（$PLUGINSDIR / uninstall* 等）
  4. 补齐 chi_sim / eng 语言包（从 tessdata_fast），裁剪多余语言包

产物布局（与 OcrService / CMake POST_BUILD 约定一致）：
  tools/tesseract/libtesseract-5.dll   （OcrService 运行时 LoadLibrary 加载）
  tools/tesseract/*.dll                （libleptonica 等 MSYS2 依赖，须同目录）
  tools/tesseract/tessdata/chi_sim.traineddata / eng.traineddata
"""
import glob
import os
import shutil
import subprocess
import sys
import urllib.request

VERSION = "5.5.3"
ASSET = f"tesseract-ocr-w64-setup-{VERSION}.20260724.exe"
URL = f"https://github.com/tesseract-ocr/tesseract/releases/download/{VERSION}/{ASSET}"
# 语言包（app 固定 -l chi_sim+eng；安装器默认不含语言包，需补齐）
LANGS = ["eng", "chi_sim"]
TESSDATA_FAST = "https://github.com/tesseract-ocr/tessdata_fast/raw/main"
# 7-Zip 便携版（仅解 NSIS 用，解包后也可删除）
ZIP7_VERSION = "26.02"
ZIP7_MSI = f"7z{ZIP7_VERSION.replace('.', '')}-x64.msi"
ZIP7_URL = f"https://github.com/ip7z/7zip/releases/download/{ZIP7_VERSION}/{ZIP7_MSI}"

PROXY = "http://127.0.0.1:7897"

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
# 中间产物与便携工具统一放 temp_scripts（规则：产物 -> temp_scripts/out，工具 -> temp_scripts/tool）
TEMP = os.path.join(ROOT, "temp_scripts")
OUT = os.path.join(TEMP, "out")
TOOLS_TESS = os.path.join(ROOT, "tools", "tesseract")
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
    msi = os.path.join(OUT, ZIP7_MSI)
    if not os.path.exists(msi):
        print(f"downloading 7-Zip MSI ...")
        download(ZIP7_URL, msi)
    # msiexec /a 管理式解包：仅把 MSI 内文件解到目标目录，不写注册表、不安装
    os.makedirs(TOOLS_7ZIP, exist_ok=True)
    subprocess.run(["msiexec", "/a", msi, "/qn", f"TARGETDIR={TOOLS_7ZIP}"],
                   check=True, timeout=300)
    exe = find_7z()
    if not exe:
        sys.exit("7-Zip 解包后未找到 7z.exe，请检查 msiexec /a 输出")
    return exe


def portable_extract(installer):
    os.makedirs(TOOLS_TESS, exist_ok=True)
    exe = setup_7z()
    print("extracting with:", exe)
    subprocess.run([exe, "x", "-y", f"-o{TOOLS_TESS}", installer],
                   check=True, timeout=600)
    # 清理安装器残留（与 OCR 引擎无关）
    plugins = os.path.join(TOOLS_TESS, "$PLUGINSDIR")
    if os.path.isdir(plugins):
        shutil.rmtree(plugins)
    for pat in ("uninstall*", "*uninst*"):
        for f in glob.glob(os.path.join(TOOLS_TESS, pat)):
            try:
                os.remove(f)
            except OSError:
                pass


def ensure_languages():
    tessdata = os.path.join(TOOLS_TESS, "tessdata")
    os.makedirs(tessdata, exist_ok=True)
    for lang in LANGS:
        dest = os.path.join(tessdata, f"{lang}.traineddata")
        if os.path.exists(dest):
            continue
        print(f"downloading language pack {lang} ...")
        download(f"{TESSDATA_FAST}/{lang}.traineddata", dest)
    # 裁剪多余语言包：仅保留本应用使用的中英文（configs/ 目录保留）
    keep = {f"{l}.traineddata" for l in LANGS}
    for f in glob.glob(os.path.join(tessdata, "*.traineddata")):
        if os.path.basename(f) not in keep:
            os.remove(f)


def main():
    dll = os.path.join(TOOLS_TESS, "libtesseract-5.dll")
    if not os.path.exists(dll):
        os.makedirs(OUT, exist_ok=True)
        installer = os.path.join(OUT, ASSET)
        if not os.path.exists(installer) or os.path.getsize(installer) < 20_000_000:
            print(f"downloading {ASSET} ...")
            download(URL, installer)
        else:
            print("installer already exists, skip download")
        portable_extract(installer)

    ensure_languages()

    if not os.path.exists(dll):
        sys.exit(f"解压失败：{dll} 不存在")
    print(f"done: {TOOLS_TESS}")
    print("OcrService 将运行时 LoadLibrary 加载 libtesseract-5.dll（进程内调用，无外挂 exe）")


if __name__ == "__main__":
    main()
