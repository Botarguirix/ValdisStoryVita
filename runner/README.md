# Valdis Story PSVITA — runner nativo

Es un runner propio de GameMaker Studio 1.x (bytecode 13) escrito en C++17 que ejecuta el bytecode
**original** del `data.win` de Steam. No es un emulador ni un wrapper Android y no necesita plugins de taiHEN.

> ⚠️ El repositorio y el VPK **no contienen nada del juego**. Necesitas tu copia legal de
> *Valdis Story: Abyssal City* en Steam (probado con la build 722041).

## Instalación en la Vita
1. Instala `build/vita/vsvita.vpk` con VitaShell (Title ID `VALDIS001`).
2. Comprueba que existe `ur0:data/libshacccg.suprx`, el compilador de shaders de vitaGL. Si no está, ábrelo una vez con ShaRKBR33D.
3. Copia el **contenido** de tu carpeta de Steam del juego (`data.win`, `data_1`, `data_2`, `data_5` y los
   `music_*.ogg`; el `.exe` y las `.dll` no hacen falta) a `ux0:data/valdis/game/`.
4. Abre el juego. La primera vez convierte `data.win` en la propia consola, con una barra de progreso:
   genera `ux0:data/valdis/vsdata/` (código, sprites, salas, sonidos y texturas DXT) y un `.png.vdx` junto
   a cada imagen de `data_*`. Necesita unos 300 MB libres. Si se interrumpe, el siguiente arranque continúa
   donde se quedó (marca `vsdata/convirtiendo`). Para regenerarlo todo, borra `ux0:data/valdis/vsdata/`.

   Alternativa en PC: `python -I tools/build_vsdata.py` genera lo mismo en `build/ux0_data_valdis/`.
   `vsconv all <data.win> <carpeta_steam> <salida>` (herramienta de PC) usa el mismo convertidor que la Vita.
5. Las partidas se guardan en `ux0:data/valdis/save/`, junto con `vsrunner.log` (los avisos del runner).

### Controles (el juego ve un mando)
| Vita | Mando del juego |
|---|---|
| Cruz / Círculo / Cuadrado / Triángulo | face1 / face2 / face3 / face4 |
| L / R | L2 / R2 (gatillos) |
| Panel trasero (izquierda / derecha) | L1 / R1 (hombros) |
| Cruceta y stick izquierdo | movimiento |
| Select / Start | Select / Start |

Los botones se pueden reasignar en el menú de controles del propio juego.

## Arquitectura
| Archivo | Contenido |
|---|---|
| `tools/export_vsdata.csx` | Exportador (corre en UndertaleModCli): data.win → `game.vsd` + `tex/*.png` + `snd/*.wav` |
| `src/vsd.*` | Lector del formato VSD |
| `src/vm.cpp` | Intérprete del bytecode (pila, variables, `with`, llamadas, arrays 2D de GMS1) |
| `src/runtime.*` | Instancias, eventos con herencia, rooms (persistentes), vistas, colisiones, variables builtin |
| `src/fn_core.cpp` | Funciones: matemáticas, cadenas, instancias, tiles, `sprite_add`/`background_add`, INI, teclado, mando, ds y Steam (stubs) |
| `src/fn_draw.cpp` | Escena por profundidad, fondos, tiles, sprites, primitivas y fuente `font_add_sprite` |
| `src/particles.cpp` | Partículas `part_*` |
| `src/audio.*`, `src/fn_audio.cpp` | Mezclador software (WAV + OGG en streaming) y `audio_*` |
| `src/gfx.*` | GL de función fija (SDL2 en PC, vitaGL en Vita), batching, páginas DXT+RLE (`.vdx`) con LRU y un hilo cargador (núcleo 2) |
| `src/main_pc.cpp` / `src/main_vita.cpp` | Plataformas |

Cobertura: las **187/187** funciones builtin y las 51 variables builtin que usa el juego.

## Compilar
```bash
# PC (MSYS2 ucrt64)
cmake -S runner -B build/pc -G Ninja && cmake --build build/pc
# Vita
cmake -S runner -B build/vita -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVITA=ON && cmake --build build/vita
```

Prueba en PC (ventana de 960×544, igual que la Vita; F10 sale):
```bash
build/pc/vsrunner.exe build/ux0_data_valdis/vsdata build/ux0_data_valdis/game build/save
```
Opciones de prueba:
- `--frames N`: sale tras N frames.
- `--turbo`: sin esperar a vsync.
- `--shot FRAME ruta.bmp`: guarda una captura.
- `--key FRAME VK`: pulsa una tecla.
- `--hold FRAME VK DUR`: mantiene una tecla.
- `--onroom ROOM DELAY VK`: pulsa una tecla al llevar DELAY frames en ROOM.
- `--warp ROOM DELAY DESTINO`: salta a DESTINO al llevar DELAY frames en ROOM (depuración).
- `--setpos FRAME X Y`: mueve al jugador (depuración).
- `--profcode`: perfil por script/builtin; también se vuelca en cada cambio de room.
- `--prof`: escribe una línea `[perf]` por segundo (fps, ms de lógica y render, texturas cargadas).
- `--budget MB`: límite de memoria de texturas (la Vita usa 140).

## Código nativo (AOT)
`vsaot` traduce el bytecode de `game.vsd` a C++ (`build/aot_gen/`, ~35 MB) y CMake lo compila dentro del runner.
El resultado es idéntico al intérprete: con `--det SEMILLA --hash N` el runner de PC imprime una huella del estado
cada N frames, y `--noaot` fuerza el intérprete para compararlas. Al arrancar, el runner comprueba que `game.vsd`
coincide con el usado al generar (huella del código); si no, interpreta todo.
```
build/pc/vsaot.exe <vsdata>/game.vsd build/aot_gen
cmake -S runner -B build/vita ... && cmake --build build/vita
```
⚠️ Un VPK con AOT contiene código derivado de tu copia del juego: es solo para uso personal, no lo compartas.
Sin `build/aot_gen` (o con `-DVS_AOT=OFF`) se compila el runner normal, que no contiene nada del juego.

## Rendimiento
En la Vita, `vsrunner.log` registra las líneas `[perf]` cuando baja de 50 fps o se cargan texturas, y
`frame lento` cuando un frame pasa de 200 ms. Envía ese log si algo va lento.

Los guardados (.ini) y el log se escriben en un hilo aparte (`src/bgio.cpp`): el juego hace un autoguardado
de ~40 KB en cada cambio de sala y escribirlo en la tarjeta bloqueaba el frame 1-2 s.

Texturas: un hilo (núcleo 2) lee y descomprime las páginas `.vdx`; el hilo principal solo las sube a la GPU. Lo que
aún no está cargado no se dibuja durante 1–2 frames. Al entrar en una sala, lo visible se espera (primero en la cola), y
2 s después se precargan las salas vecinas (id ±1, ±2). La música (ogg) se abre en el hilo de carga de audio.
Variable de entorno `VS_SYNCTEX` (PC): todo en el hilo principal, como antes.

Archivos de depuración en `ux0:data/valdis/save/` (créalos vacíos y bórralos al terminar):
- `perfverbose`: una línea `[perf]` por segundo, más el desglose `[sala]` de funciones y scripts en cada entrada a sala.
- `warp.txt`: líneas `SALA FRAMES DESTINO` (como `--warp` en PC). **El juego autoguarda al cambiar de sala**:
  haz copia de los `vvv*.dat` antes.

Perfilador detallado: crea un archivo vacío llamado `prof` en `ux0:data/valdis/save/`. Cada 600 frames el log
incluye líneas `[prof]` con el tiempo por fase, por script GML y por función builtin. Bórralo para desactivarlo.

Teclado del juego en PC: flechas para moverse (arriba salta), D ataca/acepta, A bloquea/cancela, S magia, Q asistencia, Esc menú.
