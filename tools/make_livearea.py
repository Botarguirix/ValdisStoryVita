"""Icono y LiveArea ORIGINALES (dibujados aqui, sin arte del juego): los que van en el repo y en el VPK publico.

Uso:  python -I tools/make_livearea.py            -> runner/vita/sce_sys/ (icon0, pic0, livearea/contents/*)

La Vita exige PNG de 8 bits con paleta (ver docs/LIVEAREA.md):
  icon0.png 128x128 · pic0.png 960x544 · livearea/contents/bg.png 840x500 · startup.png 280x158
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'runner', 'vita', 'sce_sys')
FONT_TITLE = r'C:\Windows\Fonts\georgiab.ttf'   # solo para dibujar el texto; no se distribuye la fuente
FONT_SUB = r'C:\Windows\Fonts\georgia.ttf'
DEEP, MID, LIGHT = (6, 18, 46), (14, 70, 110), (40, 160, 190)
GOLD, GOLD_D = (240, 196, 92), (150, 96, 30)


def save_indexed(img, path):
    """PNG de 8 bits con paleta completa de 256 entradas (con menos colores Pillow baja la profundidad y la Vita
    rechaza la instalacion: error 0x8010113D)."""
    q = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:768]
    q.putpalette(pal + [0] * (768 - len(pal)))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    q.save(path, optimize=True)


def sea(w, h, seed=7):
    """Fondo de mar profundo: degradado, rayos de luz desde arriba, burbujas y una ciudad hundida en silueta."""
    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        t = y / (h - 1)
        a = MID if t < 0.5 else DEEP
        b = LIGHT if t < 0.5 else MID
        k = 1 - (t / 0.5 if t < 0.5 else (t - 0.5) / 0.5)
        c = tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))
        for x in range(w):
            px[x, y] = c
    img = img.convert('RGBA')
    rays = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(rays)
    rnd = random.Random(seed)
    for _ in range(7):
        x0 = rnd.uniform(0.1, 0.9) * w
        spread = rnd.uniform(0.05, 0.12) * w
        d.polygon([(x0 - spread * 0.2, 0), (x0 + spread * 0.2, 0), (x0 + spread, h), (x0 - spread * 0.4, h)], fill=(180, 240, 255, 38))
    img.alpha_composite(rays.filter(ImageFilter.GaussianBlur(max(2, w // 120))))
    city = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(city)
    base = int(h * 0.78)
    x = 0
    while x < w:   # tejados y torres en silueta
        bw = rnd.randint(w // 30, w // 12); bh = rnd.randint(h // 14, h // 5)
        d.rectangle([x, base - bh, x + bw, h], fill=(4, 14, 32, 235))
        if rnd.random() < 0.6:
            d.polygon([(x - 2, base - bh), (x + bw // 2, base - bh - bh // 2), (x + bw + 2, base - bh)], fill=(4, 14, 32, 235))
        if rnd.random() < 0.3:
            d.rectangle([x + bw // 3, base - bh - bh, x + bw // 3 + max(2, bw // 5), base - bh], fill=(4, 14, 32, 235))
        x += bw + rnd.randint(0, w // 40)
    img.alpha_composite(city.filter(ImageFilter.GaussianBlur(max(1, w // 400))))
    bub = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(bub)
    for _ in range(max(12, w // 25)):
        r = rnd.uniform(1, w / 140); cx = rnd.uniform(0, w); cy = rnd.uniform(0, h * 0.8)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(200, 245, 255, 150), width=max(1, int(r / 3)))
    img.alpha_composite(bub)
    return img


def title(img, cx, cy, size, sub=True):
    """VALDIS / STORY en dorado con brillo; debajo "PS VITA"."""
    f = ImageFont.truetype(FONT_TITLE, size)
    lines = ['VALDIS', 'STORY']
    glow = Image.new('RGBA', img.size, (0, 0, 0, 0))
    text = Image.new('RGBA', img.size, (0, 0, 0, 0))
    dg, dt = ImageDraw.Draw(glow), ImageDraw.Draw(text)
    y = cy - size
    for s in lines:
        bb = dt.textbbox((0, 0), s, font=f, stroke_width=max(1, size // 14))
        x = cx - (bb[2] - bb[0]) // 2 - bb[0]
        dg.text((x, y), s, font=f, fill=(90, 220, 255, 200), stroke_width=max(2, size // 8), stroke_fill=(90, 220, 255, 200))
        dt.text((x, y), s, font=f, fill=GOLD, stroke_width=max(1, size // 14), stroke_fill=GOLD_D)
        y += int(size * 1.02)
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(max(2, size // 6))))
    img.alpha_composite(text)
    if sub:
        fs = ImageFont.truetype(FONT_SUB, max(10, size // 3))
        d = ImageDraw.Draw(img)
        bb = d.textbbox((0, 0), 'PS VITA', font=fs)
        d.text((cx - (bb[2] - bb[0]) // 2 - bb[0], y + size // 8), 'PS VITA', font=fs, fill=(200, 240, 255))


def icon():
    img = sea(128, 128, seed=3)
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT_TITLE, 70)
    glow = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).text((64, 60), 'V', font=f, anchor='mm', fill=(90, 220, 255, 230), stroke_width=7, stroke_fill=(90, 220, 255, 230))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(5)))
    d.text((64, 60), 'V', font=f, anchor='mm', fill=GOLD, stroke_width=3, stroke_fill=GOLD_D)
    fs = ImageFont.truetype(FONT_TITLE, 17)
    d.text((64, 108), 'STORY', font=fs, anchor='mm', fill=(225, 245, 255), stroke_width=2, stroke_fill=(5, 20, 40))
    d.rounded_rectangle([2, 2, 125, 125], radius=14, outline=GOLD, width=3)
    return img


def main():
    save_indexed(icon(), os.path.join(ROOT, 'icon0.png'))
    bg = sea(840, 500); title(bg, 560, 210, 74); save_indexed(bg, os.path.join(ROOT, 'livearea', 'contents', 'bg.png'))
    st = sea(280, 158, seed=11); title(st, 140, 70, 34, sub=False); save_indexed(st, os.path.join(ROOT, 'livearea', 'contents', 'startup.png'))
    pic = sea(960, 544, seed=5); title(pic, 480, 230, 92); save_indexed(pic, os.path.join(ROOT, 'pic0.png'))
    print('LiveArea original en', os.path.normpath(ROOT))


if __name__ == '__main__':
    main()
