#!/usr/bin/env python3
"""打包 dsh 运行时 zip（node + dsh node_modules）供发版上传。

用途：T-074 M3 安装包形态（用户拍板「安装包不放 dsh，安装过程中联网装好」）
——本脚本把本机 tools/ 的运行时打成单 zip，发版时上传为 GitHub Release 资产
poptools-dsh-runtime.zip（PopTools.iss 的 DshRuntimeUrl 指向它）。
zip 内容布局：node/current/... 与 dsh/node_modules/...（安装器解到 {app}\\tools）。
幂等：dist/ 已存在同名 zip 则重新打包覆盖（运行时内容可能已更新）。
用法：python scripts/pack_dsh_runtime.py [--out dist]
依赖：无第三方（zipfile 标准库）。
"""
from __future__ import annotations

import argparse
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NODE_DIR = ROOT / "tools" / "node" / "current"
DSH_DIR = ROOT / "tools" / "dsh" / "node_modules"


def add_tree(zf: zipfile.ZipFile, src: Path, arc_prefix: str) -> int:
    """递归打包目录，返回文件数。空目录跳过（node/dsh 树无空目录依赖）。"""
    count = 0
    for path in sorted(src.rglob("*")):
        if path.is_file():
            zf.write(path, arc_prefix + "/" + path.relative_to(src).as_posix())
            count += 1
    return count


def main() -> int:
    parser = argparse.ArgumentParser(description="打包 dsh 运行时 zip")
    parser.add_argument("--out", default=str(ROOT / "dist"), help="输出目录（默认 dist/）")
    args = parser.parse_args()

    import sys
    try:
        out_dir = Path(args.out)
        out_dir.mkdir(parents=True, exist_ok=True)
        out = out_dir / "poptools-dsh-runtime.zip"
        print(f"打包 node/（{NODE_DIR}）+ dsh/node_modules/（{DSH_DIR}）…")
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            n_node = add_tree(zf, NODE_DIR, "node/current")
            n_dsh = add_tree(zf, DSH_DIR, "dsh/node_modules")
        size_mb = out.stat().st_size / 1e6
        print(f"完成：{out}（{size_mb:.0f} MB，node {n_node} 文件 + dsh {n_dsh} 文件）")
        print("发版时上传为 Release 资产 poptools-dsh-runtime.zip（iss DshRuntimeUrl 指向它）")
        return 0
    except Exception as exc:  # noqa: BLE001 保留现场
        print(f"ERROR: {exc}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    import sys
    sys.exit(main())
