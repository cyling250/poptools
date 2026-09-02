# -*- coding: utf-8 -*-
r"""
make_ico.py — 从应用图标 SVG 生成 Windows 多尺寸 .ico（PopTools 图标管线）

创建时间：2026-09-02（2026-09-02 由 temp_scripts 同名脚本沉淀入 scripts/）
用途：Edge headless 以透明底渲染 512px 主图 → Pillow LANCZOS 缩放合成多尺寸帧
      → 保存 32bpp BMP 帧格式的 .ico（兼容性最好）→ 解析 ICO 目录逐帧校验。
      渲染仅使用本机 Edge（C:\Program Files (x86)\Microsoft\Edge），无其他依赖。
依赖：Pillow（项目 .venv 已有）
用法：
    python scripts/make_ico.py --help
    python scripts/make_ico.py                    # 默认 src/resources/app_icon.svg → packaging/PopTools.ico
    python scripts/make_ico.py --out 其他.ico     # 输出到别处（如试验新图标）
"""
import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
import traceback
from pathlib import Path

from PIL import Image

EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SVG = REPO_ROOT / "src" / "resources" / "app_icon.svg"
DEFAULT_OUT = REPO_ROOT / "packaging" / "PopTools.ico"
MASTER_SIZE = 512


def render_transparent_master(svg: Path, work: Path) -> Path:
    """Edge headless 渲染 512px 透明底主图，返回 PNG 路径"""
    out = work / "master_alpha.png"
    html = work / "master_alpha.html"
    html.write_text(
        "<!DOCTYPE html><html><head><style>html,body{margin:0;padding:0;"
        "background:transparent;overflow:hidden}</style></head>"
        f"<body><img src='{svg.as_uri()}' width='{MASTER_SIZE}' height='{MASTER_SIZE}'></body></html>",
        encoding="utf-8",
    )
    cmd = [
        EDGE,
        "--headless=new",
        "--disable-gpu",
        "--hide-scrollbars",
        "--default-background-color=00000000",
        "--force-device-scale-factor=1",
        "--no-first-run",
        f"--user-data-dir={work / 'edge_profile'}",
        f"--window-size={MASTER_SIZE},{MASTER_SIZE}",
        f"--screenshot={out}",
        html.as_uri(),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
    if not out.exists():
        raise RuntimeError(f"Edge 渲染失败：{result.stderr[-500:]}")
    return out


def verify_ico(path: Path) -> None:
    """解析 ICO 头与目录项，校验帧数、尺寸与数据格式"""
    data = path.read_bytes()
    res_type = struct.unpack_from("<H", data, 2)[0]
    count = struct.unpack_from("<H", data, 4)[0]
    if res_type != 1:
        raise RuntimeError(f"ICO 类型字段异常：{res_type}")
    print(f"ICO 目录：{count} 帧")
    for i in range(count):
        off = 6 + i * 16
        w, h = data[off], data[off + 1]
        bpp = struct.unpack_from("<H", data, off + 6)[0]
        size = struct.unpack_from("<I", data, off + 8)[0]
        data_off = struct.unpack_from("<I", data, off + 12)[0]
        magic = data[data_off:data_off + 4]
        fmt = "PNG" if magic == b"\x89PNG" else "BMP/DIB"
        print(f"  {w or 256}x{h or 256} {bpp}bpp {size}B {fmt}")


def main():
    parser = argparse.ArgumentParser(
        description="从应用图标 SVG 生成 Windows 多尺寸 .ico（Edge 渲染 + Pillow 合成）")
    parser.add_argument("--svg", type=Path, default=DEFAULT_SVG,
                        help=f"源 SVG 路径（默认 {DEFAULT_SVG.relative_to(REPO_ROOT)}）")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT,
                        help=f"输出 .ico 路径（默认 {DEFAULT_OUT.relative_to(REPO_ROOT)}）")
    parser.add_argument("--sizes", default="16,24,32,48,64,128,256",
                        help="ICO 帧尺寸列表，逗号分隔（默认 16,24,32,48,64,128,256）")
    args = parser.parse_args()

    try:
        sizes = sorted({int(s) for s in args.sizes.split(",")})
        if not sizes or max(sizes) > 256 or min(sizes) < 16:
            raise ValueError("尺寸须在 16~256 之间")
        if not args.svg.is_file():
            raise FileNotFoundError(f"源 SVG 不存在：{args.svg}")

        with tempfile.TemporaryDirectory(prefix="make_ico_") as tmp:
            master = render_transparent_master(args.svg.resolve(), Path(tmp))
            img = Image.open(master).convert("RGBA")
            img.load()  # 临时目录即将销毁，先把像素读入内存
        if img.size != (MASTER_SIZE, MASTER_SIZE):
            img = img.crop((0, 0, MASTER_SIZE, MASTER_SIZE))
        corner_alpha = img.getpixel((2, 2))[3]
        if corner_alpha > 8:
            raise RuntimeError(f"主图角部不透明（alpha={corner_alpha}），透明渲染未生效")

        out = args.out.resolve()
        out.parent.mkdir(parents=True, exist_ok=True)
        img.save(out, format="ICO", sizes=[(s, s) for s in sizes], bitmap_format="bmp")
        print(f"已生成 {out}（{out.stat().st_size} 字节）")
        verify_ico(out)
    except Exception:
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
