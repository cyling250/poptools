# -*- coding: utf-8 -*-
"""下载 RapidOCR-json v1.1.0 到 tools/rapidocr（二维码之外的 OCR 引擎，替代 PaddleOCR-json）。

背景：PaddleOCR-json 全栈 297MB（mklml 89 + opencv 62 + paddle_inference 49...），
是安装包 105.7MB 的 91%。RapidOCR-json（同作者 hiroi-sora）为 onnxruntime 静态
单文件（16.8MB，零 DLL 依赖），管道 JSON 协议与 PaddleOCR-json 完全同构
（{code:100,data:[{box,score,text}]}，101=无文字，已实测验证）。
语言裁剪（T-041）：RapidOcrService 启动参数固定简中 v3 配置，仅额外保留英文 rec；
cht/japan/korean/cyrillic 四套 rec 模型（约 40MB）解压后即删除，解包约 55MB。

流程：
  1. 经 7897 代理下载 RapidOCR-json_v0.2.0.7z（GitHub Release，70MB）
  2. 7-Zip 便携版解压（与 install_paddleocr.py 共用 temp_scripts/tool/7zip）
  3. RapidOCR-json_v0.2.0/ 内容平铺到 tools/rapidocr
  4. 裁剪未随服务使用的 rec 模型（对已有安装同样生效，幂等）

产物布局：
  tools/rapidocr/RapidOCR-json.exe
  tools/rapidocr/models/  （det/cls/rec onnx + 字典 + configs.txt；中/英 rec）
"""
import io
import os
import shutil
import subprocess
import sys
import urllib.request

VERSION = "v0.2.0"
URL = f"https://github.com/hiroi-sora/RapidOCR-json/releases/download/{VERSION}/RapidOCR-json_{VERSION}.7z"
PROXY = "http://127.0.0.1:7897"

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
TEMP = os.path.join(ROOT, "temp_scripts")
OUT = os.path.join(TEMP, "out")
TOOLS_RAPID = os.path.join(ROOT, "tools", "rapidocr")
TOOLS_7ZIP = os.path.join(TEMP, "tool", "7zip")
ZIP7_VERSION = "24.09"
ZIP7_MSI = f"7z{ZIP7_VERSION.replace('.', '')}-x64.msi"
ZIP7_URL = f"https://github.com/ip7z/7zip/releases/download/{ZIP7_VERSION}/{ZIP7_MSI}"

# 语言裁剪：服务启动参数固定简中 v3 配置（det/cls/rec v3 + dict_chinese，见
# RapidOcrService），仅额外保留英文 rec；以下四套 rec 模型从未被引擎加载，
# 解压后删除（恢复多语言 = 移除本表后重跑本脚本，或单独下载对应 onnx）
TRIM_REC_MODELS = [
    "rec_chinese_cht_PP-OCRv3_infer.onnx",
    "rec_japan_PP-OCRv3_infer.onnx",
    "rec_korean_PP-OCRv3_infer.onnx",
    "rec_cyrillic_PP-OCRv3_infer.onnx",
]


def proxy_env():
    env = os.environ.copy()
    env.setdefault("HTTP_PROXY", PROXY)
    env.setdefault("HTTPS_PROXY", PROXY)
    return env


def download(url, dest):
    if os.path.exists(dest):
        print("cached:", dest)
        return
    print("downloading:", url)
    proxy = urllib.request.ProxyHandler({"http": PROXY, "https": PROXY})
    opener = urllib.request.build_opener(proxy)
    with opener.open(url, timeout=300) as resp, io.open(dest, "wb") as f:
        f.write(resp.read())
    print("saved:", dest, os.path.getsize(dest), "bytes")


def ensure_7zip():
    exe = os.path.join(TOOLS_7ZIP, "Files", "7-Zip", "7z.exe")
    if os.path.exists(exe):
        return exe
    msi = os.path.join(OUT, ZIP7_MSI)
    download(ZIP7_URL, msi)
    os.makedirs(TOOLS_7ZIP, exist_ok=True)
    subprocess.check_call(["msiexec", "/a", msi, "/qn",
                           f"TARGETDIR={os.path.join(TOOLS_7ZIP, 'Files')}"])
    if not os.path.exists(exe):
        sys.exit(f"7-Zip 便携版解压失败：{exe} 不存在")
    return exe


def trim_unused_rec_models():
    models = os.path.join(TOOLS_RAPID, "models")
    removed = 0
    for name in TRIM_REC_MODELS:
        path = os.path.join(models, name)
        if os.path.exists(path):
            os.remove(path)
            removed += 1
    if removed:
        print(f"trim: 已删除 {removed} 套未使用的多语言 rec 模型（cht/japan/korean/cyrillic）")
    else:
        print("trim: 多语言 rec 模型已裁剪，无需处理")


def main():
    marker = os.path.join(TOOLS_RAPID, "RapidOCR-json.exe")
    if os.path.exists(marker):
        print(f"RapidOCR-json 已存在于 {TOOLS_RAPID}，跳过下载")
    else:
        os.makedirs(OUT, exist_ok=True)
        arc = os.path.join(OUT, f"RapidOCR-json_{VERSION}.7z")
        download(URL, arc)
        sz7 = ensure_7zip()

        extract_dir = os.path.join(OUT, "rapidocr-extract")
        if os.path.exists(extract_dir):
            shutil.rmtree(extract_dir)
        subprocess.check_call([sz7, "x", arc, f"-o{extract_dir}", "-y"],
                              stdout=subprocess.DEVNULL)
        inner = os.path.join(extract_dir, f"RapidOCR-json_{VERSION}")
        if not os.path.exists(os.path.join(inner, "RapidOCR-json.exe")):
            sys.exit(f"解压产物异常：{inner}/RapidOCR-json.exe 不存在")

        os.makedirs(os.path.dirname(TOOLS_RAPID), exist_ok=True)
        shutil.move(inner, TOOLS_RAPID)
        shutil.rmtree(extract_dir, ignore_errors=True)
        print(f"done: RapidOCR-json {VERSION} -> {TOOLS_RAPID}")

    trim_unused_rec_models()


if __name__ == "__main__":
    main()
