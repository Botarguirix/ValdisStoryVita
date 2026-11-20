#!/usr/bin/env python3
"""Lee las correcciones del Excel de revision y las aplica a la traduccion.

  python tools/lang_import.py [build/lang/revision_traduccion.xlsx]
    -> build/lang/es_zz_revision.tsv  (lo ultimo que junta lang_build, asi manda sobre los lotes anteriores)
    -> lang/es.txt                     (vuelve a ejecutar tools/lang_build.py)

Solo cuenta la columna "Correccion" (vacia = sin cambios). Los saltos de linea de la celda pasan a '#', el salto de
linea del juego. Lee el .xlsx tal como lo guarda Excel (sharedStrings) o como lo escribe lang_review.py (inlineStr).
"""
import os, re, subprocess, sys, zipfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NS = {'m': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}


def text_of(el):
    return ''.join(t.text or '' for t in el.iter('{%s}t' % NS['m']))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'lang', 'revision_traduccion.xlsx')
    z = zipfile.ZipFile(path)
    shared = []
    if 'xl/sharedStrings.xml' in z.namelist():
        for si in ET.fromstring(z.read('xl/sharedStrings.xml')).findall('m:si', NS):
            shared.append(text_of(si))
    sheet = [n for n in z.namelist() if re.match(r'xl/worksheets/sheet\d+\.xml', n)][0]
    rows = []
    for row in ET.fromstring(z.read(sheet)).iter('{%s}row' % NS['m']):
        vals = {}
        for c in row.findall('m:c', NS):
            ref = re.match(r'([A-Z]+)', c.get('r')).group(1)
            t = c.get('t')
            if t == 's':
                v = c.find('m:v', NS); vals[ref] = shared[int(v.text)] if v is not None else ''
            elif t == 'inlineStr':
                vals[ref] = text_of(c)
            else:
                v = c.find('m:v', NS); vals[ref] = v.text if v is not None else ''
        rows.append(vals)
    head = {v: k for k, v in rows[0].items()}
    ch, cc = head.get('Huella'), head.get('Corrección')
    if not ch or not cc:
        print('No encuentro las columnas "Huella" y "Corrección" en la primera fila'); return 1
    out = []
    for r in rows[1:]:
        h = (r.get(ch) or '').strip(); t = (r.get(cc) or '').strip()
        if re.fullmatch(r'[0-9a-f]{16}', h) and t:
            out.append('%s\t%s' % (h, t.replace('\r', '').replace('\n', '#')))
    dst = os.path.join(ROOT, 'build', 'lang', 'es_zz_revision.tsv')
    old = {}
    if os.path.exists(dst):
        for line in open(dst, encoding='utf-8'):
            if len(line) > 17: old[line[:16]] = line.rstrip('\n')
    for line in out: old[line[:16]] = line
    with open(dst, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(old[k] for k in sorted(old)) + '\n')
    print('%d correcciones nuevas (%d en total) -> %s' % (len(out), len(old), dst))
    subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'lang_build.py')], check=False)


if __name__ == '__main__':
    sys.exit(main())
