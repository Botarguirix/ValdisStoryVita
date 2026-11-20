#!/usr/bin/env python3
"""Genera build/lang/revision_traduccion.xlsx para revisar la traduccion frase a frase (uso local: lleva el texto
original en ingles, sacado de TU copia del juego; no se sube al repositorio).

  python tools/lang_extract.py      (si no existe build/lang/en.tsv)
  python tools/lang_review.py       ->  build/lang/revision_traduccion.xlsx

Columnas: Seccion, Origen (personaje / script), Huella, Ingles, Espanol actual, Correccion (vacia), Notas.
Escribe la frase corregida en "Correccion" y luego:  python tools/lang_import.py  (ver ese script).
El .xlsx se escribe a mano (zip + XML), sin librerias externas.
"""
import os, re, sys, zipfile
from xml.sax.saxutils import escape

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANG = os.path.join(ROOT, 'build', 'lang')


def section(src):
    s = re.sub(r'^gml_(Script|Object|RoomCC|Room)_', '', src)
    rules = [
        (r'^dia_wyatt', 'Diálogos', 'Wyatt'), (r'^dia_reina', 'Diálogos', 'Reina'),
        (r'^dia_vladyn', 'Diálogos', 'Vladyn'), (r'^dia_gilda', 'Diálogos', 'Gilda'),
        (r'^dia_civilian', 'Diálogos', 'Civiles y entrenadores'), (r'^dia_template', 'Diálogos', 'Plantilla'),
        (r'^dia_misc', 'Historia, saber y objetivos', 'Varios'),
        (r'^cinematic', 'Cinemáticas', None),
        (r'^(item_txt|armor_txt|accessory_txt|deki_shop)', 'Objetos y equipo', None),
        (r'^eqp_', 'Armas por personaje', None), (r'^storage', 'Armas por personaje', None),
        (r'^(wyatt|reina|vlad|gilda)_skill', 'Habilidades', None), (r'^fin_txt', 'Remates de enfoque', None),
        (r'^(assist_txt|adjust_fist|npc_assist)', 'Ayudantes', None), (r'^soul_boost', 'Almas', None),
        (r'^(ui_txt|draw_|step_|status_ui|ui_wyatt|key_switch|learn_skill|need_mana|quick_map|save_game|standard_menu|skill_learn|magus_steal|control_map|general_control|ending_control|boss_rating|fg_control|joy_name|control_controller)', 'Menús e interfaz', None),
        (r'^zone_set', 'Zonas', None),
        (r'_(Step|Create|Draw|Collision|Alarm)_', 'Objetos del mapa, jefes y avisos', None),
    ]
    for pat, sec, who in rules:
        if re.search(pat, s):
            if who is None:
                m = re.match(r'^(\w+?)_(skill|Step|Create|Draw|Collision|Alarm|script)', s)
                who = s.replace('_script', '')
            return sec, who
    return 'Otros', s


def col(n):
    r = ''
    while n:
        n, m = divmod(n - 1, 26); r = chr(65 + m) + r
    return r


def main():
    en = {}
    order = []
    for line in open(os.path.join(LANG, 'en.tsv'), encoding='utf-8'):
        h, t, src = line.rstrip('\n').split('\t', 2)
        en[h] = (t, src); order.append(h)
    es = {}
    for line in open(os.path.join(ROOT, 'lang', 'es.txt'), encoding='utf-8'):
        line = line.rstrip('\n')
        if len(line) > 17 and line[16] == '\t' and not line.startswith('//'):
            es[line[:16]] = line[17:]
    secorder = ['Menús e interfaz', 'Historia, saber y objetivos', 'Cinemáticas', 'Diálogos', 'Objetos y equipo',
                'Armas por personaje', 'Habilidades', 'Remates de enfoque', 'Ayudantes', 'Almas', 'Zonas',
                'Objetos del mapa, jefes y avisos', 'Otros']
    rows = []
    for i, h in enumerate(order):
        t, src = en[h]
        sec, who = section(src)
        rows.append((secorder.index(sec) if sec in secorder else 99, i, sec, who, h, t.replace('#', '\n'),
                     es.get(h, '').replace('#', '\n'), src))
    rows.sort()
    head = ['Sección', 'Personaje / origen', 'Huella', 'Inglés (original)', 'Español actual', 'Corrección', 'Notas', 'Script']
    widths = [22, 24, 18, 60, 60, 60, 25, 34]

    def cell(r, c, v, style=0):
        v = escape(v).replace('\n', '&#10;')
        return '<c r="%s%d" t="inlineStr" s="%d"><is><t xml:space="preserve">%s</t></is></c>' % (col(c), r, style, v)

    xml = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
           '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">',
           '<sheetViews><sheetView workbookViewId="0"><pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews>',
           '<cols>' + ''.join('<col min="%d" max="%d" width="%d" customWidth="1"/>' % (i + 1, i + 1, w) for i, w in enumerate(widths)) + '</cols>',
           '<sheetData>']
    xml.append('<row r="1">' + ''.join(cell(1, i + 1, v, 1) for i, v in enumerate(head)) + '</row>')
    for n, r in enumerate(rows, start=2):
        _, _, sec, who, h, t, s, src = r
        vals = [sec, who, h, t, s, '', '', src]
        styles = [2, 2, 2, 2, 2, 3, 2, 2]
        xml.append('<row r="%d">' % n + ''.join(cell(n, i + 1, v, styles[i]) for i, v in enumerate(vals)) + '</row>')
    xml.append('</sheetData><autoFilter ref="A1:%s%d"/></worksheet>' % (col(len(head)), len(rows) + 1))
    styles = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
              '<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
              '<fonts count="2"><font><sz val="11"/><name val="Calibri"/></font><font><b/><sz val="11"/><color rgb="FFFFFFFF"/><name val="Calibri"/></font></fonts>'
              '<fills count="4"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill>'
              '<fill><patternFill patternType="solid"><fgColor rgb="FF1F4E79"/></patternFill></fill>'
              '<fill><patternFill patternType="solid"><fgColor rgb="FFFFF2CC"/></patternFill></fill></fills>'
              '<borders count="1"><border/></borders><cellStyleXfs count="1"><xf/></cellStyleXfs>'
              '<cellXfs count="4"><xf/>'
              '<xf fontId="1" fillId="2" applyFont="1" applyFill="1"><alignment vertical="center"/></xf>'
              '<xf applyAlignment="1"><alignment wrapText="1" vertical="top"/></xf>'
              '<xf fillId="3" applyFill="1" applyAlignment="1"><alignment wrapText="1" vertical="top"/></xf>'
              '</cellXfs></styleSheet>')
    out = os.path.join(LANG, 'revision_traduccion.xlsx')
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>')
        z.writestr('_rels/.rels', '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>')
        z.writestr('xl/workbook.xml', '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Traduccion" sheetId="1" r:id="rId1"/></sheets><definedNames><definedName name="_xlnm._FilterDatabase" localSheetId="0" hidden="1">Traduccion!$A$1:$%s$%d</definedName></definedNames></workbook>' % (col(len(head)), len(rows) + 1))
        z.writestr('xl/_rels/workbook.xml.rels', '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>')
        z.writestr('xl/styles.xml', styles)
        z.writestr('xl/worksheets/sheet1.xml', '\n'.join(xml))
    counts = {}
    for r in rows: counts[r[2]] = counts.get(r[2], 0) + 1
    print('%d frases -> %s' % (len(rows), out))
    for k in secorder:
        if k in counts: print('  %-34s %d' % (k, counts[k]))


if __name__ == '__main__':
    sys.exit(main())
