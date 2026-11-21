# Compilar tu propia versión rápida (AOT)

El VPK de [Releases](../../../releases) es la versión de **intérprete**: no lleva nada del juego, por eso se puede compartir, pero en las salas más cargadas baja a unos 34 fps. La versión **AOT** traduce los scripts del juego a C++ y los compila dentro del VPK: va a **60 fps** en todo el juego medido, pero como lleva código derivado del juego **no se puede publicar**. Cada persona la genera con **su propia copia de Steam**.

> ⚠️ El VPK que generes es **solo para ti**. No lo compartas ni lo subas a internet.

## Qué necesitas

- Tu copia de **Valdis Story: Abyssal City** de Steam (build 722041; el script comprueba la huella de `data.win`).
- Un PC con **8 GB de RAM** o más y unos **3 GB libres**.
- [VitaSDK](https://vitasdk.org) instalado, con la variable `VITASDK` definida.
- CMake 3.16+, Ninja y un compilador de C++ para el PC (g++ o clang) con zlib.
- Este repositorio (botón **Code → Download ZIP**, o `git clone`).

### Windows

1. Instala [MSYS2](https://www.msys2.org) y abre la terminal **MSYS2 UCRT64**.
2. Instala las herramientas:
   ```bash
   pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-zlib
   ```
3. Instala VitaSDK siguiendo [vitasdk.org](https://vitasdk.org) y define la variable, por ejemplo `export VITASDK=/c/vitasdk` (añádela a `~/.bashrc` para no repetirla).

### Linux / WSL

```bash
sudo apt install build-essential cmake ninja-build zlib1g-dev git
# VitaSDK: https://vitasdk.org  →  export VITASDK=/usr/local/vitasdk
```

## Compilar

Desde la carpeta del repositorio, pasa la carpeta del juego en Steam:

```bash
tools/build_aot_vpk.sh "/c/Program Files (x86)/Steam/steamapps/common/ValdisStoryAbyssalCity"
```

En Linux/WSL la ruta será otra (por ejemplo `/mnt/c/Program Files (x86)/Steam/steamapps/common/ValdisStoryAbyssalCity`). También puedes pasar directamente la ruta a `data.win`.

El script hace cinco pasos:

1. Comprueba las herramientas y la huella de tu `data.win`.
2. Compila dos herramientas de PC: `vsconv` (lee `data.win`) y `vsaot` (el traductor).
3. Convierte tu `data.win` a `game.vsd`, con el mismo convertidor que usa la Vita.
4. Traduce los 6072 scripts del juego a C++ (unos 35 MB de código).
5. Compila el VPK para la Vita. **Es el paso largo:** unos 33 minutos en un PC de 8 hilos, y más en equipos modestos. Con poca RAM, limita los procesos con `-j 2`.

Probado el 20-11 en una copia limpia del repositorio (Windows, MSYS2 UCRT64): el código generado es idéntico al de la versión personal y en la Vita el log dice `[aot] codigo nativo activo: 6072 de 6072 scripts`.

El resultado queda en `build/vsvita_aot_personal.vpk`.

## Instalar

1. Copia `build/vsvita_aot_personal.vpk` a la Vita e instálalo con VitaShell. Sustituye al VPK de Releases y **tus partidas se conservan** (están en `ux0:data/valdis/save/`).
2. Los datos del juego (`ux0:data/valdis/game/` y la conversión en `vsdata/`) son los mismos de siempre: no hace falta volver a copiarlos.

## Cómo saber que funciona

Al arrancar, el runner comprueba que el código traducido corresponde a los datos que hay en la consola (una huella de los scripts). Si coinciden, `ux0:data/valdis/save/vsrunner.log` dice `[aot] codigo nativo activo: 6072 de 6072 scripts`. Si no coinciden (otra versión del juego), lo dice en el log e interpreta todo: funciona igual, pero sin la mejora de velocidad.

Con TRUCOS (SELECT + L + R) → contador de rendimiento puedes ver los fps y el tiempo de lógica en las salas pesadas (sewer_11, outer_slum_2).

## Problemas frecuentes

| Mensaje | Solución |
|---|---|
| `falta la variable VITASDK` | Define `export VITASDK=...` con la carpeta donde instalaste VitaSDK |
| `falta ninja` / `falta cmake` | Instálalos (ver arriba). En MSYS2, usa la terminal **UCRT64**, no la "MSYS" |
| `AVISO: data.win con huella desconocida` | No es la build 722041 de Steam. Puede funcionar, pero no está probado |
| El compilador se cierra o el PC se queda sin memoria | Vuelve a lanzarlo con `-j 2`; continúa donde se quedó |
| `vita-elf-create` falla con 0xC0000374 | Fallo conocido del SDK: cambia el tamaño de `vs_pad_probe` al final de `runner/src/gfx.cpp` y vuelve a lanzar el script |
