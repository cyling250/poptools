#!/usr/bin/env python3
"""下载 WebView2 loader SDK（NuGet Microsoft.Web.WebView2）到 tools/webview2/。

用途：T-074 dsh 嵌入 M1——只取头文件与 x64 loader DLL，供 poptools_ui_dsh 编译
（C++ 侧经 QLibrary 动态加载 WebView2Loader.dll，免 .lib/CRT 匹配问题）。
幂等：tools/webview2/.installed-version 存在则跳过（--force 重装）。
用法：python scripts/install_webview2.py [--version <x.y.z>] [--force]
依赖：无第三方（urllib/zipfile 标准库）；nuget.org 属国外源，走本机代理 127.0.0.1:17890。
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

import urllib.request

PROXY = "http://127.0.0.1:17890"
INDEX_URL = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/index.json"
PACKAGE_URL = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/{version}/microsoft.web.webview2.{version}.nupkg"
TOOLS_DIR = Path(__file__).resolve().parent.parent / "tools" / "webview2"
MARKER = TOOLS_DIR / ".installed-version"

opener = urllib.request.build_opener(
    urllib.request.ProxyHandler({"http": PROXY, "https": PROXY}))


def pick_latest_stable(versions: list[str]) -> str:
    """取最新稳定版（剔除预发布后缀）。"""
    stable = [v for v in versions if not re.search(r"[a-zA-Z-]", v)]
    if not stable:
        raise RuntimeError("index.json 无稳定版本")
    stable.sort(key=lambda v: [int(x) for x in v.split(".")])
    return stable[-1]


def main() -> int:
    parser = argparse.ArgumentParser(description="安装 WebView2 loader SDK 到 tools/webview2")
    parser.add_argument("--version", help="固定版本（默认取 NuGet 最新稳定版）")
    parser.add_argument("--force", action="store_true", help="忽略已安装标记强制重装")
    args = parser.parse_args()

    try:
        if MARKER.exists() and not args.force:
            print(f"已安装 {MARKER.read_text(encoding='utf-8').strip()}，跳过（--force 重装）")
            return 0

        with opener.open(INDEX_URL, timeout=30) as resp:
            versions = json.loads(resp.read())["versions"]
        version = args.version or pick_latest_stable(versions)
        print(f"选定版本：{version}")

        with tempfile.TemporaryDirectory() as tmp:
            nupkg = Path(tmp) / f"webview2-{version}.nupkg"
            url = PACKAGE_URL.format(version=version)
            print(f"下载 {url}")
            with opener.open(url, timeout=120) as resp, open(nupkg, "wb") as fh:
                shutil.copyfileobj(resp, fh)
            print(f"下载完成 {nupkg.stat().st_size / 1e6:.1f} MB")

            include_dir = TOOLS_DIR / "include"
            x64_dir = TOOLS_DIR / "x64"
            include_dir.mkdir(parents=True, exist_ok=True)
            x64_dir.mkdir(parents=True, exist_ok=True)
            taken = []
            with zipfile.ZipFile(nupkg) as zf:
                for name in zf.namelist():
                    if name.startswith("build/native/include/") and name.endswith((".h", ".idl")):
                        (include_dir / Path(name).name).write_bytes(zf.read(name))
                        taken.append(name)
                    elif name == "build/native/x64/WebView2Loader.dll":
                        (x64_dir / "WebView2Loader.dll").write_bytes(zf.read(name))
                        taken.append(name)
                    elif name == "build/native/x64/WebView2LoaderStatic.lib":
                        (x64_dir / "WebView2LoaderStatic.lib").write_bytes(zf.read(name))
                        taken.append(name)
            if not any(n.endswith("WebView2.h") for n in taken):
                raise RuntimeError("nupkg 中未找到 WebView2.h，SDK 结构可能已变化")
            if not (x64_dir / "WebView2Loader.dll").exists():
                raise RuntimeError("nupkg 中未找到 x64 WebView2Loader.dll")

        MARKER.write_text(version, encoding="utf-8")
        print(f"安装完成：tools/webview2/（版本 {version}，{len(taken)} 个文件）")
        return 0
    except Exception as exc:  # noqa: BLE001 保留现场
        print(f"ERROR: {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
