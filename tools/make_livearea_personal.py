"""Icono y LiveArea PERSONALES con el arte de TU copia del juego (fondo submarino del titulo y logo).

NO se suben al repo ni van en el VPK publico (arte con copyright de Endless Fluff Games): se escriben en
runner/vita/sce_sys_personal/ (ignorado por git) y CMake los usa en lugar de los originales si existen.

Uso:  python -I tools/make_livearea_personal.py <pagina_65.png> <Valdis_Story_AC.exe>
  <pagina_65.png> = la pagina de textura 65 exportada de tu data.win (build 722041 de Steam), p. ej.
  build/tex_png_backup/65.png. Ahi estan ui_splash (fondo del titulo, 512x288 en 384,612) y ui_logo
  (371x422 en 4,612). Comprobar con:  build/pc/vsdinfo build/vsdata/game.vsd bg 214   (y bg 220)
  <Valdis_Story_AC.exe> = el ejecutable de tu copia: su icono (el emblema "VS", 128x128) se usa para la burbuja y
  para el boton de Iniciar (tools/extract_exe_icon.py).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_exe_icon import biggest_icon
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'runner', 'vita', 'sce_sys_personal')
SPLASH = (384, 612, 512, 288)   # x, y, w, h en la pagina 65 (bg 214 ui_splash)
LOGO = (4, 612, 371, 422)       # bg 220 ui_logo
FONT = r'C:\Windows\Fonts\georgiab.ttf'


def save_indexed(img, path):
    q = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:768]
    q.putpalette(pal + [0] * (768 - len(pal)))   # paleta completa: PNG de 8 bits (si no, error 0x8010113D)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    q.save(path, optimize=True)


def cover(img, w, h, fx=0.5, fy=0.5):
    k = max(w / img.width, h / img.height)
    s = img.resize((round(img.width * k), round(img.height * k)), Image.LANCZOS)
    x = min(max(0, round(s.width * fx - w / 2)), s.width - w)
    y = min(max(0, round(s.height * fy - h / 2)), s.height - h)
    return s.crop((x, y, x + w, y + h))


def scene(splash, logo, w, h, logo_h, lx, ly, vita_tag=True):
    img = cover(splash, w, h).convert('RGBA')
    k = logo_h / logo.height
    l = logo.resize((round(logo.width * k), logo_h), Image.LANCZOS)
    shadow = Image.new('RGBA', img.size, (0, 0, 0, 0))
    a = l.split()[3].point(lambda v: v * 0.7)
    shadow.paste((0, 20, 40, 255), (lx - l.width // 2 + 3, ly + 4), a)
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(max(2, logo_h // 40))))
    img.alpha_composite(l, (lx - l.width // 2, ly))
    if vita_tag:
        f = ImageFont.truetype(FONT, max(10, logo_h // 11))
        ImageDraw.Draw(img).text((lx, ly + logo_h + logo_h // 30), 'PS VITA', font=f, anchor='mt', fill=(255, 255, 255),
                                 stroke_width=max(1, logo_h // 90), stroke_fill=(0, 30, 60))
    return img


def main():
    page = Image.open(sys.argv[1]).convert('RGBA')
    x, y, w, h = SPLASH; splash = page.crop((x, y, x + w, y + h))
    x, y, w, h = LOGO; logo = page.crop((x, y, x + w, y + h))
    save_indexed(scene(splash, logo, 840, 500, 360, 600, 40), os.path.join(OUT, 'livearea', 'contents', 'bg.png'))
    # boton de Iniciar: fondo del titulo + emblema del .exe a la izquierda + logo a la derecha
    emblem = biggest_icon(sys.argv[2])
    st = cover(splash, 280, 158).convert('RGBA')
    e = emblem.resize((130, 130), Image.LANCZOS)
    glow = Image.new('RGBA', st.size, (0, 0, 0, 0))
    glow.paste((0, 20, 40, 255), (12, 17), e.split()[3])
    st.alpha_composite(glow.filter(ImageFilter.GaussianBlur(5)))
    st.alpha_composite(e, (10, 14))
    k = 140 / logo.height
    l = logo.resize((round(logo.width * k), 140), Image.LANCZOS)
    st.alpha_composite(l, (150 + (130 - l.width) // 2, 9))
    save_indexed(st, os.path.join(OUT, 'livearea', 'contents', 'startup.png'))
    save_indexed(scene(splash, logo, 960, 544, 380, 640, 50), os.path.join(OUT, 'pic0.png'))
    # icono de la burbuja: el del propio .exe (emblema "VS"), al tamano exacto
    ic = emblem.resize((128, 128), Image.LANCZOS)
    bgc = Image.new('RGBA', (128, 128), (8, 30, 60, 255)); bgc.alpha_composite(ic); ic = bgc
    save_indexed(ic, os.path.join(OUT, 'icon0.png'))
    print('LiveArea personal en', os.path.normpath(OUT), '(no se sube a git)')


if __name__ == '__main__':
    main()
