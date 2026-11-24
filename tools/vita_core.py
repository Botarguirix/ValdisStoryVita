#!/usr/bin/env python3
"""Lee un volcado de fallo de la Vita (ux0:data/psp2core-*.psp2dmp: ELF core comprimido con gzip).

  python -I tools/vita_core.py <volcado.psp2dmp> [elf_con_simbolos]

Muestra los modulos cargados, los hilos (con el que fallo marcado) y sus registros. Si se pasa el ELF sin firmar del
runner (build/vita/vsvita) traduce PC y LR de eboot.bin a funcion y linea con arm-vita-eabi-addr2line.
Formato de las notas segun vita-parse-core (xyzz): nombre "CORE", tipos 1 info, 2 modulos, 3 hilos, 4 registros.
"""
import gzip, os, struct, subprocess, sys


def notes(elf):
    e_phoff, = struct.unpack_from('<I', elf, 0x1C)
    e_phentsize, e_phnum = struct.unpack_from('<HH', elf, 0x2A)
    for i in range(e_phnum):
        p_type, p_offset, _, _, p_filesz = struct.unpack_from('<IIIII', elf, e_phoff + i * e_phentsize)
        if p_type != 4:   # PT_NOTE
            continue
        pos, end = p_offset, p_offset + p_filesz
        while pos + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from('<III', elf, pos)
            name = elf[pos + 12:pos + 12 + namesz].rstrip(b'\0').decode('latin-1')
            dpos = pos + 12 + ((namesz + 3) & ~3)
            yield name, ntype, elf[dpos:dpos + descsz]
            pos = dpos + ((descsz + 3) & ~3)


def cstr(b, off, n):
    return b[off:off + n].split(b'\0')[0].decode('latin-1')


def main():
    raw = open(sys.argv[1], 'rb').read()
    elf = gzip.decompress(raw) if raw[:2] == b'\x1f\x8b' else raw
    sym = sys.argv[2] if len(sys.argv) > 2 else None
    found = {}
    for name, ntype, desc in notes(elf):
        found.setdefault((name, ntype), []).append(desc)
    for (name, ntype), lst in sorted(found.items()):
        print('nota %-12s tipo %d: %d bloque(s), %d bytes' % (name, ntype, len(lst), sum(len(d) for d in lst)))
    return elf, found, sym


if __name__ == '__main__':
    main()


def strings(b, minlen=4):
    out, cur = [], b''
    for c in b:
        if 32 <= c < 127: cur += bytes([c])
        else:
            if len(cur) >= minlen: out.append(cur.decode())
            cur = b''
    if len(cur) >= minlen: out.append(cur.decode())
    return out
