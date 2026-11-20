"""Saca el icono de un .exe de Windows (recursos RT_GROUP_ICON / RT_ICON) y guarda la imagen mas grande como PNG.

Uso:  python -I tools/extract_exe_icon.py <juego.exe> <salida.png>
Sin dependencias aparte de Pillow (lee el formato PE a mano). El PNG sale del .exe de TU copia: no se sube al repo.
"""
import io, struct, sys
from PIL import Image


def sections(b):
    pe = struct.unpack_from('<I', b, 0x3C)[0]
    nsec = struct.unpack_from('<H', b, pe + 6)[0]
    optsz = struct.unpack_from('<H', b, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', b, opt)[0]
    ddir = opt + (96 if magic == 0x10B else 112)
    rsrc_rva = struct.unpack_from('<I', b, ddir + 2 * 8)[0]
    secs = []
    off = opt + optsz
    for i in range(nsec):
        vsize, va, rawsz, rawptr = struct.unpack_from('<IIII', b, off + i * 40 + 8)
        secs.append((va, max(vsize, rawsz), rawptr))
    return rsrc_rva, secs


def rva2off(rva, secs):
    for va, size, raw in secs:
        if va <= rva < va + size:
            return rva - va + raw
    raise ValueError('rva fuera de secciones')


def resources(b):
    """{tipo: {id: bytes}} del primer idioma de cada recurso."""
    rsrc_rva, secs = sections(b)
    base = rva2off(rsrc_rva, secs)

    def entries(off):
        nn, ni = struct.unpack_from('<HH', b, off + 12)
        for i in range(nn + ni):
            name, data = struct.unpack_from('<II', b, off + 16 + i * 8)
            yield name, data

    out = {}
    for tname, tdata in entries(base):
        if not tdata & 0x80000000: continue
        typ = tname & 0xFFFF if not tname & 0x80000000 else None
        for iname, idata in entries(base + (tdata & 0x7FFFFFFF)):
            rid = iname & 0xFFFF if not iname & 0x80000000 else iname
            sub = idata
            while sub & 0x80000000:   # bajar hasta la hoja (idioma)
                sub = next(entries(base + (sub & 0x7FFFFFFF)))[1]
            drva, dsize = struct.unpack_from('<II', b, base + sub)
            o = rva2off(drva, secs)
            out.setdefault(typ, {})[rid] = b[o:o + dsize]
    return out


def biggest_icon(exe):
    b = open(exe, 'rb').read()
    res = resources(b)
    groups, icons = res.get(14, {}), res.get(3, {})
    best = None
    for gid, g in groups.items():
        _, _, count = struct.unpack_from('<HHH', g, 0)
        for i in range(count):
            w, h, colors, _, planes, bits, size, iid = struct.unpack_from('<BBBBHHIH', g, 6 + i * 14)
            w = w or 256; h = h or 256
            data = icons.get(iid)
            if data is None: continue
            # un .ico de una sola imagen para que Pillow lo lea (PNG o BMP dentro)
            ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', w % 256, h % 256, colors, 0, planes, bits, len(data), 22) + data
            try:
                img = Image.open(io.BytesIO(ico)); img.load()
            except Exception:
                continue
            key = (img.width * img.height, bits)
            if best is None or key > best[0]: best = (key, img.convert('RGBA'))
    if best is None: raise SystemExit('el exe no tiene iconos legibles')
    return best[1]


if __name__ == '__main__':
    img = biggest_icon(sys.argv[1])
    img.save(sys.argv[2])
    print('icono', img.size, '->', sys.argv[2])
