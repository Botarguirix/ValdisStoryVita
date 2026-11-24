#!/usr/bin/env python3
"""Lista los textos visibles del juego para traducirlos (uso local; la salida lleva el texto original en ingles y
no se sube al repositorio).

  python tools/lang_extract.py            ->  build/lang/en.tsv  (huella <TAB> texto <TAB> primer script donde sale)

Lee decomp/strings.txt (tabla de cadenas del data.win) y decomp/CodeEntries/*.gml (codigo GML descompilado de TU copia).
Una cadena se considera texto visible si aparece en el codigo y NUNCA en un contexto que la use como identificador:
comparaciones (== / !=), claves o secciones de ini, rutas de archivo, sprite_add/background_add, Steam, switch/case,
nombres de recursos o de variables. Ante la duda se deja fuera: una cadena sin traducir solo se ve en ingles, una
traducida por error puede romper la logica.

La huella es FNV-1a de 64 bits del texto original en UTF-8 (hex, 16 cifras): es la clave del archivo de traduccion
lang/es.txt, que asi no contiene el texto original. El runner calcula la misma huella (runner/src/lang.cpp).
"""
import os, re, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(ROOT, 'decomp')


def fnv1a64(s: str) -> str:
    h = 0xcbf29ce484222325
    for b in s.encode('utf-8'):
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return '%016x' % h


LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
# contextos que convierten una cadena en identificador
BAD_FUNCS = re.compile(r'\b(ini_\w+|file_\w+|sprite_add|background_add|sound_add|steam_\w+|asset_get_index|'
                       r'variable_\w+|object_get_\w+|room_get_\w+|execute_string|script_execute|highscore_\w+|'
                       r'string_pos|string_count|string_replace\w*|ds_map_\w+|font_add\w*|shader_\w+|audio_\w+|'
                       r'window_set_caption|url_open|get_integer|get_string)\s*\(')


def main():
    strings = open(os.path.join(DEC, 'strings.txt'), encoding='utf-8', errors='replace').read().split('\n')
    table = set(strings)
    # UndertaleModTool escribe entre comillas las cadenas con espacios al principio o al final (" Low ranking..."):
    # sin esto quedaban fuera las descripciones del LORE y otros textos que empiezan por espacio (23-11)
    table |= {s[1:-1] for s in strings if len(s) >= 2 and s[0] == '"' and s[-1] == '"'}
    names = set()   # nombres de variables/recursos: nunca se traducen
    for line in strings:   # identificadores: con guion bajo o en minusculas (las palabras sueltas en mayuscula son texto)
        if re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', line) and ('_' in line or line[0].islower()):
            names.add(line)
    good = collections.OrderedDict(); bad = set(); where = {}
    for fn in sorted(os.listdir(os.path.join(DEC, 'CodeEntries'))):
        if not fn.endswith('.gml'):
            continue
        for line in open(os.path.join(DEC, 'CodeEntries', fn), encoding='utf-8', errors='replace'):
            if '"' not in line:
                continue
            # contexto de cada literal: comparado (== / != / case) o dentro de la llamada a una funcion de identificadores
            calls = []
            for m in BAD_FUNCS.finditer(line):
                depth, j = 0, m.end() - 1
                while j < len(line):
                    if line[j] == '(':
                        depth += 1
                    elif line[j] == ')':
                        depth -= 1
                        if depth == 0:
                            break
                    j += 1
                calls.append((m.start(), j))
            for m in LIT.finditer(line):
                raw = m.group(1)
                s = raw.replace('\\"', '"').replace('\\\\', '\\')
                if s not in table:
                    continue
                before, after = line[:m.start()].rstrip(), line[m.end():].lstrip()
                ctx_bad = (before.endswith('==') or before.endswith('!=') or after.startswith('==') or after.startswith('!=')
                           or re.search(r'\bcase\s*$', before) is not None
                           or any(a < m.start() < b for a, b in calls))
                if ctx_bad:
                    bad.add(s); continue
                good.setdefault(s, None); where.setdefault(s, fn[:-4])
    # 24-11: los textos que llevan comillas dobles van entre comillas simples en el GML ('dijo "hola"') y LIT no los
    # veia (7 lineas de dialogo de Gilda, Reina y Vladyn sin traducir). Se toman de la tabla de cadenas.
    for s in strings:
        # (UndertaleModTool solo pone comillas alrededor si hay espacios en los extremos: "texto" sin ellos es literal)
        utmtQuoted = s.startswith('"') and s.endswith('"') and s.count('"') == 2 and s[1:-1] != s[1:-1].strip()
        if '"' in s and not utmtQuoted and len(re.findall(r'[A-Za-z]{2,}', s)) >= 3:
            good.setdefault(s, None); where.setdefault(s, 'comillas_simples')
    out = []
    for s in good:
        if s in bad or s in names:
            continue
        if not re.search(r'[A-Za-z]', s):        # solo numeros o simbolos
            continue
        if re.fullmatch(r'[A-Za-z0-9_]+', s) and ('_' in s or s.islower()):   # parece identificador
            continue
        if re.search(r'\.(png|ogg|wav|ini|dat|txt|sav)$', s, re.I):
            continue
        out.append(s)
    os.makedirs(os.path.join(ROOT, 'build', 'lang'), exist_ok=True)
    path = os.path.join(ROOT, 'build', 'lang', 'en.tsv')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        for s in out:
            f.write('%s\t%s\t%s\n' % (fnv1a64(s), s.replace('\t', ' ').replace('\n', '\\n'), where[s]))
    # textos que parecen visibles pero el juego tambien compara (zonas, etc.): se traducen solo al dibujarlos
    # (lang/es_draw.txt). Lista aparte en build/lang/en_draw.tsv (23-11)
    draw = []
    for s in good:
        if s not in bad or s in names or not re.search(r'[A-Za-z]{3}', s):
            continue
        if re.fullmatch(r'[A-Za-z0-9_]+', s) and ('_' in s or s.islower()):
            continue
        if re.search(r'\.(png|ogg|wav|ini|dat|txt|sav)$|[\\/]', s, re.I):
            continue
        draw.append(s)
    with open(os.path.join(ROOT, 'build', 'lang', 'en_draw.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        for s in draw:
            f.write('%s\t%s\t%s\n' % (fnv1a64(s), s.replace('\t', ' ').replace('\n', '\\n'), where[s]))
    print('%d textos comparados que parecen visibles -> build/lang/en_draw.tsv' % len(draw))
    words = sum(len(s.split()) for s in out)
    print('%d textos visibles (%d palabras) -> %s; descartadas como identificador: %d' % (len(out), words, path, len(bad)))


if __name__ == '__main__':
    sys.exit(main())
