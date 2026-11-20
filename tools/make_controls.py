"""docs/controls.png: mapa de controles de la PS Vita (español / inglés), dibujado a mano (sin arte del juego).

Uso:  python -I tools/make_controls.py
Plantilla para otros ports: cambia TITLE, VERSION, SCREEN y la lista LABELS (texto y a qué botón apunta).
Los botones salen de runner/src/main_vita.cpp (Vita -> mando GameMaker) y las acciones de los valores por
defecto del juego (global.joy_* en vs_settings.ini).
"""
import os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'controls.png')
TITLE, VERSION = 'Valdis Story PSVITA', 'ALPHA 0.5.0'
SCREEN = ('Valdis Story', 'Abyssal City', 'CONTROLES POR DEFECTO / DEFAULT CONTROLS')
W, H = 1800, 1120
BG, INK, SUB, LINE = (238, 242, 247), (24, 38, 58), (60, 72, 90), (70, 130, 170)
BODY, EDGE, DARK, SCR = (32, 42, 58), (90, 104, 124), (14, 20, 30), (36, 62, 92)
F = r'C:\Windows\Fonts\segoeui.ttf'; FB = r'C:\Windows\Fonts\segoeuib.ttf'; SYM = r'C:\Windows\Fonts\seguisym.ttf'


def font(size, bold=False):
    return ImageFont.truetype(FB if bold else F, size)


img = Image.new('RGB', (W, H), BG)
d = ImageDraw.Draw(img)
d.text((70, 55), TITLE, font=font(54, True), fill=INK)
d.text((70, 122), 'PS VITA  •  CONTROLES / CONTROLS', font=font(26), fill=SUB)
d.text((W - 70, 85), VERSION, font=font(28, True), fill=INK, anchor='rm')

# consola
d.rounded_rectangle((380, 275, 580, 320), radius=22, fill=EDGE)            # L
d.rounded_rectangle((1220, 275, 1420, 320), radius=22, fill=EDGE)          # R
d.rounded_rectangle((330, 300, 1470, 825), radius=190, fill=BODY, outline=EDGE, width=4)
d.rounded_rectangle((570, 345, 1230, 755), radius=22, fill=DARK, outline=EDGE, width=3)
d.rounded_rectangle((590, 365, 1210, 715), radius=12, fill=SCR)
d.text((900, 470), SCREEN[0], font=font(46, True), fill='white', anchor='mm')
d.text((900, 530), SCREEN[1], font=font(34), fill=(150, 200, 230), anchor='mm')
d.text((900, 605), SCREEN[2], font=font(19), fill=(210, 220, 230), anchor='mm')
d.text((900, 736), 'PS VITA', font=font(20), fill=(220, 225, 235), anchor='mm')
# cruceta, sticks, PS
d.rounded_rectangle((427, 395, 473, 525), radius=6, fill=DARK, outline=EDGE, width=2)
d.rounded_rectangle((385, 437, 515, 483), radius=6, fill=DARK, outline=EDGE, width=2)
for cx in (450, 1350):
    d.ellipse((cx - 46, 598, cx + 46, 690), fill=DARK, outline=EDGE, width=3)
    d.ellipse((cx - 30, 614, cx + 30, 674), fill=BODY, outline=EDGE, width=2)
d.ellipse((430, 735, 470, 775), outline=EDGE, width=2)
d.text((450, 755), 'PS', font=font(13), fill=(200, 205, 215), anchor='mm')
# botones de cara
FACE = {'tri': (1350, 402, (90, 200, 170), '△'), 'sq': (1285, 465, (220, 150, 210), '□'),
        'ci': (1415, 465, (235, 110, 110), '○'), 'x': (1350, 528, (130, 170, 245), '✕')}
for x, y, c, s in FACE.values():
    d.ellipse((x - 29, y - 29, x + 29, y + 29), fill=DARK, outline=EDGE, width=3)
    d.text((x, y + 1), s, font=ImageFont.truetype(SYM, 32), fill=c, anchor='mm')
for x, t in ((1305, 'SELECT'), (1390, 'START')):
    d.rounded_rectangle((x - 28, 735, x + 28, 749), radius=7, fill=EDGE)
    d.text((x, 770), t, font=font(14), fill=(200, 205, 215), anchor='mm')


def label(x, y, head, sub, anchor='l', sym=None):
    """Texto de una accion. sym = (simbolo, color) se dibuja con la fuente de simbolos delante del titulo."""
    fh = font(27, True)
    w = d.textlength(head, font=fh) + (40 if sym else 0)
    x0 = x if anchor == 'l' else x - w
    if sym:
        d.text((x0 + 14, y + 17), sym[0], font=ImageFont.truetype(SYM, 28), fill=sym[1], anchor='mm')
        x0 += 40
    d.text((x0, y), head, font=fh, fill=INK)
    if sub: d.text((x if anchor == 'l' else x - d.textlength(sub, font=font(23)), y + 38), sub, font=font(23), fill=SUB)


def line(*pts):
    d.line(pts, fill=LINE, width=3)


def col(k): return FACE[k][3], FACE[k][2]


# hombros
label(330, 213, 'L  ·  Asistencia / Assist', None); line((480, 255), (480, 290))
label(1470, 213, 'R  ·  Magia / Magic', None, 'r'); line((1320, 255), (1320, 290))
# izquierda (el texto no pasa de x=300 para no tapar la consola)
label(55, 360, 'Cruceta / D-pad', 'Mover / Move'); line((60, 440), (350, 440), (385, 460))
label(55, 560, 'Stick izq. / L-stick', 'Mover / Move'); line((60, 640), (350, 640), (404, 660))
label(55, 860, 'Panel trasero / Rear touch', 'Izq.: Mapa (L1) · Der.: libre (R1)')
d.text((55, 936), 'Left: Map (L1) · Right: free (R1)', font=font(23), fill=SUB)
# derecha
label(1500, 340, 'Ataque 2 / Attack 2', 'Ataque secundario', sym=col('tri')); line((1379, 395), (1460, 370), (1490, 370))
label(1500, 455, 'Bloquear / Block', 'Cancelar / Cancel', sym=col('ci')); line((1444, 470), (1490, 480))
label(1500, 570, 'Saltar / Jump', 'Aceptar / Accept', sym=col('x')); line((1376, 545), (1460, 595), (1490, 595))
label(1120, 860, 'Atacar / Attack', None, 'r', sym=col('sq')); line((1256, 465), (1245, 465), (1245, 845), (1130, 845))
label(1745, 860, 'START  ·  Menú / Menu', 'SELECT · Trucos / Cheats', 'r'); line((1390, 785), (1390, 845), (1500, 845))

d.rounded_rectangle((65, 1000, 1735, 1080), radius=22, fill=(214, 228, 240))
d.text((900, 1027), 'R + ○  ·  Menú de hechizos / Spell menu', font=font(26, True), fill=INK, anchor='mm')
d.text((900, 1060), 'Los botones se reasignan en el menú de controles del juego / Rebind them in the game\'s controls menu',
       font=font(19), fill=SUB, anchor='mm')
img.save(OUT, optimize=True)
print('->', os.path.normpath(OUT))
