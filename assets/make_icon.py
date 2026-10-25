"""从 assets/icon.png（母版 logo）派生所有图标资源。

    python assets/make_icon.py

产物：

    assets/icon-512.png   512px 圆角白底图标（发布 / 商店用）
    assets/icon.ico       16/24/32/48/64/128/256 —— exe 图标、窗口图标、托盘图标
    assets/ui-logo.png    96px 白色、透明底 —— 深色界面左侧的标记
    assets/favicon.png    64px 圆角白底 —— 浏览器标签页（--headless 开发模式）

母版只认 assets/icon.png：换 logo 就是换这张图，然后重跑这个脚本。
（CMake 构建时 windres 会重新把 icon.ico 编进 exe，所以改完记得重新构建。）
"""
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent
SRC = ROOT / "icon.png"

PAD = 0.06      # 图标四周留白，相对内容较长边（小尺寸下标记才够大）
RADIUS = 0.22   # 圆角半径，相对边长
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
UI_SIZE = 96


def content_box(img):
    """非白色像素的包围盒：母版四周的空白不进入图标。"""
    gray = img.convert("L")
    mask = gray.point(lambda v: 255 if v < 245 else 0)
    return mask.getbbox()


def master_square():
    """裁到内容、补成正方形、四周留白 —— 所有尺寸都从这里降采样。"""
    src = Image.open(SRC).convert("RGB")
    crop = src.crop(content_box(src))
    side = int(max(crop.size) * (1 + 2 * PAD))
    canvas = Image.new("RGB", (side, side), (255, 255, 255))
    canvas.paste(crop, ((side - crop.width) // 2, (side - crop.height) // 2))
    return canvas


def rounded(img, radius_ratio=RADIUS):
    """加圆角。遮罩按 4 倍超采样再缩回来，边缘不会锯齿。"""
    side = img.size[0]
    r = max(1, int(side * radius_ratio))
    big = Image.new("L", (side * 4, side * 4), 0)
    ImageDraw.Draw(big).rounded_rectangle([0, 0, side * 4 - 1, side * 4 - 1], radius=r * 4, fill=255)
    mask = big.resize((side, side), Image.LANCZOS)
    out = img.convert("RGBA")
    out.putalpha(mask)
    return out


def white_mark(img):
    """白底黑图 → 透明底白图：深色界面里直接用，灰度层次靠 alpha 保留。"""
    alpha = img.convert("L").point(lambda v: 255 - v)
    out = Image.new("RGBA", img.size, (255, 255, 255, 255))
    out.putalpha(alpha)
    return out


def main():
    master = master_square()

    rounded(master.resize((512, 512), Image.LANCZOS)).save(ROOT / "icon-512.png")

    frames = [rounded(master.resize((s, s), Image.LANCZOS)) for s in ICO_SIZES]
    try:
        frames[-1].save(ROOT / "icon.ico", format="ICO",
                        sizes=[(s, s) for s in ICO_SIZES], append_images=frames[:-1])
    except (TypeError, ValueError):
        frames[-1].save(ROOT / "icon.ico", format="ICO", sizes=[(s, s) for s in ICO_SIZES])

    white_mark(master).resize((UI_SIZE, UI_SIZE), Image.LANCZOS).save(ROOT / "ui-logo.png")
    rounded(master.resize((64, 64), Image.LANCZOS)).save(ROOT / "favicon.png")

    for name in ("icon-512.png", "icon.ico", "ui-logo.png", "favicon.png"):
        p = ROOT / name
        print(f"make_icon: {name:14s} {p.stat().st_size:>7d} bytes")


if __name__ == "__main__":
    main()
