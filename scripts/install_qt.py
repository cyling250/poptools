# -*- coding: utf-8 -*-
"""下载 Qt 6.8.3 (MSVC2022 x64) 套件到 tools/qt，构建时不依赖全局 Qt 安装。

流程：
  1. 若 .venv 不存在则用 uv 创建（uv 创建后自带管理解释器，无 pip，用 uv pip 安装依赖）
  2. 若 aqtinstall 未安装则 `uv pip install --python .venv` 安装（经 7897 代理）
  3. 以子进程调用 `python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64`
     - 仅下载本工程所需模块：qtbase(Core/Gui/Widgets/Network) qtsvg(Svg) qttools(windeployqt)
     - 经 127.0.0.1:7897 代理访问境外官方仓库
  4. 完成后 tools/qt/6.8.3/msvc2022_64 作为 CMake 的 CMAKE_PREFIX_PATH，
     构建时 windeployqt 把运行库自动部署到 Release 输出目录
"""
import os
import subprocess
import sys

QT_VERSION = "6.8.3"
# aqt 架构名：win64_msvc2022_64 的安装目录名为 msvc2022_64，统一用短名保证路径一致
QT_KIT = "msvc2022_64"
# 仅下载构建/运行所需模块，避免整套 Qt（约 2GB）
ARCHIVES = ["qtbase", "qtsvg", "qttools"]
PROXY = "http://127.0.0.1:7897"

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
VENV = os.path.join(ROOT, ".venv")
VENV_PY = os.path.join(VENV, "Scripts", "python.exe")
TOOLS_QT = os.path.join(ROOT, "tools", "qt")
KIT_DIR = os.path.join(TOOLS_QT, QT_VERSION, QT_KIT)


def proxy_env():
    env = os.environ.copy()
    env.setdefault("HTTP_PROXY", PROXY)
    env.setdefault("HTTPS_PROXY", PROXY)
    return env


def running_in_venv():
    return os.path.normcase(sys.prefix) == os.path.normcase(VENV)


def create_venv_if_missing():
    if not os.path.exists(VENV_PY):
        print("creating .venv via uv ...")
        subprocess.check_call(["uv", "venv", ".venv"], cwd=ROOT, env=proxy_env())


def ensure_aqtinstall():
    """确保当前解释器（venv）里装有 aqtinstall。"""
    check = subprocess.run([sys.executable, "-c", "import aqt"],
                           capture_output=True, text=True)
    if check.returncode == 0:
        return
    print("installing aqtinstall into .venv ...")
    # uv 创建的 venv 默认无 pip，统一用 uv pip 安装
    subprocess.check_call(["uv", "pip", "install", "--python", VENV_PY, "-q", "aqtinstall"],
                          cwd=ROOT, env=proxy_env())
    subprocess.check_call([VENV_PY, "-c", "import aqt"], env=proxy_env())


def download():
    windeploy = os.path.join(KIT_DIR, "bin", "windeployqt.exe")
    if os.path.exists(windeploy):
        print(f"Qt {QT_VERSION} ({QT_KIT}) 已存在于 {KIT_DIR}，跳过下载")
        return

    os.makedirs(TOOLS_QT, exist_ok=True)
    cmd = [sys.executable, "-m", "aqt", "install-qt",
           "windows", "desktop", QT_VERSION, QT_KIT,
           "-O", TOOLS_QT,
           "--archives"] + ARCHIVES + \
          ["--timeout", "120"]
    print("running:", " ".join(cmd))
    print("proxy:", PROXY)
    subprocess.check_call(cmd, env=proxy_env())

    if not os.path.exists(windeploy):
        sys.exit(f"下载完成但未找到 {windeploy}，请检查 aqt 输出")
    print(f"done: Qt {QT_VERSION} ({QT_KIT}) -> {KIT_DIR}")
    print("CMake 将默认使用 CMAKE_PREFIX_PATH = " + KIT_DIR.replace("/", "\\"))


def main():
    if not running_in_venv():
        # 以系统/管理解释器启动：先确保 .venv 存在，再切换进去重新执行
        create_venv_if_missing()
        os.execv(VENV_PY, [VENV_PY, os.path.abspath(__file__)])
        return  # 不会到达这里（execv 已替换进程）
    ensure_aqtinstall()
    download()


if __name__ == "__main__":
    main()
