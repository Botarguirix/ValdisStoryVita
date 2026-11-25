"""Icono y LiveArea ORIGINALES (dibujados aqui, sin arte del juego): los que van en el repo y en el VPK publico.

Uso:  python -I tools/make_livearea.py            -> runner/vita/sce_sys/ (icon0, pic0, livearea/contents/*)

Diseño (24-11): una ciudad abisal iluminada en el fondo del mar, rayos de luz desde la superficie, plancton que brilla
y un emblema dorado (anillo con la "V" y una espada) junto al titulo. La version y el tipo de build (AOT / PUBLICA) no
van aqui: los sella tools/livearea_stamp.py al compilar (CMake), sobre este arte o sobre el personal.

La Vita exige PNG de 8 bits con paleta (ver docs/LIVEAREA.md):
  icon0.png 128x128 · pic0.png 960x544 · livearea/contents/bg.png 840x500 · startup.png 280x158
"""
import math, os, random
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'runner', 'vita', 'sce_sys')
FONT_TITLE = r'C:\Windows\Fonts\georgiab.ttf'   # solo para dibujar el texto; no se distribuye la fuente
FONT_SUB = r'C:\Windows\Fonts\georgia.ttf'
TOP, MIDC, DEEP = (28, 120, 150), (8, 46, 78), (2, 8, 22)
GOLD, GOLD_L, GOLD_D = (236, 190, 86), (255, 232, 160), (120, 74, 22)
CYAN = (110, 230, 255)


def save_indexed(img, path):
    """PNG de 8 bits con paleta completa de 256 entradas (con menos colores Pillow baja la profundidad y la Vita
    rechaza la instalacion: error 0x8010113D)."""
    q = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:768]
    q.putpalette(pal + [0] * (768 - len(pal)))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    q.save(path, optimize=True)


def lerp(a, b, k):
    return tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))


def abyss(w, h, seed=7, city=True):
    """Mar profundo: degradado, rayos de luz, ciudad abisal con ventanas encendidas y plancton brillante."""
    rnd = random.Random(seed)
    img = Image.new('RGB', (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        c = lerp(TOP, MIDC, t / 0.45) if t < 0.45 else lerp(MIDC, DEEP, (t - 0.45) / 0.55)
        d.line([(0, y), (w, y)], fill=c)
    img = img.convert('RGBA')
    # rayos de luz desde la superficie
    rays = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    dr = ImageDraw.Draw(rays)
    for _ in range(9):
        x0 = rnd.uniform(-0.1, 1.1) * w; sp = rnd.uniform(0.04, 0.10) * w; lean = rnd.uniform(-0.25, 0.25) * w
        dr.polygon([(x0 - sp * 0.15, 0), (x0 + sp * 0.15, 0), (x0 + lean + sp, h * 0.85), (x0 + lean - sp * 0.5, h * 0.85)],
                   fill=(170, 240, 255, rnd.randint(18, 34)))
    img.alpha_composite(rays.filter(ImageFilter.GaussianBlur(max(2, w // 90))))
    if city:
        # dos planos de ciudad: el lejano (azulado, difuso) y el cercano (oscuro, con ventanas)
        for plane, (col, blur, hmin, hmax, base_k, lit) in enumerate([((10, 40, 70, 200), max(1, w // 300), 0.10, 0.30, 0.80, 0.25),
                                                                     ((3, 10, 24, 250), 0, 0.12, 0.42, 0.90, 0.55)]):
            layer = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            dl = ImageDraw.Draw(layer)
            glow = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            dg = ImageDraw.Draw(glow)
            base = int(h * base_k)
            x = -rnd.randint(0, w // 20)
            while x < w:
                bw = rnd.randint(max(4, w // 32), max(6, w // 11)); bh = int(h * rnd.uniform(hmin, hmax))
                top = base - bh
                dl.rectangle([x, top, x + bw, h], fill=col)
                kind = rnd.random()
                if kind < 0.35:   # cupula
                    dl.ellipse([x, top - bw // 2, x + bw, top + bw // 2], fill=col)
                elif kind < 0.65:   # aguja
                    dl.polygon([(x, top), (x + bw // 2, top - bh // 2), (x + bw, top)], fill=col)
                elif kind < 0.8:   # torre con arco
                    dl.rectangle([x + bw // 3, top - bh // 3, x + 2 * bw // 3, top], fill=col)
                if plane == 1 and bw > 8:   # ventanas encendidas
                    for _ in range(int(bw * bh / 260 * lit) + 1):
                        wx = rnd.randint(x + 2, x + bw - 3); wy = rnd.randint(top + 3, min(h - 3, base + 10))
                        c = (255, 210, 120, 255) if rnd.random() < 0.6 else (130, 235, 255, 255)
                        s = max(1, w // 420)
                        dl.rectangle([wx, wy, wx + s, wy + s * 2], fill=c)
                        dg.ellipse([wx - 3 * s, wy - 3 * s, wx + 4 * s, wy + 5 * s], fill=(c[0], c[1], c[2], 70))
                x += bw + rnd.randint(0, max(1, w // 60))
            if blur:
                layer = layer.filter(ImageFilter.GaussianBlur(blur))
            img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(max(1, w // 200))))
            img.alpha_composite(layer)
        # niebla del fondo marino sobre la base de la ciudad
        fog = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        df = ImageDraw.Draw(fog)
        for y in range(int(h * 0.7), h):
            k = (y - h * 0.7) / (h * 0.3)
            df.line([(0, y), (w, y)], fill=(20, 90, 120, int(70 * k)))
        img.alpha_composite(fog)
    # plancton y burbujas que brillan
    p = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    dp = ImageDraw.Draw(p)
    for _ in range(max(20, w // 9)):
        r = rnd.uniform(0.6, w / 260 + 0.8); cx = rnd.uniform(0, w); cy = rnd.uniform(0, h)
        a = rnd.randint(90, 220)
        dp.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(160, 240, 255, a))
    for _ in range(max(6, w // 60)):
        r = rnd.uniform(2, w / 110 + 2); cx = rnd.uniform(0, w); cy = rnd.uniform(0, h * 0.7)
        dp.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(200, 248, 255, 150), width=max(1, int(r / 4)))
    img.alpha_composite(p.filter(ImageFilter.GaussianBlur(0.6)))
    img.alpha_composite(p)
    return img


def emblem(img, cx, cy, r):
    """Anillo dorado con la "V" y una espada vertical detras, con brillo cian."""
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    sw = max(2, r // 9)
    # espada (hoja, guarda y pomo)
    blade = [(cx - r * 0.09, cy - r * 1.25), (cx + r * 0.09, cy - r * 1.25), (cx + r * 0.07, cy + r * 0.85), (cx, cy + r * 1.15), (cx - r * 0.07, cy + r * 0.85)]
    d.polygon(blade, fill=(215, 235, 245, 255), outline=(90, 120, 140, 255))
    d.line([(cx, cy - r * 1.2), (cx, cy + r * 1.05)], fill=(150, 185, 205, 255), width=max(1, r // 30))
    d.rectangle([cx - r * 0.42, cy - r * 1.32, cx + r * 0.42, cy - r * 1.22], fill=GOLD, outline=GOLD_D)
    d.ellipse([cx - r * 0.1, cy - r * 1.55, cx + r * 0.1, cy - r * 1.35], fill=GOLD, outline=GOLD_D)
    # anillo
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=GOLD_D, width=sw + 2)
    d.ellipse([cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1], outline=GOLD, width=sw)
    d.ellipse([cx - r * 0.8, cy - r * 0.8, cx + r * 0.8, cy + r * 0.8], outline=GOLD_D, width=max(1, sw // 3))
    # "V"
    f = ImageFont.truetype(FONT_TITLE, int(r * 1.15))
    d.text((cx, cy + r * 0.05), 'V', font=f, anchor='mm', fill=GOLD_L, stroke_width=max(1, r // 14), stroke_fill=GOLD_D)
    glow = layer.filter(ImageFilter.GaussianBlur(max(2, r // 5)))
    tint = Image.new('RGBA', img.size, CYAN + (0,))
    tint.putalpha(glow.getchannel('A').point(lambda a: int(a * 0.75)))
    img.alpha_composite(tint)
    img.alpha_composite(layer)


def title(img, x, y, size, anchor='l', sub=True):
    """VALDIS STORY en dorado con brillo; debajo ABYSSAL CITY · PS VITA."""
    f = ImageFont.truetype(FONT_TITLE, size)
    fs = ImageFont.truetype(FONT_SUB, max(9, int(size * 0.34)))
    glow = Image.new('RGBA', img.size, (0, 0, 0, 0)); text = Image.new('RGBA', img.size, (0, 0, 0, 0))
    dg, dt = ImageDraw.Draw(glow), ImageDraw.Draw(text)
    a = 'lt' if anchor == 'l' else 'mt'
    for i, s in enumerate(['VALDIS', 'STORY']):
        yy = y + int(i * size * 1.0)
        dg.text((x, yy), s, font=f, anchor=a, fill=CYAN + (200,), stroke_width=max(2, size // 7), stroke_fill=CYAN + (200,))
        dt.text((x, yy), s, font=f, anchor=a, fill=GOLD, stroke_width=max(1, size // 14), stroke_fill=GOLD_D)
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(max(2, size // 5))))
    img.alpha_composite(text)
    if sub:
        d = ImageDraw.Draw(img)
        yy = y + int(size * 2.12)
        d.text((x, yy), 'ABYSSAL CITY  ·  PS VITA', font=fs, anchor=a, fill=(205, 242, 255), stroke_width=1, stroke_fill=(3, 15, 30))


def icon():
    img = abyss(128, 128, seed=3, city=True)
    shade = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shade).ellipse([14, 10, 114, 110], fill=(0, 8, 20, 120))
    img.alpha_composite(shade.filter(ImageFilter.GaussianBlur(8)))
    emblem(img, 64, 60, 36)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([1, 1, 126, 126], radius=16, outline=GOLD_D, width=3)
    d.rounded_rectangle([2, 2, 125, 125], radius=15, outline=GOLD, width=1)
    return img


def main():
    save_indexed(icon(), os.path.join(ROOT, 'icon0.png'))
    # LiveArea: la puerta va en el centro; emblema a la izquierda arriba y titulo a la derecha
    bg = abyss(840, 500, seed=7); emblem(bg, 150, 150, 70); title(bg, 600, 22, 60, anchor='m')
    save_indexed(bg, os.path.join(ROOT, 'livearea', 'contents', 'bg.png'))
    st = abyss(280, 158, seed=11); emblem(st, 62, 80, 34); title(st, 118, 34, 34, anchor='l', sub=False)
    save_indexed(st, os.path.join(ROOT, 'livearea', 'contents', 'startup.png'))
    pic = abyss(960, 544, seed=5); emblem(pic, 250, 250, 105); title(pic, 400, 150, 92, anchor='l')
    save_indexed(pic, os.path.join(ROOT, 'pic0.png'))
    print('LiveArea original en', os.path.normpath(ROOT))


if __name__ == '__main__':
    main()
