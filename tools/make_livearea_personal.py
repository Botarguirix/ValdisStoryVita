"""Icono y LiveArea PERSONALES con arte oficial de Valdis Story (de tu copia del juego y de imagenes promocionales).

NO se suben al repo ni van en el VPK publico (arte con copyright de Endless Fluff Games): se escriben en
runner/vita/sce_sys_personal/ (ignorado por git) y CMake los usa en la build AOT en lugar de los originales.
CMake les sella encima la version y el tipo de build (tools/livearea_stamp.py: "v0.9.0 AOT").

Uso:  python -I tools/make_livearea_personal.py <pagina_65.png> <Valdis_Story_AC.exe> [carpeta_arte]
  <pagina_65.png>  pagina de textura 65 de tu data.win (build/tex_png_backup/65.png): ui_splash y ui_logo.
  <exe>            el ejecutable de tu copia: SIEMPRE de ahi sale el icono de la burbuja (emblema "VS").
  [carpeta_arte]   (24-11, pedido del usuario) arte promocional del juego, por defecto build/arte_oficial/ (ignorado):
                     portada.webp       512x512, logo + Wyatt y Reina   -> pic0 (lo que se ve al abrir el juego)
                     portada_ancha.png  la misma ilustracion apaisada   -> fondo difuminado de pic0
                     escena.webp        1024x640, ilustracion            -> fondo de la LiveArea
                     circulo.webp       512x512 con transparencia        -> boton Iniciar
                   Sin esa carpeta se usa el fondo y el logo del titulo del juego (pagina 65), como antes.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_exe_icon import biggest_icon
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'runner', 'vita', 'sce_sys_personal')
ARTE = os.path.join(HERE, '..', 'build', 'arte_oficial')
SPLASH = (384, 612, 512, 288)   # x, y, w, h en la pagina 65 (bg 214 ui_splash)
LOGO = (4, 612, 371, 422)       # bg 220 ui_logo
FONT = r'C:\Windows\Fonts\georgiab.ttf'
FONT_B = r'C:\Windows\Fonts\arialbd.ttf'


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


def vita_tag(img, x, y, size, anchor='mt'):
    """"PS VITA" en blanco con borde, para que se vea que es la version de la Vita."""
    f = ImageFont.truetype(FONT_B, size)
    ImageDraw.Draw(img).text((x, y), 'PS VITA', font=f, anchor=anchor, fill=(255, 255, 255),
                             stroke_width=max(2, size // 7), stroke_fill=(10, 25, 50))


def shadowed(img, layer, pos, blur, alpha=0.8):
    sh = Image.new('RGBA', img.size, (0, 0, 0, 0))
    sh.paste((0, 10, 25, 255), (pos[0] + 3, pos[1] + 5), layer.split()[3].point(lambda v: int(v * alpha)))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(blur)))
    img.alpha_composite(layer, pos)


def icon_from_exe(exe):
    """Burbuja: siempre el icono del .exe del juego, con "PS VITA" arriba (la banda de version la pone el sello)."""
    emblem = biggest_icon(exe)
    ic = Image.new('RGBA', (128, 128), (8, 30, 60, 255))
    e = emblem.resize((104, 104), Image.LANCZOS)
    ic.alpha_composite(e, (12, 2))
    band = Image.new('RGBA', ic.size, (0, 0, 0, 0))
    ImageDraw.Draw(band).rectangle([4, 4, 123, 19], fill=(5, 20, 45, 200))
    ic.alpha_composite(band)
    vita_tag(ic, 64, 12, 12, anchor='mm')
    return emblem, ic


def with_official_art(exe):
    portada = Image.open(os.path.join(ARTE, 'portada.webp')).convert('RGBA')
    ancha = Image.open(os.path.join(ARTE, 'portada_ancha.png')).convert('RGBA')
    escena = Image.open(os.path.join(ARTE, 'escena.webp')).convert('RGBA')
    circulo = Image.open(os.path.join(ARTE, 'circulo.webp')).convert('RGBA')
    emblem, ic = icon_from_exe(exe)
    # pic0 (al abrir el juego): la portada entera en el centro sobre la misma ilustracion apaisada, difuminada y oscura
    pic = cover(ancha, 960, 544).filter(ImageFilter.GaussianBlur(10))
    pic = ImageEnhance.Brightness(pic).enhance(0.55).convert('RGBA')
    p = portada.resize((544, 544), Image.LANCZOS)
    edge = Image.new('L', p.size, 0)
    ImageDraw.Draw(edge).rectangle([0, 0, 543, 543], fill=255)
    edge = edge.filter(ImageFilter.GaussianBlur(0))
    mask = Image.new('L', p.size, 255)
    fade = Image.linear_gradient('L').rotate(90, expand=True).resize((40, 544))   # bordes laterales suaves
    mask.paste(fade, (0, 0)); mask.paste(fade.transpose(Image.FLIP_LEFT_RIGHT), (504, 0))
    p.putalpha(mask)
    pic.alpha_composite(p, (208, 0))
    shadowed(pic, emblem.resize((150, 150), Image.LANCZOS), (37, 150), 6)   # emblema del .exe y "PS VITA" a la izquierda
    vita_tag(pic, 112, 312, 40)
    save_indexed(pic, os.path.join(OUT, 'pic0.png'))
    # LiveArea: la ilustracion de la escena de fondo; la puerta va en el centro
    bg = cover(escena, 840, 500, fx=0.5, fy=0.45)
    save_indexed(bg, os.path.join(OUT, 'livearea', 'contents', 'bg.png'))
    # boton Iniciar: el circulo (personajes + logo) a la izquierda y "PS VITA" a la derecha, sobre la escena oscurecida
    st = ImageEnhance.Brightness(cover(escena, 280, 158, fx=0.3).filter(ImageFilter.GaussianBlur(3))).enhance(0.6).convert('RGBA')
    c = circulo.resize((150, 150), Image.LANCZOS)
    shadowed(st, c, (6, 4), 4)
    e = emblem.resize((64, 64), Image.LANCZOS)
    shadowed(st, e, (180, 22), 3)
    vita_tag(st, 212, 94, 20)
    save_indexed(st, os.path.join(OUT, 'livearea', 'contents', 'startup.png'))
    save_indexed(ic, os.path.join(OUT, 'icon0.png'))


def with_title_screen(page_png, exe):
    page = Image.open(page_png).convert('RGBA')
    x, y, w, h = SPLASH; splash = page.crop((x, y, x + w, y + h))
    x, y, w, h = LOGO; logo = page.crop((x, y, x + w, y + h))
    emblem, ic = icon_from_exe(exe)

    def scene(w, h, logo_h, lx, ly):
        img = cover(splash, w, h).convert('RGBA')
        k = logo_h / logo.height
        l = logo.resize((round(logo.width * k), logo_h), Image.LANCZOS)
        shadowed(img, l, (lx - l.width // 2, ly), max(2, logo_h // 40), 0.7)
        vita_tag(img, lx, ly + logo_h + logo_h // 30, max(10, logo_h // 11))
        return img
    save_indexed(scene(840, 500, 360, 600, 40), os.path.join(OUT, 'livearea', 'contents', 'bg.png'))
    st = cover(splash, 280, 158).convert('RGBA')
    shadowed(st, emblem.resize((130, 130), Image.LANCZOS), (10, 14), 5)
    k = 140 / logo.height
    l = logo.resize((round(logo.width * k), 140), Image.LANCZOS)
    st.alpha_composite(l, (150 + (130 - l.width) // 2, 9))
    save_indexed(st, os.path.join(OUT, 'livearea', 'contents', 'startup.png'))
    save_indexed(scene(960, 544, 380, 640, 50), os.path.join(OUT, 'pic0.png'))
    save_indexed(ic, os.path.join(OUT, 'icon0.png'))


def main():
    global ARTE
    if len(sys.argv) > 3:
        ARTE = sys.argv[3]
    if os.path.exists(os.path.join(ARTE, 'portada.webp')):
        with_official_art(sys.argv[2]); src = 'arte oficial de ' + os.path.normpath(ARTE)
    else:
        with_title_screen(sys.argv[1], sys.argv[2]); src = 'pantalla de titulo (pagina 65)'
    print('LiveArea personal en', os.path.normpath(OUT), '(' + src + '; no se sube a git)')


if __name__ == '__main__':
    main()
