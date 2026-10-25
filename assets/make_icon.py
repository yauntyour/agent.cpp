"""生成 agent 的应用图标（icon.svg / icon.ico）。

设计：深色底 + 一枚发光的「声音球体」，球体上有一道新月高光，
像是黑暗里侧过脸来的角色；左侧三条声波弧表示它能听见你。

    python assets/make_icon.py
"""
import math
import pathlib

from PIL import Image, ImageDraw, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent
SIZE = 1024          # 先大图渲染，再降采样出各尺寸
BG_TOP = (26, 22, 44)
BG_BOTTOM = (9, 9, 14)
ORB_CORE = (186, 176, 255)
ORB_MID = (124, 108, 255)
ORB_EDGE = (232, 116, 198)


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def radial(size, center, radius, inner, outer):
    """返回一张 RGBA 图：从 center 向外尺寸 radius 处渐变到 outer。"""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    cx, cy = center
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - cx, y - cy) / radius
            if d >= 1.0:
                continue
            t = d ** 1.35
            px[x, y] = (*lerp(inner, outer, t), round(255 * (1 - t) ** 1.6))
    return img


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle((0, 0, size - 1, size - 1), radius=radius, fill=255)
    return m


def build_master():
    s = SIZE
    base = Image.new("RGB", (s, s), BG_BOTTOM)
    d = ImageDraw.Draw(base)
    for y in range(s):                     # 竖向渐变底
        d.line((0, y, s, y), fill=lerp(BG_TOP, BG_BOTTOM, y / s))

    cx, cy = s * 0.57, s * 0.5
    orb_r = s * 0.29

    glow = radial(s, (cx, cy), orb_r * 2.1, (150, 130, 255), (30, 20, 60))
    base = Image.alpha_composite(base.convert("RGBA"), glow)

    orb = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    od = ImageDraw.Draw(orb)
    od.ellipse((cx - orb_r, cy - orb_r, cx + orb_r, cy + orb_r), fill=(*ORB_MID, 255))
    base = Image.alpha_composite(base, orb)

    # 内部球体渐变：亮核偏左上
    core = radial(s, (cx - orb_r * 0.28, cy - orb_r * 0.34), orb_r * 1.5, ORB_CORE, ORB_EDGE)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).ellipse(
        (cx - orb_r, cy - orb_r, cx + orb_r, cy + orb_r), fill=255)
    base.paste(core, (0, 0), Image.composite(mask, Image.new("L", (s, s), 0), core.split()[3]))

    # 新月高光：一枚略小的圆向右下偏移，用底色「咬」出月牙
    crescent = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    cd = ImageDraw.Draw(crescent)
    cd.ellipse((cx - orb_r * 1.02, cy - orb_r * 1.02, cx + orb_r * 1.02, cy + orb_r * 1.02),
               fill=(255, 255, 255, 46))
    cut = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(cut).ellipse(
        (cx - orb_r * 0.96 + orb_r * 0.26, cy - orb_r * 0.96 - orb_r * 0.18,
         cx + orb_r * 0.96 + orb_r * 0.26, cy + orb_r * 0.96 - orb_r * 0.18),
        fill=(0, 0, 0, 255))
    crescent = Image.composite(crescent, Image.new("RGBA", (s, s), (0, 0, 0, 0)),
                               Image.eval(cut.split()[3], lambda v: 255 - v))
    base = Image.alpha_composite(base, crescent)

    # 三条声波弧（左侧）
    waves = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    wd = ImageDraw.Draw(waves)
    for i, (r, alpha, width) in enumerate(((0.085, 235, 17), (0.135, 165, 15), (0.185, 95, 13))):
        rr = s * r
        wd.arc((cx - rr - s * 0.245, cy - rr, cx + rr - s * 0.245, cy + rr),
               start=128, end=232, fill=(214, 208, 255, alpha), width=width)
    waves = waves.filter(ImageFilter.GaussianBlur(s * 0.002))
    base = Image.alpha_composite(base, waves)

    # 圆角遮罩
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    out.paste(base, (0, 0), rounded_mask(s, round(s * 0.235)))
    return out


def main():
    master = build_master()
    master.resize((512, 512), Image.LANCZOS).save(ROOT / "icon-512.png")
    sizes = [16, 24, 32, 48, 64, 128, 256]
    frames = [master.resize((n, n), Image.LANCZOS) for n in sizes]
    frames[-1].save(ROOT / "icon.ico", format="ICO",
                    sizes=[(n, n) for n in sizes], append_images=frames[:-1])
    print("assets/make_icon.py: wrote icon-512.png, icon.ico")


if __name__ == "__main__":
    main()
