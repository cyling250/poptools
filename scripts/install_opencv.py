# -*- coding: utf-8 -*-
"""下载 OpenCV 4.10.0 (Windows x64 官方 prebuilt) 到 tools/opencv，构建时不依赖全局安装。

创建时间：2026-08-23
用途：为「eSearch 窗口识别算法移植」提供 OpenCV。算法只需要 imgproc(imgproc 在
      opencv_world 动态库内)，CMake 用 find_package(OpenCV) 引入。
依赖：.venv（uv 创建）、curl.exe、tar.exe（Windows 自带 bsdtar，可解 7z 自解包包）
数据来源：https://github.com/opencv/opencv/releases （境外，走 127.0.0.1:7897 代理）

流程：
  1. 若 .venv 不存在则用 uv 创建，并切换到 .venv 里的 python 重新执行本脚本
  2. 已存在 tools/opencv/opencv/build/OpenCVConfig.cmake 则直接跳过
  3. 经 127.0.0.1:7897 代理下载 opencv-<VERSION>-windows.exe（约 183MB）
  4. 用 tar.exe（bsdtar）解压，得到 tools/opencv/opencv
  5. 校验后打印 CMake 需要的 OpenCV_ROOT
"""
import os
import shutil
import subprocess
import sys
import urllib.request

OPENCV_VERSION = "4.10.0"
# 官方 Windows 发布：可链接 x64/vc16 库，内含 opencv_world 动态库
URL = (
    f"https://github.com/opencv/opencv/releases/download/{OPENCV_VERSION}"
    f"/opencv-{OPENCV_VERSION}-windows.exe"
)
PROXY = "http://127.0.0.1:7897"
EXE_NAME = f"opencv-{OPENCV_VERSION}-windows.exe"

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
VENV = os.path.join(ROOT, ".venv")
VENV_PY = os.path.join(VENV, "Scripts", "python.exe")
TOOLS_OPENCV = os.path.join(ROOT, "tools", "opencv")
DL_DIR = os.path.join(TOOLS_OPENCV, "downloads")  # 下载缓存（exe），解压后保留
# 官方自解压包解出来是一层 opencv/ 目录
OPENCV_ROOT = os.path.join(TOOLS_OPENCV, "opencv")
CONFIG_CMAKE = os.path.join(OPENCV_ROOT, "build", "OpenCVConfig.cmake")


def proxy_opener():
    handler = urllib.request.ProxyHandler(
        {"http": PROXY, "https": PROXY})
    return urllib.request.build_opener(handler)


def running_in_venv():
    return os.path.normcase(sys.prefix) == os.path.normcase(VENV)


def create_venv_if_missing():
    if not os.path.exists(VENV_PY):
        print("creating .venv via uv ...")
        subprocess.check_call(["uv", "venv", ".venv"], cwd=ROOT)


def find_tool(name: str) -> str:
    """在 PATH 及各标准位置定位工具绝对路径。"""
    found = shutil.which(name)
    if found:
        return found
    for cand in (
        os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", name),
        rf"C:\Program Files\7-Zip\{name}.exe",
        rf"C:\Program Files (x86)\7-Zip\{name}.exe",
    ):
        if os.path.exists(cand):
            return cand
    return ""


def download():
    """走代理下载官方自解压包。"""
    os.makedirs(DL_DIR, exist_ok=True)
    dst = os.path.join(DL_DIR, EXE_NAME)
    if os.path.exists(dst) and os.path.getsize(dst) > 100 * 1024 * 1024:
        print(f"已存在 {dst}，跳过下载")
        return dst

    print(f"downloading {URL} -> {dst}")
    print(f"proxy: {PROXY}")
    opener = proxy_opener()
    try:
        with opener.open(URL, timeout=120) as resp, open(dst, "wb") as f:
            shutil.copyfileobj(resp, f)
    except Exception as e:  # noqa: BLE001 保留现场文件，便于排查
        print(f"下载失败：{e}\n现场文件：{dst}\n请确认代理 127.0.0.1:7897 已开启", file=sys.stderr)
        raise
    if not (os.path.getsize(dst) > 100 * 1024 * 1024):
        sys.exit(f"{dst} 下载不完整，请删除后重跑")
    return dst


def extract(exe: str):
    """用 bsdtar 解压 7z 自解压包。"""
    if os.path.exists(CONFIG_CMAKE):
        print(f"OpenCV 已解压：{OPENCV_ROOT}，跳过")
        return
    tar = find_tool("tar.exe")
    if not tar:
        sys.exit("未找到 tar.exe（Windows 自带 bsdtar），无法解压 OpenCV 自解压包")
    os.makedirs(TOOLS_OPENCV, exist_ok=True)
    print(f"extracting with {tar} ...")
    r = subprocess.run([tar, "-xf", exe, "-C", TOOLS_OPENCV],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"tar 解压失败：\n{r.stderr or r.stdout}\n"
                 f"可用 7-Zip 手动解压 {exe} 到 {TOOLS_OPENCV}")
    if not os.path.exists(CONFIG_CMAKE):
        sys.exit(f"解压完成但未找到 {CONFIG_CMAKE}，请人工检查 {TOOLS_OPENCV}")


def main():
    if not running_in_venv():
        create_venv_if_missing()
        os.execv(VENV_PY, [VENV_PY, os.path.abspath(__file__)])
        return  # 不会到达这里
    if os.path.exists(CONFIG_CMAKE):
        print(f"OpenCV 已就绪：{OPENCV_ROOT}")
        print("CMake 将默认使用 OPENCV_ROOT = " + OPENCV_ROOT.replace("/", "\\"))
        return
    exe = download()
    extract(exe)
    print(f"done: OpenCV {OPENCV_VERSION} -> {OPENCV_ROOT}")
    print("CMake 将默认使用 OPENCV_ROOT = " + OPENCV_ROOT.replace("/", "\\"))


if __name__ == "__main__":
    try:
        main()
    except Exception as e:  # noqa: BLE001
        print(f"[install_opencv] 执行失败：{e}", file=sys.stderr)
        raise