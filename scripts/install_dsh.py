#!/usr/bin/env python3
"""安装 dsh（DeepSeek Harness）运行时：Node 24 LTS win-x64 + npm @deepseek-ai/dsh。

用途：T-074 dsh 原样移植 M1——sidecar 路线的运行时落点（蓝图
docs/design/20261121-dsh-port-blueprint.md 第二节）。
- Node → tools/node/current/（node.exe / npm.cmd 同目录）
- dsh  → tools/dsh/node_modules/...（package.json 固定版本）
幂等：tools/node/.installed-version / tools/dsh/.installed-version 存在即跳过
（--force-node / --force-dsh 分别重装）。
代理：nodejs.org 与 registry.npmjs.org 均为国外源，走本机代理 127.0.0.1:17890
（下载走 urllib 代理 opener；npm 子进程注入 HTTPS_PROXY/HTTP_PROXY 环境变量）。
用法：python scripts/install_dsh.py [--force-node] [--force-dsh] [--dsh-version <v>]
依赖：无第三方标准库；npm 子进程调用 tools/node/current/npm.cmd。
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import urllib.request

PROXY = "http://127.0.0.1:17890"
NODE_INDEX_URL = "https://nodejs.org/dist/index.json"
NODE_MIN_VERSION = (24, 9, 0)  # dsh engines: node>=24.9.0
DSH_VERSION_DEFAULT = "0.2.2-rc.5"

TOOLS = Path(__file__).resolve().parent.parent / "tools"
NODE_DIR = TOOLS / "node"
DSH_DIR = TOOLS / "dsh"
NODE_MARKER = NODE_DIR / ".installed-version"
DSH_MARKER = DSH_DIR / ".installed-version"
NODE_CURRENT = NODE_DIR / "current"

opener = urllib.request.build_opener(
    urllib.request.ProxyHandler({"http": PROXY, "https": PROXY}))


def _version_key(version: str) -> tuple[int, ...]:
    return tuple(int(x) for x in version.lstrip("v").split("."))


def install_node(force: bool) -> str:
    """装 Node 24 LTS 到 tools/node/current/，返回版本号。"""
    if NODE_MARKER.exists() and NODE_CURRENT.joinpath("node.exe").exists() and not force:
        version = NODE_MARKER.read_text(encoding="utf-8").strip()
        print(f"Node 已安装 {version}，跳过")
        return version

    print("查询 nodejs.org 发行索引…")
    with opener.open(NODE_INDEX_URL, timeout=30) as resp:
        releases = json.loads(resp.read())
    candidates = [
        r["version"] for r in releases
        if r["version"].startswith("v24.")
        and _version_key(r["version"]) >= NODE_MIN_VERSION
        and not r.get("lts") is False and r.get("npm")  # 含 npm 的完整发行
    ]
    if not candidates:
        raise RuntimeError("nodejs.org 上未找到 >=24.9 的 v24 发行")
    version = max(candidates, key=_version_key)
    zip_name = f"node-{version}-win-x64.zip"
    url = f"https://nodejs.org/dist/{version}/{zip_name}"
    print(f"下载 {url}")
    with tempfile.TemporaryDirectory() as tmp:
        zip_path = Path(tmp) / zip_name
        with opener.open(url, timeout=300) as resp, open(zip_path, "wb") as fh:
            shutil.copyfileobj(resp, fh)
        print(f"下载完成 {zip_path.stat().st_size / 1e6:.1f} MB，解压…")
        if NODE_CURRENT.exists():
            shutil.rmtree(NODE_CURRENT)
        with zipfile.ZipFile(zip_path) as zf:
            zf.extractall(tmp)
        extracted = Path(tmp) / f"node-{version}-win-x64"
        if not (extracted / "node.exe").exists():
            raise RuntimeError(f"zip 解压结果异常：{extracted}")
        NODE_DIR.mkdir(parents=True, exist_ok=True)
        shutil.move(str(extracted), str(NODE_CURRENT))
    NODE_MARKER.write_text(version, encoding="utf-8")
    print(f"Node 安装完成：tools/node/current/（{version}）")
    return version


def install_dsh(force: bool, dsh_version: str) -> None:
    """npm install @deepseek-ai/dsh 到 tools/dsh/。"""
    if DSH_MARKER.exists() and DSH_MARKER.read_text(encoding="utf-8").strip() == dsh_version and not force:
        print(f"dsh 已安装 {dsh_version}，跳过")
        return

    npm_cmd = NODE_CURRENT / "npm.cmd"
    if not npm_cmd.exists():
        raise RuntimeError(f"npm 不存在：{npm_cmd}（先装 Node）")
    DSH_DIR.mkdir(parents=True, exist_ok=True)
    (DSH_DIR / "package.json").write_text(
        json.dumps({"name": "poptools-dsh-runtime",
                    "private": True,
                    "dependencies": {"@deepseek-ai/dsh": dsh_version}},
                   indent=2),
        encoding="utf-8")
    env = {
        **dict(__import__("os").environ),
        "HTTPS_PROXY": PROXY,
        "HTTP_PROXY": PROXY,
        "NO_PROXY": "localhost,127.0.0.1",
    }
    print(f"npm install @deepseek-ai/dsh@{dsh_version}（走代理，包族较大需数分钟）…")
    # CLI 旗标显式指定代理：用户全局 .npmrc 写死的旧端口（7897）优先级高于
    # 环境变量，必须用命令行覆盖（npm 配置优先级：CLI > env > user npmrc）
    result = subprocess.run(
        ["cmd", "/c", str(npm_cmd), "install", "--no-audit", "--no-fund",
         "--loglevel", "error",
         f"--proxy={PROXY}", f"--https-proxy={PROXY}",
         "--registry=https://registry.npmjs.org"],
        cwd=DSH_DIR, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        raise RuntimeError(f"npm install 失败（code {result.returncode}）:\n{result.stdout[-2000:]}\n{result.stderr[-2000:]}")
    bin_js = DSH_DIR / "node_modules" / "@deepseek-ai" / "dsh" / "lib" / "bin.js"
    if not bin_js.exists():
        raise RuntimeError(f"安装后缺少 {bin_js}——包结构可能已变化")
    DSH_MARKER.write_text(dsh_version, encoding="utf-8")
    print(f"dsh 安装完成：tools/dsh/（bin: {bin_js}）")


def main() -> int:
    parser = argparse.ArgumentParser(description="安装 Node + dsh sidecar 运行时到 tools/")
    parser.add_argument("--force-node", action="store_true", help="重装 Node")
    parser.add_argument("--force-dsh", action="store_true", help="重装 dsh")
    parser.add_argument("--dsh-version", default=DSH_VERSION_DEFAULT, help="固定 dsh 版本")
    args = parser.parse_args()

    try:
        install_node(args.force_node)
        install_dsh(args.force_dsh, args.dsh_version)
        return 0
    except Exception as exc:  # noqa: BLE001 保留现场
        print(f"ERROR: {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
