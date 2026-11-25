"""Sella la version y el tipo de build en el icono y la LiveArea (lo llama CMake al configurar cada build de la Vita).

Uso:  python -I tools/livearea_stamp.py <origen sce_sys> <destino> <version> <AOT|PUBLICA>
      p. ej.  ... runner/vita/sce_sys build/vita_noaot/sce_art 0.9.0-alpha PUBLICA

- AOT: tu version personal, con el codigo del juego compilado en tu PC (dorado/rojo).
- PUBLICA: la del Release, sin codigo del juego; la Vita traduce el juego sola (cian).
Marca las cuatro imagenes (icon0, pic0, bg, startup) y las guarda otra vez como PNG de 8 bits con paleta completa
(docs/LIVEAREA.md). Si algo falla (sin Pillow, sin fuentes), CMake usa las imagenes sin sellar.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageStat

FONTS_B = [r'C:\Windows\Fonts\arialbd.ttf',   # cifras claras (con Georgia el 0 parece una o)
           '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', '/usr/share/fonts/TTF/DejaVuSans-Bold.ttf']
STYLE = {   # fondo de la etiqueta, borde, texto
    'AOT': ((120, 24, 20), (240, 190, 86), (255, 236, 170), 'AOT', 'Versión AOT (compilada en tu PC)'),
    'PUBLICA': ((8, 60, 90), (110, 230, 255), (225, 250, 255), 'PÚBLICA', 'Versión pública (la Vita traduce el juego)'),
}


def font(size):
    for p in FONTS_B:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def save_indexed(img, path):
    q = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:768]
    q.putpalette(pal + [0] * (768 - len(pal)))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    q.save(path, optimize=True)


def pill(img, text, size, x, y, anchor, kind, pad=None):
    """Etiqueta redondeada con texto; anchor 'rb' (abajo a la derecha), 'lb', 'rt'..."""
    bgc, edge, fg = STYLE[kind][:3]
    d = ImageDraw.Draw(img)
    f = font(size)
    bb = d.textbbox((0, 0), text, font=f)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    px, py = pad if pad else (max(3, size // 2), max(2, size // 4))
    w, h = tw + 2 * px, th + 2 * py
    x0 = x - w if anchor[0] == 'r' else x
    y0 = y - h if anchor[1] == 'b' else y
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    dl = ImageDraw.Draw(layer)
    dl.rounded_rectangle([x0, y0, x0 + w, y0 + h], radius=h // 2, fill=bgc + (235,), outline=edge + (255,), width=max(1, size // 10))
    dl.text((x0 + px - bb[0], y0 + py - bb[1]), text, font=f, fill=fg + (255,))
    img.alpha_composite(layer)


def pill_size(text, size):
    f = font(size); d = ImageDraw.Draw(Image.new('RGB', (1, 1)))
    bb = d.textbbox((0, 0), text, font=f)
    px, py = max(3, size // 2), max(2, size // 4)
    return bb[2] - bb[0] + 2 * px, bb[3] - bb[1] + 2 * py


def auto(img, text, size, kind, margin=8, centers=True):
    """Coloca la etiqueta donde la imagen tiene menos detalle (bordes) entre las esquinas y, si centers, los centros
    de arriba y abajo."""
    w, h = pill_size(text, size)
    W, H = img.size
    edges = img.convert('L').filter(ImageFilter.FIND_EDGES)
    cands = [(margin, margin, 'lt'), (W - margin, margin, 'rt'), (margin, H - margin, 'lb'), (W - margin, H - margin, 'rb')]
    if centers:
        cands += [((W - w) // 2, margin, 'lt'), ((W - w) // 2, H - margin, 'lb')]
    def box(x, y, a):
        x0 = x - w if a[0] == 'r' else x
        y0 = y - h if a[1] == 'b' else y
        return (x0, y0, x0 + w, y0 + h)
    best = min(cands, key=lambda c: ImageStat.Stat(edges.crop(box(*c))).mean[0])
    pill(img, text, size, best[0], best[1], best[2], kind)


def main():
    src, dst, ver, kind = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4].upper()
    if kind not in STYLE:
        sys.exit('tipo: AOT o PUBLICA')
    short, long_ = STYLE[kind][3], STYLE[kind][4]
    vshort = 'v' + ver.split('-')[0]                       # v0.9.0
    files = {'icon0.png': None, 'pic0.png': None, 'livearea/contents/bg.png': None, 'livearea/contents/startup.png': None}
    for rel in files:
        files[rel] = Image.open(os.path.join(src, rel)).convert('RGBA')
    # 24-11 (pedido del usuario): que el sello no tape nada. Se mide la etiqueta y se pone en la esquina (o centro de
    # arriba/abajo) con menos detalle de la imagen; en la burbuja, una etiqueta minima.
    auto(files['icon0.png'], f'{vshort} {"AOT" if kind == "AOT" else "PÚB"}', 8, kind, margin=5, centers=False)   # corto: cabe en la esquina
    auto(files['livearea/contents/bg.png'], f'v{ver}  ·  {long_}', 15, kind, margin=8)
    auto(files['pic0.png'], f'v{ver}  ·  {long_}', 16, kind, margin=10)
    auto(files['livearea/contents/startup.png'], f'{vshort} {short}', 10, kind, margin=4, centers=False)
    for rel, im in files.items():
        save_indexed(im, os.path.join(dst, rel))
    print(f'LiveArea sellada: {vshort} {kind} -> {os.path.normpath(dst)}')


if __name__ == '__main__':
    main()
