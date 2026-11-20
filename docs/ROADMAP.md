# Valdis Story PSVITA — Roadmap (lo que falta)

Actualizado: 2026-11-19 (tarde). Objetivo: terminar el juego en la Vita con **todos los personajes** a **60 fps fijos**.

Leyenda: ✅ verificado EN LA VITA · 🖥️ solo probado en PC · ⬜ pendiente · 🔶 mejorado, pero queda algo

---

## 0. Verificado en la consola (vita-agent: capturas, entradas y medición)

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

## 1. Rendimiento

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

## 2. Juego completo (regla: probar todo el juego)

| # | Tarea | Estado |
|---|---|---|
| 2.1 | Wyatt: campaña completa | 🖥️ empezada |
| 2.2 | Reina: campaña completa | ✅ hasta el jefe 1 |
| 2.3 | Gilda y Vladyn (se desbloquean jugando) | 🖥️ elegibles con `unlockchars`; partida nueva + intro + recorrido de 245 salas con `monkey` y `godmode` sin cuelgues |
| 2.3b | Prueba de estabilidad por personaje (PC, 19-11) | 🖥️ Wyatt, Reina, Vladyn y Gilda: partida nueva, intro del barco y recorrido de las 245 salas pulsando botones al azar: 0 cuelgues, 0 muertes, sin avisos nuevos. Falta repetirlo en la Vita |
| 2.3e | Texturas vacías en la Vita al entrar en una sala | ✅ | Las páginas que se suben a mitad de un frame (al entrar en una sala, lo visible se carga mientras se dibuja) a veces quedaban vacías en la GPU: retratos de Wyatt y Vladyn en la selección de personaje (página 59). Subirla otra vez entre frames lo arregla siempre (comprobado con capturas). Ahora se vuelve a subir al empezar el frame siguiente. Puede explicar otros "sprites invisibles" vistos antes |
| 2.3c | Avisos pendientes de revisar | ⬜ Lecturas de variables en una instancia que no existe dentro de `nme_v_player_script`, `nme_v_feral_script`, `hermit_Step_0`, `pressure_switch_Step_0` y `save_player_script` (una vez por partida). En GMS1 eso sería un error: comprobar si es una diferencia del runner |
| 2.3d | Boss Rush de prueba (PC, 19-11) | 🖥️ 36 jefes y rivales (azudor … abbigail, ruckjahl, anemone, los 4 personajes rivales) 30 s cada uno en arena_1 con monkey y godmode: 0 cuelgues, 0 frames perdidos. Encontró un fallo real: el código de creación de sala se ejecutaba sin `self` (el modo Arena perdía sus variables); corregido con una instancia "dummy" (id 0), como GMS1. Falta repetirlo en la Vita |
| 2.4 | Todos los jefes y zonas, con una lista de salas probadas en la Vita | 🔶 Las 245 salas recorridas en la Vita (1.8); jefes en combate real: solo Azudor |
| 2.5 | Guardar y cargar, autoguardado entre salas, finales | 🖥️ (carga ✅) |
| 2.6 | Menús: hechizos, habilidades, objetos, mapa, opciones y lore | 🖥️ (pausa ✅) |
| 2.7 | Audio: toda la música y los efectos, bucles, volumen de las opciones | 🖥️ |

## 3. Estabilidad y memoria

| # | Tarea | Estado |
|---|---|---|
| 3.1 | Pico de RAM por debajo de 381 MiB y de VRAM por debajo de 112 MiB, comunicado en cada build | ✅ se registra en cada prueba |
| 3.2 | Sin cuelgues en sesiones largas | ⬜ |
| 3.3 | Suspender y reanudar la Vita en mitad de la partida | ⬜ |

## 4. Controles y experiencia

| # | Tarea | Estado |
|---|---|---|
| 4.1 | L/R → gatillos, panel trasero → hombros: comprobar que sea cómodo en combate | 🖥️ |
| 4.2 | Iconos de botones del juego (aparecen como un mando de PC) | ⬜ |
| 4.3 | Contador de fps verde: ocultarlo o activarlo desde una opción | ⬜ |

## 5. Entrega (uso personal)

| # | Tarea | Estado |
|---|---|---|
| 5.1 | Instrucciones de instalación (README) | ✅ |
| 5.2 | El VPK con AOT lleva código derivado del juego: solo para ti, no se comparte | — |
| 5.3 | Versión sin AOT (intérprete), que podría compartirse sin el juego | ✅ `build/vita_noaot` (1,3 MB). Probada en la Vita: Azudor 57–59 fps, sewer_11 ~34 fps (lógica 2,4 veces más lenta que AOT). Es la del Release |
| 5.4 | Icono y LiveArea propios, sin arte del juego | ⬜ |

## 6. Herramientas de prueba en la Vita

| Archivo en `ux0:data/valdis/save/` | Efecto |
|---|---|
| `warp.txt` | Líneas `SALA FRAMES DESTINO`: salta de sala para medir sin jugar. **Ojo:** el juego autoguarda; haz copia de las partidas antes |
| `profphase` | Perfil ligero: tiempo por fase del frame y por script, con el código nativo activo |
| `perfverbose` | Una línea `[perf]` por segundo, más un desglose de funciones y scripts en cada entrada a sala |
| `prof` | Perfilador por script (líneas `[prof]` cada 600 frames) |
| `proffases` | Solo el tiempo de cada fase del frame, casi sin coste (`profphase` cronometra cada llamada e infla la lógica unas 3–4 veces) |
| `tour.txt` | Recorrido: `FRAMES n` y una sala por línea; escribe una línea `[recorrido]` por sala (fps, frames perdidos, peor frame). Informe: `python build/vitalogs/tour_report.py vsrunner.log` |
| `stress.txt` | Estrés: `SALA CADA_FRAMES CUANTOS MAXIMO OBJ...` mete enemigos junto al jugador y escribe `[estres]` por tanda |
| `godmode` | El jugador no muere en combate (hp a 999999). El autoguardado se lo puede guardar: restaurar partidas |

**Cuidado con las partidas al probar con el agente:** nunca `fs trash` sobre una ruta que se vaya a reutilizar: la papelera actúa tarde y borra el archivo nuevo (así se perdieron las partidas dos veces el 19-11; restauradas desde `build/save_vita_bak_1119_0913`). `fs move` no sobrescribe y `fs download` puede devolver una copia vieja. Usa `build/vitalogs/restore_saves.sh <copia>` (aparta con move, sube con nombre único) y comprueba con `fs list` y con Retry en el menú. Los archivos de prueba quedan en `ux0:data/valdis/pruebas/`.

**Fallo conocido del SDK:** `vita-elf-create.exe` a veces se cae (código 0xC0000374) según cómo queden las direcciones del binario. Si pasa, cambia el tamaño de `vs_pad_probe` al final de `runner/src/gfx.cpp` y recompila.

`tools/deploy.sh` instala por el puente (los VPK subidos van a la papelera del agente) y `tools/va.sh` ejecuta cualquier comando del agente.

---

### Notas de reglas
- **Delta time:** Valdis Story (GMS1) está programado por frames a `room_speed` 60. El runner fija los 60 fps en lugar de escalar por delta, porque escalar cambiaría la física del juego.
- **Overclock:** los relojes por defecto son 500/222 MHz. Si quieres subirlos, hazlo tú mismo con PSVshell, sabiendo que da más calor y desgaste. Con 60 fps estables ahora no hace falta.

---

## 7. Reglas del proyecto (19-11)
- **Criterio de rendimiento para la cuenta final:** objetivo 60 fps. En las salas más exigentes se acepta un **mínimo de 30 fps**. Como el juego avanza por frames, eso solo es aceptable si la lógica sigue a 60 pasos por segundo y lo que baja es el dibujo (si no, a 30 fps el juego va a la mitad de velocidad): requiere la opción de "dibujo a 30 fps cuando la lógica no llega" (1.12). Clasificación por sala: ✅ ≥ 58 fps · 🟡 30–58 fps (aceptable solo en salas exigentes) · ❌ < 30 fps.
- **Prioridad:** estabilidad y terminar el juego 1:1 (igual que el original). Las partidas de la Vita no importan; para probar zonas finales se puede usar una partida avanzada.
- **Las herramientas de prueba se conservan** (`warp.txt`, `tour.txt`, `stress.txt`, `godmode`, perfiles): al final serán una sección de **TRUCOS** dentro del juego.

## 8. Después de terminar el juego (alcanzables)
| # | Idea | Notas |
|---|---|---|
| 8.1 | Menú de TRUCOS | Salto a cualquier sala, modo sin morir, contador de rendimiento, recorrido y estrés |
| 8.2 | Modo "masterquest" | Enemigos nuevos o extra en varias salas. Límite: ~1 ms por enemigo activo en la Vita (sección 1.9); diseñar cada sala dentro de su margen |
| 8.3 | "Randomizer" | Orden de objetos/enemigos aleatorio; depende de entender bien los scripts de progreso (globals de jefes y objetos) |
| 8.4 | Editor de partida / trucos de inventario | Todos los objetos, armas, hechizos, habilidades, oro y nivel. El juego guarda todo en variables GML (~400 con prefijos `item_`, `wep_`, `skill_`, `spell_`, `talent_`, además de `g_item`) y en el `.dat` de la partida: el runner puede escribirlas igual que hace el modo sin morir con `hp`. Antes hay que mapear qué significa cada variable y respetar los límites del juego para no romper la progresión |
| 8.5 | Modo **Boss Rush** | Jefes seguidos en las salas de arena. Ya existe como herramienta de prueba (`save/bossrush.txt`, sección 6 y `docs/TESTING.md`); para jugarlo falta: menú, orden fijo, recuperar vida entre jefes y marcador. El propio juego tiene un modo Arena con jefes al azar (`arena_boss_script`) que sirve de base |
