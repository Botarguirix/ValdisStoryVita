#!/usr/bin/env python3
"""Iconos de los logros para el aviso y la pantalla de logros del runner.

Baja de la pagina publica de logros de Steam de Valdis Story (app 252030) el icono de cada logro (desbloqueado y
bloqueado) y los guarda como PNG de 64x64 en <salida>/<api_name>.png y <api_name>_gris.png, mas nombres_en.txt
(nombre y descripcion originales, para jugar en ingles). El runner los busca en
<vsdata>/logros/; si no estan, usa el sprite del jefe o un trofeo dibujado.

Los iconos son arte del juego (Endless Fluff Games): se bajan en la maquina de cada usuario, no se suben al
repositorio ni van dentro del VPK publico.

Uso:  python tools/logros_iconos.py [salida]        (por defecto build/vsdata/logros)
      Despues copia la carpeta a ux0:data/valdis/vsdata/logros/ en la Vita.
"""
import io
import json
import os
import re
import sys
import urllib.request

APP = 252030
PAGINA = f"https://steamcommunity.com/stats/{APP}/achievements/?l=english"
CDN = f"https://shared.fastly.steamstatic.com/community_assets/images/apps/{APP}/"
UA = {"User-Agent": "Mozilla/5.0 (vsrunner logros_iconos)"}
LADO = 64


def bajar(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=30) as r:
        return r.read()


def logros(html):
    # La pagina trae los logros como JSON escapado dentro del HTML: {\"api_name\":...,\"icon_achieved\":...}
    # comillas dentro de un texto (\\\", se marcan con \x01 y se devuelven luego) y del JSON (\")
    txt = html.replace('\\\\\\"', '\x01').replace('\\"', '"')
    vistos = {}
    for m in re.finditer(r'\{"internal_key":\d+,"api_name":"([^"]+)","name":"([^"]*)","description":"([^"]*)".*?'
                         r'"icon_achieved":"([0-9a-f]+\.jpg)","icon_unachieved":"([0-9a-f]+\.jpg)"', txt):
        vistos.setdefault(m.group(1), tuple(x.replace('\x01', '"') for x in m.groups()[1:]))
    return vistos


def main():
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Falta Pillow: pip install pillow")
    raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    salida = sys.argv[1] if len(sys.argv) > 1 else os.path.join(raiz, "build", "vsdata", "logros")
    os.makedirs(salida, exist_ok=True)
    lista = logros(bajar(PAGINA).decode("utf-8", "replace"))
    if not lista:
        sys.exit("No se encontraron logros en la pagina de Steam (cambio el formato?)")
    hechos = 0
    # nombres y descripciones originales (ingles) para la pantalla de logros con el juego en ingles
    with open(os.path.join(salida, "nombres_en.txt"), "w", encoding="utf-8", newline="\n") as f:
        for api, (nombre, desc, _, _) in sorted(lista.items()):
            f.write(f"{api}\t{nombre.strip()}\t{desc.strip()}\n")
    for api, (_, _, si, no) in sorted(lista.items()):
        for archivo, sufijo in ((si, ""), (no, "_gris")):
            destino = os.path.join(salida, f"{api}{sufijo}.png")
            if os.path.exists(destino):
                continue
            img = Image.open(io.BytesIO(bajar(CDN + archivo))).convert("RGBA").resize((LADO, LADO), Image.LANCZOS)
            img.save(destino)
        hechos += 1
    print(f"{hechos} logros con icono en {salida}")


if __name__ == "__main__":
    main()
