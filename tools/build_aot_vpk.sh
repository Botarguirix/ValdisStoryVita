#!/usr/bin/env bash
# Genera TU VPK rápido (AOT) de Valdis Story PSVITA a partir de TU copia de Steam.
#
#   tools/build_aot_vpk.sh <carpeta del juego en Steam | ruta a data.win> [-j N]
#
# Requisitos: VitaSDK (variable VITASDK), CMake 3.16+, Ninja y un compilador de C++ para el PC (g++ o clang)
# con zlib. En Windows: terminal "MSYS2 UCRT64" o WSL. Guía completa: docs/AOT_USUARIOS.md
#
# El resultado lleva código traducido de tu copia del juego: es SOLO para uso personal. No lo compartas.
set -euo pipefail

die() { echo "ERROR: $*" >&2; exit 1; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KNOWN_SHA="0a9c913393a30039f696df796d94f0b290c2871febb44a58ad9178f5647d010f"   # Steam build 722041

SRC=""; JOBS=""
while [ $# -gt 0 ]; do
    case "$1" in
        -j) JOBS="${2:-}"; shift 2 ;;
        -j*) JOBS="${1#-j}"; shift ;;
        -h|--help) sed -n 2,9p "$0"; exit 0 ;;
        *) SRC="$1"; shift ;;
    esac
done
[ -n "$SRC" ] || { sed -n 2,9p "$0"; exit 1; }
if [ -d "$SRC" ]; then DATA_WIN="$SRC/data.win"; else DATA_WIN="$SRC"; fi
[ -f "$DATA_WIN" ] || die "no encuentro data.win en '$SRC'"
if [ -z "$JOBS" ]; then JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4); fi

echo "== 1/5 Comprobando herramientas"
[ -n "${VITASDK:-}" ] || die "falta la variable VITASDK (instala VitaSDK: https://vitasdk.org)"
[ -f "$VITASDK/share/vita.toolchain.cmake" ] || die "VITASDK=$VITASDK no parece una instalación de VitaSDK"
export PATH="$VITASDK/bin:$PATH"
command -v cmake >/dev/null || die "falta cmake"
command -v ninja >/dev/null || die "falta ninja"
command -v c++ >/dev/null || command -v g++ >/dev/null || command -v clang++ >/dev/null || die "falta un compilador de C++ para el PC"
command -v arm-vita-eabi-g++ >/dev/null || die "no encuentro arm-vita-eabi-g++ en \$VITASDK/bin"

if command -v sha256sum >/dev/null; then SHA=$(sha256sum "$DATA_WIN" | cut -c1-64)
else SHA=$(shasum -a 256 "$DATA_WIN" | cut -c1-64); fi
if [ "$SHA" = "$KNOWN_SHA" ]; then echo "   data.win: build 722041 de Steam (correcta)"
else echo "   AVISO: data.win con huella desconocida ($SHA). Se probó con la build 722041; puede fallar."; fi

OUT="$ROOT/build/aot_usuario"
mkdir -p "$OUT/vsdata/snd" "$OUT/vsdata/tex"   # vsconv escribe aqui los sonidos incrustados

echo "== 2/5 Compilando las herramientas de PC (vsconv, vsaot)"
cmake -S "$ROOT/runner" -B "$OUT/tools" -G Ninja -DVS_TOOLS_ONLY=ON -DVS_AOT=OFF -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$OUT/tools" --target vsconv vsaot -j "$JOBS"

echo "== 3/5 Leyendo tu data.win (game.vsd)"
"$OUT/tools/vsconv" vsd "$DATA_WIN" "$OUT/vsdata"
[ -f "$OUT/vsdata/game.vsd" ] || die "vsconv no generó game.vsd"

echo "== 4/5 Traduciendo los scripts del juego a C++ (vsaot)"
rm -rf "$OUT/aot_gen"
"$OUT/tools/vsaot" "$OUT/vsdata/game.vsd" "$OUT/aot_gen"
[ -f "$OUT/aot_gen/aot_table.cpp" ] || die "vsaot no generó el código"

echo "== 5/5 Compilando el VPK para la Vita con $JOBS procesos (puede tardar de 30 min a varias horas)"
cmake -S "$ROOT/runner" -B "$OUT/vita" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DVS_AOT=ON -DVS_AOT_DIR="$OUT/aot_gen" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$OUT/vita" -j "$JOBS"

cp "$OUT/vita/vsvita.vpk" "$ROOT/build/vsvita_aot_personal.vpk"
echo
echo "Listo: build/vsvita_aot_personal.vpk"
echo "Instálalo con VitaShell en lugar del VPK de Releases (tus partidas se conservan)."
echo "Lleva código traducido de TU copia del juego: es solo para ti. No lo compartas ni lo subas a internet."
