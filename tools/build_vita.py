"""Valdis Story: Abyssal City -> PS Vita (YoYo Loader) builder.

Toma la copia de Steam del usuario + un APK Android donante de GameMaker Studio 1.x
y genera game.apk listo para ux0:data/gms/valdis/game.apk.

Uso:
  python -I tools/build_vita.py --donor <donante.apk> [--steam <carpeta juego>] [--out build/game.apk]

No distribuye nada del juego: todo sale de la copia de Steam local.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_STEAM = r"C:\Program Files (x86)\Steam\steamapps\common\ValdisStoryAbyssalCity"
UTMT = os.path.join(HERE, "utmt_cli", "UndertaleModCli.exe")
PATCHES = [os.path.join(ROOT, "patches", "vita_paths.csx")]
# Build de Steam 722041
KNOWN_SHA256 = {"0a9c913393a30039f696df796d94f0b290c2871febb44a58ad9178f5647d010f"}
NO_COMPRESS = (".ogg", ".png", ".droid")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--steam", default=DEFAULT_STEAM)
    ap.add_argument("--donor", required=True, help="APK Android de un juego GMS 1.x (runner donante)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "game.apk"))
    args = ap.parse_args()

    data_win = os.path.join(args.steam, "data.win")
    if not os.path.isfile(data_win):
        sys.exit(f"No se encuentra {data_win}")
    digest = sha256(data_win)
    if digest not in KNOWN_SHA256:
        print(f"AVISO: data.win con hash desconocido ({digest}); puede no ser la build 722041")

    build = os.path.join(ROOT, "build")
    os.makedirs(build, exist_ok=True)
    droid = os.path.join(build, "game.droid")
    cmd = [UTMT, "load", data_win, "-o", droid, "-f", "-s", *PATCHES]
    print("Parcheando data.win ->", droid)
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

    # Lista de archivos a meter en assets/
    extra = [(droid, "assets/game.droid")]
    for name in sorted(os.listdir(args.steam)):
        p = os.path.join(args.steam, name)
        if name.lower().endswith(".ogg"):
            extra.append((p, f"assets/{name}"))
        elif os.path.isdir(p) and name.startswith("data_"):
            for f in sorted(os.listdir(p)):
                extra.append((os.path.join(p, f), f"assets/{name}/{f}"))

    tmp = args.out + ".tmp"
    with zipfile.ZipFile(args.donor) as src, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as dst:
        kept = 0
        for item in src.infolist():
            n = item.filename
            # Fuera los assets del donante (salvo splash) y la firma antigua
            if n.startswith("assets/") and n != "assets/splash.png":
                continue
            if n.startswith("META-INF/"):
                continue
            dst.writestr(item, src.read(n))
            kept += 1
        for path, arc in extra:
            ctype = zipfile.ZIP_STORED if arc.lower().endswith(NO_COMPRESS) else zipfile.ZIP_DEFLATED
            dst.write(path, arc, compress_type=ctype)
    os.replace(tmp, args.out)
    print(f"OK: {args.out}  ({kept} archivos del donante + {len(extra)} del juego)")
    print("Copia a ux0:data/gms/valdis/game.apk y en YoYo Loader pulsa Triangulo ->"
          " 'Compressed Textures' + 'Optimise APK with Externalisation'.")


if __name__ == "__main__":
    main()
