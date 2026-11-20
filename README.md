# Valdis Story PSVITA

[Español](#español) · [English](#english)

---

## Español

Port nativo de **Valdis Story: Abyssal City** para PlayStation Vita. Es un *runner* propio de GameMaker Studio 1.x escrito en C++17 desde cero: máquina virtual, eventos, salas, dibujo, partículas y audio. No es un emulador ni usa YoYo Loader. El juego se convierte a partir de **tu propia copia de Steam** directamente en la consola.

**Versión 0.5.0-alpha** · Title ID `VALDIS001` · Estado: jugable, en pruebas. Ver [estado y roadmap](docs/ROADMAP.md).

### ⚠️ Aviso legal

- **Este proyecto NO apoya ni tolera la piratería.** Necesitas una **copia legal de Valdis Story: Abyssal City comprada en Steam**.
- El repositorio y el VPK **no incluyen el juego**: ni código, ni gráficos, ni música, ni textos. El VPK solo trae el motor. En el primer arranque, la consola convierte los archivos de tu copia de Steam.
- Probado con la **build de Steam 722041** (App ID 252030).
- Valdis Story es propiedad de Endless Fluff Games. Este es un proyecto de fans sin ánimo de lucro y sin relación con ellos ni con Sony.

### Dos versiones

| | VPK de Releases (intérprete) | Versión compilada (AOT) |
|---|---|---|
| Contenido | Solo el motor | Motor + código del juego traducido a C++ |
| Velocidad | Más lenta en salas cargadas | 60 fps en casi todo el juego (ver roadmap) |
| Cómo conseguirla | Descargar de [Releases](../../releases) | Compilarla tú desde tu copia (ver [Compilar](#compilar)). **No se puede compartir**: lleva código derivado del juego |

### Requisitos

- PS Vita o PS TV con **HENkaku / h-encore / Enso** y **VitaShell**.
- **`ur0:data/libshacccg.suprx`** (compilador de shaders de vitaGL), extraído con **ShaRKBR33D** en la propia consola.
- No hace falta ningún plugin de taiHEN. Recomendado: el 4.º núcleo libre con **CapUnlocker**.
- Espacio libre en `ux0:` para la copia del juego más unos **300 MB** de datos convertidos.

### Instalación

1. Descarga `vsvita.vpk` de [Releases](../../releases) e instálalo con VitaShell.
2. Copia el contenido de tu carpeta de Steam `steamapps/common/ValdisStoryAbyssalCity/` en `ux0:data/valdis/game/`: `data.win`, las carpetas `data_1`, `data_2`, `data_5` y los `music_*.ogg` (el `.exe` y las `.dll` no hacen falta).
3. Abre el juego. El primer arranque convierte los datos (unos minutos, con barra de progreso). Las partidas se guardan en `ux0:data/valdis/save/`.

### Controles

El juego ve un mando; los botones se pueden reasignar en el menú de controles del propio juego.

| Vita | Mando del juego |
|---|---|
| Cruz / Círculo / Cuadrado / Triángulo | face1 / face2 / face3 / face4 |
| L / R | L2 / R2 (gatillos) |
| Panel trasero (izquierda / derecha) | L1 / R1 (hombros) |
| Cruceta y stick izquierdo | Movimiento |
| Select / Start | Select / Start |

### Compilar

Requisitos: CMake 3.16+, [VitaSDK](https://vitasdk.org), y para PC MSYS2 (SDL2, zlib, libvorbis).

```bash
# Herramientas y runner de PC
cmake -S runner -B build/pc -G Ninja && cmake --build build/pc
# VPK sin código del juego (el de Releases)
cmake -S runner -B build/vita_noaot -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVS_AOT=OFF
cmake --build build/vita_noaot
```

Versión rápida (AOT) desde tu copia: `build/pc/vsconv` genera `game.vsd` desde tu `data.win`, `build/pc/vsaot` lo traduce a C++ en `build/aot_gen/`, y la build de Vita normal (`-DVS_AOT=ON`) lo incluye. Detalles en [runner/README.md](runner/README.md).

### Documentación

- [Roadmap, estado y medidas en la consola](docs/ROADMAP.md)
- [Herramientas de prueba (futuro menú de trucos)](docs/TESTING.md)
- [Datos y conversión](docs/DATA.md)
- [Arquitectura del runner](runner/README.md)
- [Viabilidad inicial](docs/VIABILIDAD.md) · [Notas de decompilación](docs/NOTAS_DECOMP.md)
- [Estructura común de los repos de ports](docs/ESTRUCTURA_REPOS.md) · [Cómo publicar](docs/PUBLICAR.md) · [Icono y LiveArea](docs/LIVEAREA.md)

---

## English

Native port of **Valdis Story: Abyssal City** to PlayStation Vita: a custom GameMaker Studio 1.x runner written from scratch in C++17 (VM, events, rooms, rendering, particles, audio). It is not an emulator and does not use YoYo Loader. The game is converted from **your own Steam copy** on the console itself.

**Version 0.5.0-alpha** · Title ID `VALDIS001` · Status: playable, under testing. See the [roadmap](docs/ROADMAP.md) (Spanish).

### ⚠️ Legal notice

- **This project does NOT support piracy.** You need a **legal Steam copy of Valdis Story: Abyssal City**.
- The repository and the VPK **do not include the game** (no code, graphics, music or text). On first boot the console converts the files from your Steam copy.
- Tested with **Steam build 722041** (App ID 252030).
- Valdis Story belongs to Endless Fluff Games. This is a non-profit fan project, not affiliated with them or Sony.

### Two builds

The VPK on [Releases](../../releases) is the **interpreter** build (engine only, shareable, slower in heavy rooms). The fast **AOT** build translates the game's code to C++ and must be **built by yourself** from your own copy; it cannot be shared.

### Installation

1. Install `vsvita.vpk` from [Releases](../../releases) with VitaShell. Requires `ur0:data/libshacccg.suprx` (extract it with ShaRKBR33D).
2. Copy the whole contents of `steamapps/common/ValdisStoryAbyssalCity/` into `ux0:data/valdis/game/`.
3. Launch the game. The first boot converts the data (a few minutes, with a progress bar).

### Building

See the Spanish section above (`cmake` commands) and [runner/README.md](runner/README.md).
