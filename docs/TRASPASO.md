# Traspaso completo del proyecto Valdis Story PSVITA

Documento para continuar el proyecto en **otro PC** con una sesión nueva de Claude, que no tiene la memoria ni la conversación de este equipo. Reúne todo lo visto hasta el **19-11-2026**: qué es el proyecto, cómo está hecho, cómo se trabaja, qué se midió, qué se arregló, qué decidió el usuario, las reglas y el roadmap completo (copiado al final).

> **Para Claude en el PC nuevo:** lee este archivo entero antes de hacer nada. Luego monta el entorno con [INSTALAR_PC_NUEVO.md](INSTALAR_PC_NUEVO.md). Las reglas de la sección 3 son del usuario y se cumplen siempre. Habla con el usuario en **español**.

Índice: 1. Resumen · 2. Estado actual · 3. Reglas del usuario · 4. Cómo está hecho · 5. Cómo se trabaja (PC, Vita, agente) · 6. Historia de la sesión: hallazgos y arreglos · 7. Publicación en GitHub · 8. Pendientes inmediatos · 9. Mapa de documentos · 10. Roadmap completo · 11. Herramientas de prueba (copia de TESTING.md)

---

## 1. Resumen

- **Qué es:** port nativo de *Valdis Story: Abyssal City* (Steam, App ID 252030, build **722041**, GameMaker Studio 1.x, bytecode 13) a PS Vita. No es emulador ni YoYo Loader: es un **runner propio en C++17** que ejecuta el bytecode original del `data.win` del usuario.
- **Datos:** el juego se convierte desde la copia de Steam del usuario, en la propia Vita (primer arranque) o en PC (`tools/build_vsdata.py`). `data.win` SHA-256 `0a9c913393a30039f696df796d94f0b290c2871febb44a58ad9178f5647d010f`.
- **Dos versiones:**
  - **Intérprete** (`build/vita_noaot`, ~1,4 MB): solo el motor, se puede publicar. Es la del Release.
  - **AOT** (`build/vita`, ~15 MB): el bytecode traducido a C++ (`vsaot`), 2–2,5 veces más rápido en lógica. Lleva código derivado del juego: **solo uso personal, nunca se publica**.
- **Hecho 100 % con IA ("ULTRA IA VIBECODED")**: Claude Opus 5.5 (principal) y Claude Sonnet 5.5 (subagentes) en Claude Code. El usuario dirige y prueba en su Vita.
- **Repo público:** <https://github.com/Botarguirix/ValdisStoryVita> · Release **v0.5.0-alpha** (pre-release) publicado el 19-11 con el VPK de intérprete (SHA-256 `48252de9a54c01c70390856150cdef838b5e694fe6b70714f8d38b094f2b8576`).
- **Title ID:** `VALDIS001`. Carpeta en la Vita: `ux0:data/valdis/` (`game/` copia de Steam, `vsdata/` convertido, `save/` partidas + log + archivos de prueba, `pruebas/` donde se apartan cosas).

## 2. Estado actual (19-11-2026)

- Jugable de principio a fin en teoría; **campaña completa todavía no jugada en la Vita**. El usuario va a jugarla ahora con la versión AOT para ver hasta dónde llega.
- **Rendimiento en la Vita (AOT, 500/222 MHz):**
  - Recorrido de las 245 salas: 211 sin ningún frame perdido, 0,32 % perdidos en total. Con Gilda pulsando botones al azar: 245/245, 0,81 %.
  - Salas más pesadas (outer_slum_2, sewer_11, temple_5) a 60 fps; Azudor a 60.
  - Jefes en Boss Rush de prueba: Azudor, Shin Azudor, Shu, Jahzracht, Gernot y Myrgato sin fallos (0,2–2,1 % perdidos). Ragano en su sala real (lab_6): 60 pasos/s con AOT (0,3 %) y con intérprete (0,4 %).
  - Coste por enemigo activo: ~0,9 ms de lógica. Margen de la sala ÷ 1 ms = enemigos extra antes de bajar de 60.
  - Intérprete: Azudor 57–59 fps; sewer_11 ~34 fps.
- **Estabilidad:** 4 personajes recorriendo 245 salas en PC sin cuelgues; Gilda en la Vita sin cuelgues. Un único congelamiento visto (Boss Rush con Ragano dentro de la Arena del pueblo, 1 de 3 intentos), no repetido; se dejó de lado al decidir que el Boss Rush no usará la Arena.
- **En la Vita del usuario:** instalada la versión AOT con la LiveArea personal (emblema "VS" del `.exe`), **sin archivos de prueba** en `save/` (apartados en `pruebas/cheat_*`). Las partidas viejas tienen datos de pruebas; el usuario empezará partida nueva.
- **Memoria:** RAM ~317 de 335 MB; el log `[memoria]` cada minuto (heap y memoria libre de vitaGL) está en la build actual. Falta una sesión larga para descartar fugas.

## 3. Reglas del usuario (obligatorias)

**Proyecto**
- Prioridad: **estabilidad y terminar el juego 1:1** (igual que el original). Las partidas no importan: se pueden mover, borrar o bajar una avanzada si hace falta.
- Si hace falta cambiar el código para tener herramientas, se hace. **Las herramientas de prueba no se descartan:** al final serán una sección de **TRUCOS** en el juego.
- Después de terminar: menú de trucos, **Boss Rush** jugable, **masterquest** (enemigos nuevos/extra), **randomizer**, editor de inventario/partida (todos los objetos, armas, hechizos, habilidades).
- **Boss Rush (decisión del 19-11):** cada jefe se pelea **en su propia sala** y al pasar a la siguiente pantalla se carga la del siguiente jefe. **No usar la Arena del pueblo.** Si da problemas, se deja y se sigue con el roadmap.
- **Rendimiento:** objetivo 60 fps; en las salas más exigentes se acepta **mínimo 30 fps**, solo si la lógica sigue a 60 pasos/s (salto de dibujo activo). Clasificación: ✅ ≥ 58 · 🟡 30–58 (solo salas exigentes) · ❌ < 30.

**Reglas generales de los ports a Vita (dadas por el usuario para todos sus proyectos)**
- Antes de programar, revisar documentación actual (VitaSDK, vitaGL, psp2-tooling, wiki de Vita) y resumirla.
- Herramientas: VitaSDK, vitaGL, vita-agent-use, DMR (hasta 381 MiB de RAM), CapUnlocker (4.º núcleo). Si algo no se puede instalar, decir cuál y proponer alternativa.
- Solo copias legales del usuario; parar si los archivos parecen piratas.
- Memoria: pico < 381 MiB de RAM y < 112 MiB de VRAM, comunicado en cada build.
- Relojes por defecto **500 MHz CPU / 222 MHz GPU** (PSVshell). Subirlos lo hace el usuario: avisar siempre de **calor y desgaste**.
- Usar los 4 núcleos y reportar la carga por núcleo. Audio < 1 ms por frame.
- Logs con etiquetas `[CPU-bound]`, `[GPU-bound]`, `[VMEM-bound]`, `[RAM-bound]`, `[bandwidth-bound]` y la causa concreta.
- Probar **todo el juego**, no solo el título. Desplegar cada build, hacer captura y describirla. Diagnosticar antes de arreglar. Lista de lo verificado en hardware frente a lo no verificado. **Nunca decir "funciona" si no se vio en la Vita.** Ser honesto con los números.
- Actitud: animar; plantear objetivos exigentes una vez, con números, y dar opciones.

**Seguridad y privacidad**
- Nunca subir nada del juego (datos, `decomp/`, `build/`, VPK AOT, arte personal `runner/vita/sce_sys_personal/`), ni rutas con el usuario de Windows, IPs o el correo personal. Commits con el correo *noreply* `332337963+Botarguirix@users.noreply.github.com`. Revisar el **historial** antes de publicar ([ESTRUCTURA_REPOS.md](ESTRUCTURA_REPOS.md)).
- Descargas: pedir permiso diciendo nombre, origen y tamaño.
- **No tocar `ur0:tai/config.txt`** sin permiso explícito.
- La cuenta de GitHub tiene verificación en dos pasos: Claude **no inicia sesión**. `git push` desde la terminal sí funcionó en el PC original (credenciales guardadas por GitHub Desktop); los Releases se crean en la web con la sesión del usuario (el navegador del panel no puede adjuntar archivos: el VPK lo arrastra el usuario).
- Borrados permanentes: confirmar antes.

**vita-agent (control de la Vita desde el PC)**
- **Nunca dos clientes del agente a la vez** (admite una sola sesión).
- No regenerar ni borrar `~/vita-agent-config` en WSL (emparejamiento).
- PS + SELECT detiene el agente. Emparejamientos y permisos (ACL) los aprueba el usuario en la consola.
- **Nunca `fs trash`** sobre una ruta que se vaya a reutilizar: la papelera actúa tarde, por ruta, y borra el archivo nuevo (así se perdieron partidas dos veces). `fs move` no sobrescribe. Apartar con `fs move` a `ux0:data/valdis/pruebas/<nombre único>`.
- **No descargar `vsrunner.log` con el juego abierto** (bloquea el log). Cerrar la app, mover el log a `pruebas/log_<hora>.txt` y descargar esa copia.
- Los archivos de prueba en `save/` se **quitan** (moviéndolos a `pruebas/`) antes de que el usuario juegue.

**Trucos de trabajo**
- En Bash, los heredoc rompen los `\n` dentro de cadenas de C: escribir los parches con la herramienta Write/Edit o con un script de Python que use un marcador.
- Cambiar `runtime.h` o `aot_rt.h` recompila todo el AOT (~30 min): mantener el PC despierto. `gfx.h` y los `.cpp` son rápidos.
- Al compilar en Git Bash: `export PATH=/c/msys64/ucrt64/bin:$PATH` (si no, `vsrunner.exe` sale sin imprimir nada por faltar DLL).

## 4. Cómo está hecho

| Parte | Archivos |
|---|---|
| Lector del formato propio `game.vsd` | `runner/src/vsd.*` |
| Conversión `data.win` → `vsdata/` (en la Vita y en PC) | `runner/src/convert*.cpp`, `runner/tools/vsconv.cpp`, `tools/build_vsdata.py`, `tools/export_vsdata.csx` |
| Intérprete del bytecode | `runner/src/vm.cpp` (con `vmCallFn` en línea y `vmCallFnSlow` para perfiles) |
| Instancias, eventos, salas, colisiones, variables integradas | `runner/src/runtime.*` (rejilla de colisiones por objeto, caja de colisión cacheada por instancia, `instance_id`, código de sala con instancia "dummy") |
| Variables por objeto | `runner/src/varstore.h` (cada objeto tiene su tabla de variables; cada instancia un vector denso) |
| Funciones de GameMaker (187/187) | `runner/src/fn_core.cpp`, `fn_draw.cpp`, `fn_audio.cpp`, `particles.cpp` |
| Gráficos | `runner/src/gfx.*`: vitaGL / SDL2, páginas DXT comprimidas con LZ4 (VDX4), hilo cargador en el núcleo 2, precarga de salas vecinas, re-subida entre frames de las páginas subidas a mitad de frame, tiras > 4096 px partidas, modo descarte (salto de dibujo) |
| Audio | `runner/src/audio.*`: mezclador por software, WAV en memoria con cargador en segundo plano, OGG en streaming, máximo 64 voces, `lengthSeconds` solo lee la cabecera del WAV |
| Traductor AOT | `runner/tools/vsaot.cpp` → `build/aot_gen/` (C++), `runner/src/aot_rt.h` |
| Plataformas | `runner/src/main_vita.cpp` (salto de dibujo, muestreador en el núcleo 2, log `[memoria]` cada minuto), `main_pc.cpp` |
| Herramientas de prueba / futuros trucos | `runner/src/debugtour.*` (tour, stress, godmode, monkey, unlockchars, bossrush, drawdebug) |
| Inspección de datos | `runner/tools/vsdinfo.cpp` (`rooms`, `objects`, `roominst SALA`, `roompos`, `code NOMBRE`, `strings`, `collisions`, `bg N`, `roombgs SALA`, `sprframes NOMBRE`, `spritepages`, `sounds`) |
| LiveArea | `tools/make_livearea.py` (original, pública), `tools/make_livearea_personal.py` + `tools/extract_exe_icon.py` (personal, con el emblema del `.exe`) — ver [LIVEAREA.md](LIVEAREA.md) |
| Imagen de controles | `tools/make_controls.py` → `docs/controls.png` |
| Scripts de la Vita | `tools/va.sh` (agente), `tools/deploy.sh` (instala), `tools/restore_saves.sh`, `tools/tour_report.py`, `tools/vita_input/*.json` |

**Controles por defecto** (del juego con mando; reasignables en su menú): ✕ saltar/aceptar · ○ bloquear/cancelar · □ atacar · △ ataque 2 · R magia · L asistencia · R + ○ menú de hechizos · panel trasero izquierdo = mapa (L1), derecho libre (R1) · START menú · SELECT libre. Constantes de GameMaker: 32769 ✕, 32770 ○, 32771 □, 32772 △, 32773 L1, 32774 R1, 32775 L2, 32776 R2, 32777 SELECT, 32778 START. El runner mapea L/R físicos a L2/R2 y el panel trasero a L1/R1.

El contador verde de fps de la esquina **no es del juego**: es el overlay del plugin PSVshell.

## 5. Cómo se trabaja

### 5.1 Compilar

```bash
export PATH=/c/msys64/ucrt64/bin:$PATH
cmake --build build/pc --target vsrunner                  # PC
export VITASDK=/c/vitasdk PATH=/c/vitasdk/bin:$PATH
cmake --build build/vita -j 8                             # Vita AOT (personal)
cmake --build build/vita_noaot -j 8                       # Vita intérprete (público)
```

### 5.2 Regla de determinismo
Toda optimización debe dar **las mismas huellas**: el runner de PC con `--det 1234 --hash 10` imprime 300 huellas del estado en un escenario fijo; se comparan con las de referencia. También `--discard-every N` comprobó que el salto de dibujo no cambia la lógica. Escenario usado (claves: 68 = D, 13 = Enter, 39/37 = flechas):

```bash
K="--key 400 68 --key 500 68 --key 600 68 --onroom profile_room 60 68 --onroom char_room 60 39 --onroom char_room 120 68"
build/pc/vsrunner.exe <vsdata> build/ux0_data_valdis/game <copia de partidas> --turbo --det 1234 --hash 10 --frames 3000 $K \
  --warp outer_slum_1 300 outer_slum_6 --setpos 1000 1100 1000 --hold 1000 39 400 --hold 1450 68 30 --hold 1500 37 300 --hold 1820 68 30 \
  --warp outer_slum_6 900 outer_slum_17 --hold 2000 39 200 --hold 2250 68 40 --hold 2400 83 20 --onroom outer_slum_17 500 27 --onroom status_room 120 27
```

Antes de optimizar en el PC nuevo, generar las huellas de referencia con el código actual y guardarlas.

### 5.3 Probar en la Vita (receta)
1. `tools/deploy.sh --no-launch` (o `VPK=build/vita_noaot/vsvita.vpk tools/deploy.sh --no-launch`).
2. Poner archivos de prueba en `save/`: subir con nombre único `u<hora>_<archivo>` y `fs move` a su nombre final. Comprobar con `tools/va.sh fs list ux0:data/valdis/save`.
3. Lanzar y navegar el menú: `tools/va.sh app launch VALDIS001`, esperar ~28 s, `input submit tools/vita_input/hold_CROSS.json` dos veces (el primero puede solo saltar la intro), y en la pantalla de partidas `tap_DOWN.json` ×3 + `tap_CROSS.json` para la partida 4. Partida nueva: título → DOWN → ✕ (New Game) → ✕ (Normal) → selección de personaje (RIGHT×k) → ✕ → espacio libre → ✕.
4. Capturas: `tools/va.sh screen capture --output <ruta WSL>.jpg`.
5. Cerrar (`app close VALDIS001`), mover `vsrunner.log` a `pruebas/log_<hora>.txt`, descargarlo con `fs download` y leer las líneas `[jefe]`, `[recorrido]`, `[perf]`, `[memoria]`, `[aviso]`.
6. Quitar los archivos de prueba (moverlos a `pruebas/cheat_<hora>_*`).

El agente corre en WSL (Ubuntu) y `tools/va.sh` lo llama con la ruta de `tools/local.env`. Ver [INSTALAR_PC_NUEVO.md](INSTALAR_PC_NUEVO.md) §8.

### 5.4 Perfilar
- En la Vita: `save/prof` (por script, infla la lógica), `save/proffases` (casi gratis), `save/sampler` (muestreo en el núcleo 2), `save/perfverbose`.
- En PC: `--profcode` (tiempo por script y por función en cada cambio de sala). El PC es ~20–30 veces más rápido que la Vita por enemigo: medir siempre en la consola.

## 6. Historia de esta sesión: hallazgos y arreglos (orden aproximado)

| Tema | Qué pasó | Resultado |
|---|---|---|
| Ataques enemigos sin animación | El primer uso de una animación pedía su página en mitad del combate; `anim_kudir` (6220 px) y `fx_life_blade` (4235 px) superan el máximo de textura | Precarga de la familia `anim_<nombre>_*`; tiras partidas en trozos. ✅ PC, falta verlo en la Vita |
| sewer_11 a 60 fps | Cada enemigo probaba ~320 bloques en colisiones | Rejilla de colisiones por objeto; ✅ 60 fps fijos |
| Audio bloqueaba el hilo principal | El mezclador tenía el lock mientras mezclaba | Copia las voces y mezcla sin el lock; tope de 64 voces |
| Tirones al entrar en salas vecinas | La precarga consultaba la memoria de vitaGL por cada tile | Cacheado por frame |
| Límite de enemigos | Medido con `stress.txt` en sewer_11 | ~0,9 ms por enemigo activo; tabla en el roadmap 1.9 |
| Variables por objeto (VarStore) | Optimización de acceso a variables | −4 a −10 %; menos de lo estimado (se dijo al usuario) |
| Caja de colisión cacheada, lectura directa de variables integradas | | −8 a −13 % juntas |
| Salto de dibujo (dibujo a 30 fps si la lógica no llega) | Pedido por el usuario con la regla de 30 fps | Implementado y verificado: huellas idénticas; más pasos/s con sobrecarga moderada |
| Retratos vacíos (página 59) | Páginas subidas a mitad de frame quedaban vacías en la GPU | Se vuelven a subir al empezar el frame siguiente. ✅ Vita |
| Código de creación de sala sin `self` | El modo Arena perdía variables | Instancia "dummy" (id 0), como GMS1 |
| `instance_id` | Faltaba la variable integrada | Añadida |
| Log en ruta equivocada | `flushLog` antes de conocer la carpeta de partidas | Orden corregido |
| Partidas borradas dos veces | `fs trash` + reutilizar ruta | Nuevo método: apartar con `fs move` (`tools/restore_saves.sh`) |
| `vita-elf-create` se caía (0xC0000374) | Por la disposición del binario | Relleno `vs_pad_probe` a 12288 |
| godmode no evitaba morir | El jugador no tiene `hp_max` | Se fija `hp` a 999999 cada frame |
| monkey abría el menú de hechizos | Magia + bloqueo a la vez | Nunca se pulsan juntos |
| `-Os` | 25 % más lento | Descartado |
| LiveArea | Imágenes PNG de 8 bits con paleta completa (si no, error 0x8010113D) | Original (pública) y personal con el emblema "VS" del `.exe` en la burbuja y el botón Iniciar. Guía [LIVEAREA.md](LIVEAREA.md) |
| Boss Rush en la Vita | 6 jefes bien; con Ragano un congelamiento (1 de 3) y lógica 13,5 ms | El coste venía de la Arena del pueblo (~17 NPC reaccionan a un enemigo). Ragano en su sala (lab_6): 60 pasos/s. Decisión: Boss Rush sin Arena |
| Tirón de 1,3 s al aparecer un jefe | `audio_sound_length` leía el WAV entero de la tarjeta y no guardaba el resultado | Solo cabecera + caché: 86 ms una vez. Huellas idénticas |
| Publicación | Repo, README "vibecoded", controles, guía de repos, noreply, historial limpio | Release v0.5.0-alpha publicado |

## 7. Publicación en GitHub

- Repo: `Botarguirix/ValdisStoryVita`, rama `main`. Guía general para cualquier port: [ESTRUCTURA_REPOS.md](ESTRUCTURA_REPOS.md). Pasos y notas del Release de este repo: [PUBLICAR.md](PUBLICAR.md).
- Antes de cada push: `git ls-files` sin nada del juego; buscar en el historial el usuario de Windows, IPs y rutas `C:/Users`; `git log --format='%ae' | sort -u` solo con el noreply.
- Releases: etiqueta `vX.Y.Z-alpha`, pre-release, adjuntar **solo** `build/vita_noaot/vsvita.vpk` probado en la Vita, notas en el orden de la guía.

## 8. Pendientes inmediatos (en orden)

1. Esperar el informe del usuario jugando la campaña con AOT (sala/jefe y qué pasó; leer el log con la consola cerrada).
2. Sesión larga (1–2 h) para descartar fugas con el log `[memoria]` (roadmap 1.6 / 3.2).
3. Revisar en la Vita las animaciones de ataque arregladas (1.7).
4. Avisos de lectura en instancia inexistente (2.3c).
5. Entradas en frío de 270–740 ms (1.1 / 1.11).
6. Suspender y reanudar (3.3, lo prueba el usuario).
7. Después del juego: trucos, Boss Rush por salas (tabla jefe → sala con `vsdinfo roominst`), masterquest, randomizer, editor.

## 9. Mapa de documentos

| Documento | Para qué |
|---|---|
| [README.md](../README.md) | Presentación pública, aviso legal, versión de Steam, controles, compilar |
| [INSTALAR_PC_NUEVO.md](INSTALAR_PC_NUEVO.md) | Montar todas las herramientas en un PC nuevo |
| [ROADMAP.md](ROADMAP.md) | Estado y pendientes (copiado abajo) |
| [TESTING.md](TESTING.md) | Herramientas de prueba / futuros trucos (copiado abajo) |
| [DATA.md](DATA.md) | Datos del juego y conversión |
| [NOTAS_DECOMP.md](NOTAS_DECOMP.md) | Lo aprendido decompilando (local) |
| [VIABILIDAD.md](VIABILIDAD.md) | Estudio inicial de opciones |
| [LIVEAREA.md](LIVEAREA.md) | Icono y LiveArea (guía base para todos los ports) |
| [ESTRUCTURA_REPOS.md](ESTRUCTURA_REPOS.md) | Cómo publicar cualquier port (plantilla) |
| [PUBLICAR.md](PUBLICAR.md) | Pasos y notas del Release de este repo |
| [runner/README.md](../runner/README.md) | Arquitectura del runner y opciones de PC |


---

## 10. Roadmap completo (copia de ROADMAP.md al 19-11-2026)

### Valdis Story PSVITA — Roadmap (lo que falta)

Actualizado: 2026-11-19 (noche; Release v0.5.0-alpha publicado). Objetivo: terminar el juego en la Vita con **todos los personajes** a **60 fps fijos**.

Leyenda: ✅ verificado EN LA VITA · 🖥️ solo probado en PC · ⬜ pendiente · 🔶 mejorado, pero queda algo

---

### 0. Verificado en la consola (vita-agent: capturas, entradas y medición)

| Qué | Dato medido en la Vita |
|---|---|
| Conversión del `data.win` de Steam en la propia Vita | ✅ pico de 49 MB de RAM |
| Código nativo (AOT) | ✅ 6072 de 6072 scripts |
| Combate de jefe (Azudor, outer_slum_7) | ✅ lógica ~4,5 ms + dibujo ~3 ms → **60 fps** |
| outer_slum_2, la sala más pesada (430 instancias) | ✅ lógica 8,2 ms + dibujo 4,3 ms → **60 fps** |
| outer_slum_6, 8, 9, 10, 5, 4 | ✅ recorridas con saltos de depuración, sin avisos de rendimiento |
| Personaje visible y texturas correctas | ✅ |
| Texturas en segundo plano | ✅ una página nueva en juego: 4–12 ms en el hilo principal (antes 70–100 ms) |
| Precarga de las salas vecinas | ✅ outer_slum_8, 9 y 10 entran sin leer ni una página |
| Entrada a salas grandes (outer_slum_2/6) | ✅ de 650 ms a menos de 2 frames (rejilla de colisiones e índice de tiles) |
| Música (ogg) | ✅ abrirla cuesta 0,0 ms en el hilo principal (antes 25–88 ms) |
| Texturas VDX4 | ✅ recompresión automática en la Vita; entrada en frío de 1,33 a 1,07 s |
| Menú principal, pausa/estado, Game Over, Retry | ✅ |
| Relojes, DMR, 4.º núcleo | ✅ 500/222 MHz, +64 MB, CapUnlocker activo |
| Núcleos | ✅ núcleo 0 ~45 % (77 % en outer_slum_2), 1 ~8 %, 2 ~1 %, 3 ~12 % |
| Memoria | ✅ RAM 317 de 335 MB; VRAM toda reservada por vitaGL, con ~140–155 MB libres dentro |
| Lógica igual que el intérprete | 🖥️ 300 huellas deterministas idénticas tras cada cambio |

---

### 1. Rendimiento

| # | Tarea | Estado | Notas |
|---|---|---|---|
| 1.1 | Entrada en frío a una sala que no es vecina (p. ej. al cargar partida) | 🔶 | 1,33 s → **1,07 s** con VDX4. Leer las 9 páginas cuesta ~740 ms y descomprimirlas ~340 ms. Las vecinas ya son inmediatas |
| 1.2 | Texturas comprimidas tipo LZ (VDX4) | ✅ | 94 → 53 MB. La Vita recomprime los datos antiguos sola al arrancar (~1 min, una vez). Leer y descomprimir en dos núcleos a la vez se midió peor (compiten por la memoria) y quedó desactivado |
| 1.3 | Primer frame del arranque ~1,8 s, más el logo de vitaGL de 10–20 s | ⬜ | Parte son esperas de texturas y parte la compilación de shaders; probar la caché de shaders de vitaGL |
| 1.4 | Primer frame de outer_slum_7: ~200 ms de lógica (intro del jefe) | ⬜ | Diagnosticar con `save/perfverbose` |
| 1.5 | Medir las zonas tardías | 🔶 | Medidas en la Vita las 12 salas más pesadas: lib_3, grave_13, skyway_3, outer_slum_11 y grave_2 van a 56–60 fps; temple_5 pasó de 53–54 a **60 fps** (lógica 15 → 8,6 ms con la caché de cajas de colisión) |
| 1.5b | sewer_11 | ✅ | **60 fps fijos durante 33 s** (lógica 9,2 ms + dibujo 4,7 ms; antes 52–61 fps con dibujo 6,8 ms). Cambios: rejilla de colisiones por objeto en la fase de colisiones (cada enemigo probaba los ~320 `block_normal`; la fase bajó a ~0,9 ms) y memoria reutilizada en primitivas y orden de dibujo. Medido con Wyatt (antes con Reina, en otra posición): no es una comparación exacta |
| 1.5c | Scripts gigantes (nme_v_demon 107 KB de código, nme_v_feral 90 KB) | 🔶 | La versión `-Os` (`build/vita_os`, código nativo de 36 a 21 MB) arranca bien en la Vita: el cuelgue de antes era el bloqueo de archivos del agente. Falta medir su rendimiento |
| 1.6 | Sesión larga (30–60 min) para descartar fugas de RAM o VRAM | ⬜ | |
| 1.8 | Recorrido completo (245 salas, 6 s cada una, `save/tour.txt`) | ✅ | 211 salas sin ningún frame perdido; 0,32 % de frames perdidos en total. Las más cargadas: temple_9 (11,8 ms), temple_14, garden_4, outer_slum_2 y sewer_11 (~11 ms), con 2,4–5 ms de margen. Ojo: con el jugador quieto y los enemigos que haya en la sala al entrar |
| 1.9 | Límite de enemigos (`save/stress.txt`, enemigos activos junto al jugador) | 🔶 | Cada enemigo activo cuesta ~0,6–1,1 ms de lógica en la Vita (~30 veces más que en PC: acceso a memoria). En sewer_11 (13,8 ms): +4 → 18,3 ms (empieza a bajar), +8 → 21 ms, +20 → 30 ms, +48 → ~20 fps. Regla: margen de la sala ÷ ~1 ms = enemigos activos extra antes de bajar de 60 |
| 1.10 | Arreglado en el recorrido | ✅ | (a) El mezclador de audio bloqueaba el hilo principal 22–35 ms por sonido con muchas voces (ahora no espera; límite de 64 voces). (b) Tirón de 200–360 ms a los 2 s de entrar en algunas salas (la precarga de vecinas consultaba la memoria de vitaGL por cada tile). (c) `-Os` medido: 25 % más lento que `-O3`, descartado |
| 1.11 | Quedan | ⬜ | Entradas en frío de 270–740 ms en algunas salas; hallow_2 crea ~190 instancias al entrar (574 ms); tirones sueltos de ~26 ms en sewer_14 y west_11 |
| 1.12a | Variables por objeto (`varstore.h`) | ✅ | Medido en la Vita: −4 a −10 % de coste por frame (sewer_11 13,8 → 13,2 ms; +12 enemigos 24,9 → 22,6 ms). Menos de lo estimado: el acceso a variables no era lo principal. Huellas idénticas |
| 1.12c | Caja de colisión cacheada en cada instancia + llamada directa a funciones integradas | ✅ | Medido en la Vita: collision_rectangle 7,8 → 5,0 % del tiempo, position_meeting 3,0 → 1,8 %; +24 enemigos 31,5 → 29,4 ms. Huellas idénticas |
| 1.12b | Lectura/escritura directa de variables integradas (x, y, object_index...) en el código nativo | ✅ | Junto con 1.12a, medido en la Vita: −8 a −13 % (sewer_11 13,8 → 12,7 ms; +4 enemigos 18,3 → 16,5 ms, ya dentro de 60 fps; +12 enemigos 24,9 → 21,6 ms). Huellas idénticas |
| 1.12d | Dibujo a 30 fps cuando la lógica no llega | ✅ | Si un frame se pasa de 15,5 ms, el siguiente ejecuta lógica y eventos Draw (42 de 166 cambian variables: no se pueden saltar) pero no manda nada a la GPU; nunca dos seguidos. Huellas idénticas en PC descartando 1 de cada 2 y 1 de cada 3 frames. Vita, sewer_11 con enemigos extra (pasos de lógica/s): +4 54 → 58, +8 43 → 52, +12 41 → 41, +24 28 → 32. Ayuda con sobrecarga moderada; con mucha, el límite es la lógica. Se desactiva con `save/nodrawskip` |
| 1.12 | Optimizar el coste por enemigo | ⬜ | Opciones: variables por objeto en posiciones fijas resueltas por el AOT (el gran salto, 20–40 % de la lógica estimado, trabajo grande); dibujo a 30 fps cuando la lógica no llega (el juego no se ralentiza, se ve menos fluido; opcional); atajos en `nme_crowding_script` (poco) |
| 1.7 | Ataques sin animación o sprite (aviso del usuario) | 🖥️ | Dos causas: (a) el primer uso de una animación pedía su página en mitad del combate y no se dibujaba durante ~8 frames (visto en los registros de la Vita: "sin listar a tiempo 2" contra Azudor). Ahora se precarga la familia entera `anim_<nombre>_*` al cambiar de sprite, y los objetos variantes (siren_ice, shin_azudor, evil_dhistahn, ruckjahl, assassin_fire…) encuentran sus animaciones. (b) `anim_kudir` (6220 px) y `fx_life_blade` (4235 px) eran más anchas que el máximo de textura (4096): ahora se parten en trozos de frames enteros. Comprobado en PC con captura; falta verlo en la Vita |

### 2. Juego completo (regla: probar todo el juego)

| # | Tarea | Estado |
|---|---|---|
| 2.1 | Wyatt: campaña completa | 🖥️ empezada |
| 2.2 | Reina: campaña completa | ✅ hasta el jefe 1 |
| 2.3 | Gilda y Vladyn (se desbloquean jugando) | 🖥️ elegibles con `unlockchars`; partida nueva + intro + recorrido de 245 salas con `monkey` y `godmode` sin cuelgues |
| 2.3b | Prueba de estabilidad por personaje (PC, 19-11) | 🖥️ Wyatt, Reina, Vladyn y Gilda: partida nueva, intro del barco y recorrido de las 245 salas pulsando botones al azar: 0 cuelgues, 0 muertes, sin avisos nuevos. Falta repetirlo en la Vita |
| 2.3e | Texturas vacías en la Vita al entrar en una sala | ✅ | Las páginas que se suben a mitad de un frame (al entrar en una sala, lo visible se carga mientras se dibuja) a veces quedaban vacías en la GPU: retratos de Wyatt y Vladyn en la selección de personaje (página 59). Subirla otra vez entre frames lo arregla siempre (comprobado con capturas). Ahora se vuelve a subir al empezar el frame siguiente. Puede explicar otros "sprites invisibles" vistos antes |
| 2.3c | Avisos pendientes de revisar | ⬜ Lecturas de variables en una instancia que no existe dentro de `nme_v_player_script`, `nme_v_feral_script`, `hermit_Step_0`, `pressure_switch_Step_0` y `save_player_script` (una vez por partida). En GMS1 eso sería un error: comprobar si es una diferencia del runner |
| 2.3d | Boss Rush de prueba (PC, 19-11) | 🖥️ 36 jefes y rivales (azudor … abbigail, ruckjahl, anemone, los 4 personajes rivales) 30 s cada uno en arena_1 con monkey y godmode: 0 cuelgues, 0 frames perdidos. Encontró un fallo real: el código de creación de sala se ejecutaba sin `self` (el modo Arena perdía sus variables); corregido con una instancia "dummy" (id 0), como GMS1. **Vita (19-11, AOT):** 6 jefes sin fallos (Azudor … Myrgato, 0,2–2,1 % perdidos); con Ragano el juego se congeló una vez (1 de 3 intentos). Medido después: el coste venía de meter el jefe en arena_1, el pueblo con ~17 NPC que reaccionan a un enemigo (13,5 ms de lógica, 42 pasos/s); sin jefe arena_1 va a 60 pasos/s (4,4 ms) y Ragano **en su sala real (lab_6) va a 60 pasos/s, 0,1 % perdidos**. Corregido el tirón de 1,3 s al aparecer: `audio_sound_length` leía el wav entero de la tarjeta y no guardaba el resultado; ahora lee solo la cabecera y lo recuerda. **Decisión del usuario:** el Boss Rush deja la Arena; cada jefe se pelea en su propia sala (ver 8.5) |
| 2.4 | Todos los jefes y zonas, con una lista de salas probadas en la Vita | 🔶 Las 245 salas recorridas en la Vita (1.8); jefes en combate real: Azudor, y Ragano en su sala (lab_6, 60 pasos/s); Boss Rush de prueba: 6 jefes más sin fallos |
| 2.5 | Guardar y cargar, autoguardado entre salas, finales | 🖥️ (carga ✅) |
| 2.6 | Menús: hechizos, habilidades, objetos, mapa, opciones y lore | 🖥️ (pausa ✅) |
| 2.7 | Audio: toda la música y los efectos, bucles, volumen de las opciones | 🖥️ |

### 3. Estabilidad y memoria

| # | Tarea | Estado |
|---|---|---|
| 3.1 | Pico de RAM por debajo de 381 MiB y de VRAM por debajo de 112 MiB, comunicado en cada build | ✅ se registra en cada prueba |
| 3.2 | Sin cuelgues en sesiones largas | ⬜ |
| 3.3 | Suspender y reanudar la Vita en mitad de la partida | ⬜ |

### 4. Controles y experiencia

| # | Tarea | Estado |
|---|---|---|
| 4.1 | L/R → gatillos, panel trasero → hombros: comprobar que sea cómodo en combate | 🖥️ |
| 4.2 | Iconos de botones del juego | 🔶 Revisado: son teclas genéricas con la acción escrita (`Atk`, `Jmp`, `Def`, `Mag`, `Ok`, flechas), válidas en la Vita; solo `Esc` es de PC (en la Vita es Start). Mejora opcional: sprites propios con □ ✕ ○ △ y Start |
| 4.3 | Contador de fps verde | ✅ No es del juego: es el overlay del plugin PSVshell (se quita desde el plugin) |

### 5. Entrega (uso personal)

| # | Tarea | Estado |
|---|---|---|
| 5.1 | Instrucciones de instalación (README) | ✅ |
| 5.2 | El VPK con AOT lleva código derivado del juego: solo para ti, no se comparte | — |
| 5.3 | Versión sin AOT (intérprete), que podría compartirse sin el juego | ✅ `build/vita_noaot` (1,4 MB). Probada en la Vita: Azudor 57–59 fps, sewer_11 ~34 fps (lógica 2,4 veces más lenta que AOT). Es la del Release |
| 5.4 | Icono y LiveArea propios, sin arte del juego | ✅ Original dibujada por script (`tools/make_livearea.py`) para el VPK público; versión personal con el título y el logo de tu copia (`tools/make_livearea_personal.py`, no se sube). Guía: `docs/LIVEAREA.md` |

### 6. Herramientas de prueba en la Vita

| Archivo en `ux0:data/valdis/save/` | Efecto |
|---|---|
| `warp.txt` | Líneas `SALA FRAMES DESTINO`: salta de sala para medir sin jugar. **Ojo:** el juego autoguarda; haz copia de las partidas antes |
| `profphase` | Perfil ligero: tiempo por fase del frame y por script, con el código nativo activo |
| `perfverbose` | Una línea `[perf]` por segundo, más un desglose de funciones y scripts en cada entrada a sala |
| `prof` | Perfilador por script (líneas `[prof]` cada 600 frames) |
| `proffases` | Solo el tiempo de cada fase del frame, casi sin coste (`profphase` cronometra cada llamada e infla la lógica unas 3–4 veces) |
| `tour.txt` | Recorrido: `FRAMES n` y una sala por línea; escribe una línea `[recorrido]` por sala (fps, frames perdidos, peor frame). Informe: `python tools/tour_report.py vsrunner.log` |
| `stress.txt` | Estrés: `SALA CADA_FRAMES CUANTOS MAXIMO OBJ...` mete enemigos junto al jugador y escribe `[estres]` por tanda |
| `godmode` | El jugador no muere en combate (hp a 999999). El autoguardado se lo puede guardar: restaurar partidas |

**Cuidado con las partidas al probar con el agente:** nunca `fs trash` sobre una ruta que se vaya a reutilizar: la papelera actúa tarde y borra el archivo nuevo (así se perdieron las partidas dos veces el 19-11; restauradas desde `build/save_vita_bak_1119_0913`). `fs move` no sobrescribe y `fs download` puede devolver una copia vieja. Usa `tools/restore_saves.sh <copia>` (aparta con move, sube con nombre único) y comprueba con `fs list` y con Retry en el menú. Los archivos de prueba quedan en `ux0:data/valdis/pruebas/`.

**Fallo conocido del SDK:** `vita-elf-create.exe` a veces se cae (código 0xC0000374) según cómo queden las direcciones del binario. Si pasa, cambia el tamaño de `vs_pad_probe` al final de `runner/src/gfx.cpp` y recompila.

`tools/deploy.sh` instala por el puente (los VPK subidos van a la papelera del agente) y `tools/va.sh` ejecuta cualquier comando del agente.

---

#### Notas de reglas
- **Delta time:** Valdis Story (GMS1) está programado por frames a `room_speed` 60. El runner fija los 60 fps en lugar de escalar por delta, porque escalar cambiaría la física del juego.
- **Overclock:** los relojes por defecto son 500/222 MHz. Si quieres subirlos, hazlo tú mismo con PSVshell, sabiendo que da más calor y desgaste. Con 60 fps estables ahora no hace falta.

---

### 7. Reglas del proyecto (19-11)
- **Criterio de rendimiento para la cuenta final:** objetivo 60 fps. En las salas más exigentes se acepta un **mínimo de 30 fps**. Como el juego avanza por frames, eso solo es aceptable si la lógica sigue a 60 pasos por segundo y lo que baja es el dibujo (si no, a 30 fps el juego va a la mitad de velocidad): requiere la opción de "dibujo a 30 fps cuando la lógica no llega" (1.12). Clasificación por sala: ✅ ≥ 58 fps · 🟡 30–58 fps (aceptable solo en salas exigentes) · ❌ < 30 fps.
- **Prioridad:** estabilidad y terminar el juego 1:1 (igual que el original). Las partidas de la Vita no importan; para probar zonas finales se puede usar una partida avanzada.
- **Las herramientas de prueba se conservan** (`warp.txt`, `tour.txt`, `stress.txt`, `godmode`, perfiles): al final serán una sección de **TRUCOS** dentro del juego.

### 8. Después de terminar el juego (alcanzables)
| # | Idea | Notas |
|---|---|---|
| 8.1 | Menú de TRUCOS | Salto a cualquier sala, modo sin morir, contador de rendimiento, recorrido y estrés |
| 8.2 | Modo "masterquest" | Enemigos nuevos o extra en varias salas. Límite: ~1 ms por enemigo activo en la Vita (sección 1.9); diseñar cada sala dentro de su margen |
| 8.3 | "Randomizer" | Orden de objetos/enemigos aleatorio; depende de entender bien los scripts de progreso (globals de jefes y objetos) |
| 8.4 | Editor de partida / trucos de inventario | Todos los objetos, armas, hechizos, habilidades, oro y nivel. El juego guarda todo en variables GML (~400 con prefijos `item_`, `wep_`, `skill_`, `spell_`, `talent_`, además de `g_item`) y en el `.dat` de la partida: el runner puede escribirlas igual que hace el modo sin morir con `hp`. Antes hay que mapear qué significa cada variable y respetar los límites del juego para no romper la progresión |
| 8.5 | Modo **Boss Rush** | **Cada jefe en su propia sala** (Ragano en lab_6, etc.): al pasar a la siguiente pantalla se carga la sala del siguiente jefe. No usar la Arena del pueblo (sus NPC reaccionan al jefe y bajan el rendimiento). Pendiente: tabla jefe → sala (`vsdinfo roominst`), saltar escenas y puertas cerradas. Ya existe como herramienta de prueba (`save/bossrush.txt`, sección 6 y `docs/TESTING.md`); para jugarlo falta: menú, orden fijo, recuperar vida entre jefes y marcador. El propio juego tiene un modo Arena con jefes al azar (`arena_boss_script`) que sirve de base |

---

## 11. Herramientas de prueba (copia de TESTING.md)

### Herramientas de prueba (futuro menú de TRUCOS)

Se activan creando archivos en `ux0:data/valdis/save/` (en PC, en la carpeta de partidas que se pase al runner). Si no existen, el juego funciona normal. Al terminar el port pasarán a un menú de trucos dentro del juego (roadmap, sección 8).

| Archivo | Efecto |
|---|---|
| `warp.txt` | Líneas `SALA FRAMES DESTINO`: tras `FRAMES` frames en `SALA`, salta a `DESTINO`. Sirve para llegar a cualquier sala sin jugar |
| `tour.txt` | Recorrido: `FRAMES n` y una sala por línea. Visita cada sala y escribe una línea `[recorrido]` con fps, frames perdidos, peor frame e instancias. Informe: `python tools/tour_report.py vsrunner.log` |
| `stress.txt` | Estrés: `SALA CADA_FRAMES CUANTOS MAXIMO OBJ1 [OBJ2 ...]`. En `SALA` crea enemigos junto al jugador por tandas y escribe una línea `[estres]` por tanda |
| `unlockchars` | Los cuatro personajes en la selección (mantiene `global.unlock_char = 1`; el juego lo activa al desbloquear el lore 63) |
| `monkey` | Botones al azar en las salas de juego: moverse, saltar, atacar, magia, bloquear (nunca magia + bloqueo juntos, que abre el menú de hechizos). Con `godmode` y `tour.txt` prueba el código de cada personaje en todas las salas |
| `bossrush.txt` | Boss Rush de prueba: `SALA FRAMES JEFE1 JEFE2 ...` (p. ej. `arena_1 1800 azudor shu itahn ...`). Va a la sala y saca cada jefe junto al jugador; línea `[jefe]` con coste, frames perdidos, si sigue vivo y avisos nuevos. Con `monkey` y `godmode` hay combate real |
| `nodrawskip` | Desactiva el dibujo a 30 fps cuando la lógica no llega (para medir el coste real de cada frame) |
| `godmode` | El jugador no muere en combate (`hp` a 999999 en cada frame). El autoguardado puede guardar ese valor |
| `sampler` | Muestreador de perfiles: un hilo en el núcleo 2 anota cada 0,5 ms qué script y qué función integrada se ejecutan. Informe `[muestreo]` cada 10 s. Sin coste en el hilo principal |
| `proffases` | Tiempo de cada fase del frame (casi sin coste) |
| `profphase` | Fases + tiempo por script y función (infla la lógica 3–4 veces: solo para comparar proporciones) |
| `perfverbose` | Una línea `[perf]` por segundo y desglose al entrar en cada sala |
| `prof` | Perfilador por script con el intérprete (desactiva el código nativo) |

### En PC

`build/pc/vsrunner <vsdata> <carpeta_juego> <carpeta_partidas> [opciones]`:
`--turbo` (sin esperar al vsync), `--frames N`, `--warp SALA FRAMES DESTINO`, `--key FRAME TECLA`, `--hold FRAME TECLA DURACION`,
`--det SEMILLA` + `--hash N` (huellas deterministas del estado: comparar dos builds), `--bench FRAME`, `--sample archivo`, `--profcode`, `--noaot`.

**Regla:** cualquier optimización debe dar exactamente las mismas huellas (`--det --hash`) que la versión anterior.

### Con vita-agent (pruebas automáticas en la consola)

- `tools/va.sh` ejecuta comandos del agente (configura `tools/local.env`). Nunca dos clientes a la vez.
- `tools/deploy.sh` sube e instala el VPK. `tools/vita_input/*.json` son pulsaciones (`tools/va.sh input submit ...`; en el menú del título usar `hold_CROSS.json`).
- **No leer `vsrunner.log` mientras el juego corre:** descargarlo cada minuto bloqueó la escritura del registro en la Vita (el juego siguió funcionando, pero el registro se quedó congelado). Descargarlo solo al cerrar el juego.
- **Partidas:** nunca `fs trash` sobre una ruta que se vaya a reutilizar (la papelera del agente actúa tarde, por ruta, y borra el archivo nuevo). `fs move` no sobrescribe y `fs download` puede devolver una copia vieja: comprobar con `fs list`. `tools/restore_saves.sh <copia>` restaura de forma segura.
