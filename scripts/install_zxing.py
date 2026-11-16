# -*- coding: utf-8 -*-
"""下载 zxing-cpp 源码到 tools/zxing（二维码识别库，Apache-2.0）。

流程：
  1. 经 17890 代理从 GitHub 下载 zxing-cpp v2.2.1 源码 tar.gz（约 1.3MB）
  2. Python 标准库 tarfile 解压，把 zxing-cpp-2.2.1/ 内容移到 tools/zxing
  3. CMake 检测 tools/zxing/CMakeLists.txt 存在时以静态库编入
     （EXCLUDE_FROM_ALL，仅 poptools_core 链接），缺失时二维码功能
     编译为"不可用"，不阻断构建

产物布局：
  tools/zxing/CMakeLists.txt
  tools/zxing/core/src/...
"""
import io
import os
import shutil
import sys
import tarfile
import urllib.request

VERSION = "2.2.1"
URL = f"https://github.com/zxing-cpp/zxing-cpp/archive/refs/tags/v{VERSION}.tar.gz"
PROXY = "http://127.0.0.1:17890"

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
TEMP = os.path.join(ROOT, "temp_scripts")
OUT = os.path.join(TEMP, "out")
TOOLS_ZXING = os.path.join(ROOT, "tools", "zxing")


def main():
    if os.path.exists(os.path.join(TOOLS_ZXING, "CMakeLists.txt")):
        print(f"zxing-cpp 已存在于 {TOOLS_ZXING}，跳过下载")
        return

    os.makedirs(OUT, exist_ok=True)
    tgz = os.path.join(OUT, f"zxing-cpp-{VERSION}.tar.gz")
    if not os.path.exists(tgz):
        print("downloading:", URL)
        proxy = urllib.request.ProxyHandler({"http": PROXY, "https": PROXY})
        opener = urllib.request.build_opener(proxy)
        with opener.open(URL, timeout=120) as resp, io.open(tgz, "wb") as f:
            f.write(resp.read())
        print("saved:", tgz, os.path.getsize(tgz), "bytes")

    # 解压到临时目录，再把 zxing-cpp-<ver>/ 内容挪到 tools/zxing
    extract_dir = os.path.join(OUT, f"zxing-cpp-{VERSION}-extract")
    if os.path.exists(extract_dir):
        shutil.rmtree(extract_dir)
    os.makedirs(extract_dir)
    with tarfile.open(tgz, "r:gz") as t:
        t.extractall(extract_dir)

    inner = os.path.join(extract_dir, f"zxing-cpp-{VERSION}")
    if not os.path.exists(os.path.join(inner, "CMakeLists.txt")):
        sys.exit(f"解压产物异常：未找到 {inner}/CMakeLists.txt")

    os.makedirs(os.path.dirname(TOOLS_ZXING), exist_ok=True)
    shutil.move(inner, TOOLS_ZXING)
    shutil.rmtree(extract_dir, ignore_errors=True)
    print(f"done: zxing-cpp {VERSION} -> {TOOLS_ZXING}")


if __name__ == "__main__":
    main()
