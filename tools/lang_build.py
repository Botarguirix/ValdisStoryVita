#!/usr/bin/env python3
"""Junta los lotes de traduccion build/lang/es_*.tsv ("huella<TAB>texto") en lang/es.txt y los revisa contra
build/lang/en.tsv (tools/lang_extract.py): huella existente, espacios del principio y del final copiados del
original (el juego pega fragmentos), mismo numero de saltos '#' y de comillas simples. lang/es.txt no lleva el texto original.

  python tools/lang_build.py
"""
import glob, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    en = {}
    for line in open(os.path.join(ROOT, 'build', 'lang', 'en.tsv'), encoding='utf-8'):
        h, s, _ = line.rstrip('\n').split('\t', 2)
        en[h] = s
    es = {}; warn = 0
    for path in sorted(glob.glob(os.path.join(ROOT, 'build', 'lang', 'es_*.tsv'))):
        for n, line in enumerate(open(path, encoding='utf-8'), 1):
            line = line.rstrip('\n').rstrip('\r')
            if not line or line.startswith('//'):
                continue
            if len(line) < 18 or line[16] != '\t':
                print('%s:%d formato invalido' % (os.path.basename(path), n)); warn += 1; continue
            h, t = line[:16], line[17:]
            o = en.get(h)
            if o is None:
                print('%s:%d huella desconocida %s' % (os.path.basename(path), n, h)); warn += 1; continue
            lead = lambda x: len(x) - len(x.lstrip(' ')); trail = lambda x: len(x) - len(x.rstrip(' '))
            t = ' ' * lead(o) + t.strip(' ') + ' ' * trail(o)   # los espacios de los extremos, siempre los del original
            probs = []
            if o.count('#') != t.count('#'): probs.append("saltos '#' %d/%d" % (o.count('#'), t.count('#')))
            if probs:
                print('%s:%d %s: %s' % (os.path.basename(path), n, ', '.join(probs), t)); warn += 1
            es[h] = t
    head = ('// Traduccion al espanol de Valdis Story: Abyssal City (traduccion de fans para el port de PS Vita).\n'
            '// Formato: huella<TAB>texto. La huella es FNV-1a 64 del texto original (tools/lang_extract.py);\n'
            '// el texto original no esta en este archivo. Generado por tools/lang_build.py.\n')
    with open(os.path.join(ROOT, 'lang', 'es.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(head)
        for h in sorted(es):
            f.write('%s\t%s\n' % (h, es[h]))
    words = sum(len(en[h].split()) for h in es); total = sum(len(s.split()) for s in en.values())
    print('lang/es.txt: %d de %d textos (%.1f%% de las palabras), avisos: %d' % (len(es), len(en), 100.0 * words / max(1, total), warn))


if __name__ == '__main__':
    sys.exit(main())
