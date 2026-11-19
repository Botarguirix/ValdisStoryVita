"""Prepara los datos para la Vita a partir de TU copia de Steam de Valdis Story.

Genera build/ux0_data_valdis/ con:
  vsdata/   game.vsd + tex/*.vdx + snd/*.wav   (exportado del data.win con UndertaleModCli)
  game/     data_1/ data_2/ data_5/ music_*.ogg (copiados; los PNG llevan al lado su .png.vdx)

Las texturas se guardan en DXT1/DXT5 (.vdx): la Vita las sube sin decodificar PNG y ocupan 4-8x menos
memoria. Con --png se dejan en PNG (sin perdida, pero mucho mas lento en la consola).
Copia el contenido de esa carpeta a ux0:data/valdis/ en la Vita (FTP o USB con VitaShell).

Uso:  python -I tools/build_vsdata.py [--steam "<carpeta del juego>"]
"""
import argparse
import hashlib
import io
import os
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_STEAM = r"C:\Program Files (x86)\Steam\steamapps\common\ValdisStoryAbyssalCity"
UTMT = os.path.join(HERE, "utmt_cli", "UndertaleModCli.exe")
KNOWN_SHA256 = {"0a9c913393a30039f696df796d94f0b290c2871febb44a58ad9178f5647d010f"}  # build Steam 722041


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


GL_DXT1_RGB = 0x83F0
GL_DXT5 = 0x83F3


def rle_blocks(data, bs):
    """RLE por bloques DXT: token u32; bit31 = repetir N veces el bloque siguiente, si no N bloques literales.
    Las zonas transparentes son bloques identicos: ~2x menos que el DXT y se descomprime a velocidad de memcpy."""
    n = len(data) // bs
    out = bytearray()
    lit_start = 0
    i = 0
    while i < n:
        b = data[i * bs:(i + 1) * bs]
        j = i + 1
        while j < n and data[j * bs:(j + 1) * bs] == b:
            j += 1
        if j - i >= 3:
            if i > lit_start:
                out += struct.pack("<I", i - lit_start) + data[lit_start * bs:i * bs]
            out += struct.pack("<I", 0x80000000 | (j - i)) + b
            lit_start = j
        i = j
    if n > lit_start:
        out += struct.pack("<I", n - lit_start) + data[lit_start * bs:n * bs]
    return bytes(out)


def to_vdx(png, out, pot=False):
    """PNG -> .vdx: 'VDX3' + w,h (textura) + ancho/alto reales + formato GL + tamano DXT + tamano RLE, datos."""
    from PIL import Image
    im = Image.open(png).convert("RGBA")
    rw, rh = im.size
    if pot:
        w = 1 << max(2, (rw - 1).bit_length()); h = 1 << max(2, (rh - 1).bit_length())
    else:
        w, h = (rw + 3) & ~3, (rh + 3) & ~3
    if (w, h) != (rw, rh):
        canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0)); canvas.paste(im, (0, 0)); im = canvas
    opaque = im.getchannel("A").getextrema() == (255, 255)
    fmt = GL_DXT1_RGB if opaque else GL_DXT5
    buf = io.BytesIO()
    (im.convert("RGB") if opaque else im).save(buf, "DDS", pixel_format="DXT1" if opaque else "DXT5")
    dds = buf.getvalue()
    off = 128 + (20 if dds[84:88] == b"DX10" else 0)
    size = (w // 4) * (h // 4) * (8 if opaque else 16)
    data = dds[off:off + size]
    assert len(data) == size, png
    z = rle_blocks(data, 8 if opaque else 16)
    with open(out, "wb") as f:
        f.write(b"VDX3" + struct.pack("<7I", w, h, rw, rh, fmt, size, len(z)) + z)
    return png


def _conv(job):
    png, out, pot, remove = job
    to_vdx(png, out, pot)
    if remove:
        os.remove(png)
    return png


def convert_all(jobs):
    with ProcessPoolExecutor() as ex:
        for i, _ in enumerate(ex.map(_conv, jobs), 1):
            if i % 20 == 0 or i == len(jobs):
                print(f"  texturas DXT: {i}/{len(jobs)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--steam", default=DEFAULT_STEAM)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "ux0_data_valdis"))
    ap.add_argument("--png", action="store_true", help="no comprimir texturas (PNG sin perdida)")
    args = ap.parse_args()

    data_win = os.path.join(args.steam, "data.win")
    if not os.path.isfile(data_win):
        sys.exit(f"No se encuentra {data_win}. Instala Valdis Story desde Steam o indica --steam.")
    if sha256(data_win) not in KNOWN_SHA256:
        print("AVISO: data.win distinto de la build de Steam probada (722041). Puede fallar.")

    vsdata = os.path.join(args.out, "vsdata")
    game = os.path.join(args.out, "game")
    os.makedirs(vsdata, exist_ok=True)
    os.makedirs(game, exist_ok=True)

    print("Exportando data.win -> vsdata (UndertaleModCli)...")
    env = dict(os.environ, VSD_OUT=vsdata)
    subprocess.run([UTMT, "load", data_win, "-s", os.path.join(HERE, "export_vsdata.csx")],
                   check=True, env=env, stdout=subprocess.DEVNULL)

    print("Copiando assets externos...")
    copied = 0
    for name in sorted(os.listdir(args.steam)):
        src = os.path.join(args.steam, name)
        if name.lower().endswith(".ogg"):
            shutil.copy2(src, os.path.join(game, name)); copied += 1
        elif os.path.isdir(src) and name.startswith("data_"):
            shutil.copytree(src, os.path.join(game, name), dirs_exist_ok=True); copied += len(os.listdir(src))

    if not args.png:
        print("Comprimiendo texturas a DXT (.vdx)...")
        tex = os.path.join(vsdata, "tex")
        jobs = [(os.path.join(tex, n), os.path.join(tex, n[:-4] + ".vdx"), False, True)
                for n in os.listdir(tex) if n.endswith(".png")]
        for dp, _, fs in os.walk(game):
            jobs += [(os.path.join(dp, n), os.path.join(dp, n + ".vdx"), True, False) for n in fs if n.lower().endswith(".png")]
        convert_all(jobs)

    total = 0
    for dp, _, fs in os.walk(args.out):
        total += sum(os.path.getsize(os.path.join(dp, f)) for f in fs)
    print(f"OK: {args.out}  ({copied} archivos externos, {total / 2**20:.0f} MB en total)")
    print("Copia el CONTENIDO de esa carpeta a ux0:data/valdis/ (quedan ux0:data/valdis/vsdata y ux0:data/valdis/game).")


if __name__ == "__main__":
    main()
