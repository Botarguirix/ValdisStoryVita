# Valdis Story: Abyssal City → PS Vita — Informe de viabilidad y roadmap

Fecha: 2026-11-11 · Fuente del juego: copia de Steam (AppID 252030) instalada en
`C:\Program Files (x86)\Steam\steamapps\common\ValdisStoryAbyssalCity`

## 1. Veredicto

**Viable, con riesgo medio.** Es un juego de GameMaker Studio 1.x compilado en modo
**VM (bytecode)**, no en YYC. Eso lo hace compatible en principio con **YoYo Loader
Vita** (Rinnegatamante) mediante "asset swap" sobre un APK Android donante. No hace
falta reescribir el juego. Los dos riesgos reales son:

1. **Bytecode 13 (GMS 1.x muy temprano)**: hay que encontrar un runner Android
   (`libyoyo.so`) de una versión que acepte bytecode 13. Es el bloqueante número 1 y
   se resuelve (o no) en la primera semana.
2. **Memoria de texturas**: 758 MB en RGBA sin comprimir, frente a 512 MB de RAM + 128 MB
   de VRAM en la Vita. Hay que comprimirlas a PVRTC2. Es un problema conocido y tiene
   solución.

## 2. Análisis técnico del `data.win` de Steam (medido en tu copia)

| Dato | Valor | Implicación |
|---|---|---|
| Motor | GameMaker: Studio 1.x (IDE 1.0.0.1224) | Era 2013, GMS 1.1–1.2 |
| Bytecode | **13** | Muy antiguo: la mayoría de los donantes son BC 14–17 |
| Compilación | VM (chunk CODE con 6072 entradas, 4,9 MB) | ✅ Portable con YoYo Loader (YYC no lo sería) |
| Resolución | 1024×576 | La Vita es 960×544: escalado ×0,94 o un parche de la vista |
| Texturas | 74 páginas (35 de 2048², 18 de 1024×2048…), 197 MB en PNG | **~758 MB en RGBA**: comprimir es obligatorio |
| Audio | 137 WAV dentro de `data.win` (38 MB) + 45 OGG de música externos (~170 MB) | Externalizar los sonidos; la música ya va en streaming |
| Shaders | Ninguno (SHDR vacío) | ✅ Sin problemas de shaders en vitaGL |
| Extensiones/DLL | Ninguna (EXTN = 0) | ✅ Sin código nativo de Windows |
| Surfaces | No usa `surface_create` | ✅ Menos riesgo gráfico |
| Funciones Steam | 30 llamadas (`steam_set_achievement`, cloud, leaderboards, screenshots) | Comprobar que el runner Android las acepta como *stub*; si no, parchearlas con UTMT |
| Mando | `gamepad_*` y `joystick_*` | Mapeable; YoYo Loader ofrece emulación de teclado si falla |
| Assets externos | `data_1/2/5` (91 PNG, 14 MB) cargados con `sprite_add`/`background_add` | Copiarlos a `assets/` y revisar las rutas |
| Guardado | `ini_*`, `file_*`, `highscore_*` | Funciona en el sandbox de `ux0:data/gms/...` |
| Tamaño total | 428 MB | Cabe sin problema en una SD2Vita o memory card |

API total: 187 funciones nativas, todas estándar en GMS 1.x (dibujo 2D, partículas,
tiles, audio, ds_list/ds_map, ini). **No hay nada exótico.**

## 3. "Recomps", loaders y reimplementaciones revisados

| Opción | Qué es | Estado para este juego | Esfuerzo |
|---|---|---|---|
| **A. YoYo Loader Vita** (Rinnegatamante) | Carga el runner Android de GameMaker (`libyoyo.so`) en la Vita con so-loader + vitaGL. Acepta APKs Android y, mediante asset swap, `data.win` de PC en modo VM | **Recomendada.** Soporta PC/consola en bytecode; no soporta YYC. Riesgo: donante para BC13 | Bajo–medio (semanas) |
| **A'. Fork de YoYo Loader** (como el de *Void Stranger Vita*) | Loader modificado con parches específicos para un juego | Plan B si hacen falta hooks propios (Steam, resolución, memoria) | Medio |
| **B. Butterscotch** | Reimplementación open source del runner de GMS que ejecuta `data.win` directamente | Apunta a BC16 / GMS 1.4.1804 y a un subconjunto de funciones. Habría que añadir BC13, las 187 funciones y un backend para Vita | Alto (meses) |
| **C. Reconstruir en GameMaker moderno** | Decompilar con UndertaleModTool, reimportar en un proyecto y exportar a Android | Las licencias de GMS 1.4 ya no se venden. GameMaker actual cambia mucho la API (tiles, audio, `instance_create`). Solo para uso personal | Alto |
| **D. Port nativo (recomp manual)** | Decompilar el GML y reescribirlo en C/C++ con SDL2 + vitaGL | Control total y mejor rendimiento, pero son unos 6000 scripts | Muy alto (6+ meses) |
| OpenGMK / GM8Emulator | Runner para GameMaker 8 *clásico* | ❌ No sirve: este juego es GMS (data.win) | — |
| PhosphorVM | Runner experimental | ❌ Según su propio README no ejecuta ningún juego | — |
| GMLoader / gmloader-next (PortMaster) | El mismo concepto que YoYo Loader, para handhelds ARM con Linux | ✅ Útil como **banco de pruebas**: si funciona ahí con un donante, funcionará en la Vita | Bajo |

No existe un "static recomp" estilo N64Recomp para GameMaker. El equivalente práctico
es el loader más el runner Android oficial.

## 4. Requisito: el juego debe salir de Steam

No se redistribuye ningún asset. El usuario aporta su copia legal y el proyecto entrega:

- **Opción 1 — Script/patcher en PC (recomendada):** `build_vita.py` lee la carpeta de
  Steam, valida el hash de `data.win` y aplica un parche xdelta con los cambios de
  bytecode (resolución, stubs de Steam, rutas). Después renombra a `game.droid`, mete
  la música OGG y `data_*` en `assets/` y empaqueta `game.apk` usando el donante (que
  también tiene que conseguir el usuario).
- **Opción 2 — Guía manual:** los pasos de UTMT y 7-Zip documentados (sección 6).
  Sirve para validar, pero se equivoca más gente.
- **Opción 3 — Patcher en la propia Vita:** copiar los archivos de Steam a
  `ux0:data/gms/valdis/` y que un VPK propio los convierta. Es lo más cómodo para el
  usuario final, pero hay que portar xdelta y comprimir las texturas en la consola, que
  es lentísimo. Mejor dejarlo para el final.

Para comprobar la propiedad basta con el hash del `data.win` de la build actual de
Steam. Las cuentas de Steam no tienen por qué intervenir.

## 5. Hardware y entorno de pruebas

**Consolas (retail con homebrew)**
- Firmware 3.60–3.65 con **Ensō** (ideal) o 3.65–3.74 con HENkaku / h-encore / HENlo.
- Plugins: `kubridge.skprx` y `fd_fix.skprx` en `*KERNEL` de `ux0:tai/config.txt`
  (no instales `fd_fix` si usas rePatch), y `libshacccg.suprx` extraído.
- SD2Vita de 32 GB o más, más **overclock a 444 MHz** (el loader ya puede hacerlo).
- Recomendado: 1 Vita OLED (PCH-1000) y 1 Slim (PCH-2000) para probar las dos
  variantes de RAM/LCD. Una PS TV opcional para probar con mando externo.
- Las DevKit y TestKit no hacen falta: el loader es homebrew y corre en unidades
  retail con HENkaku. Si tienes TestKits, sirven para lo mismo, pero el desarrollo y
  la depuración se hacen con VitaSDK, no con el SDK oficial.

**PC**
- UndertaleModTool (versión con soporte de BC13 y el script `UTMTGetVersionGuesstimate.csx`).
- 7-Zip, PVRTexTool (Imagination, cuenta gratuita) y el script `ExternalizeAllTextures.csx`.
- VitaSDK (solo si se hace fork del loader), VitaShell y FTP, y `vita-parse-core`
  para crash dumps.
- Opcional: un handheld ARM con PortMaster/gmloader como banco rápido.

**Donante:** un APK Android de **cualquier juego GMS con un runner compatible con BC13**
(GMS 1.1–1.2, de 2013–2014). Úsalo solo con fines de prueba y, a ser posible, que sea
uno que tengas comprado. La hoja de donantes de la comunidad (enlazada en el gist de
asset swap) cruza las versiones.

## 6. Roadmap

| Fase | Duración | Objetivo | Criterio de salida |
|---|---|---|---|
| **0. Preparación** | 1–2 días | Vitas con Ensō/HENkaku, plugins, libshacccg, YoYo Loader nightly. Probar un juego GMS que ya funcione | El loader ejecuta un juego conocido de la lista como "Playable" |
| **1. Prueba de concepto (go/no-go)** | 1 semana | Abrir `data.win` en UTMT, confirmar BC13 y la versión exacta. Buscar 2–3 donantes (BC13, y BC14/15 como respaldo). Asset swap y arranque | Llega al menú principal. **Si ningún runner acepta BC13 → plan B (Butterscotch o fork)** |
| **2. Memoria** | 1–2 semanas | "Optimise APK with Externalisation" + texturas comprimidas, luego PVRTC2 4bpp manual. Externalizar los WAV | Recorre 3 zonas y 2 jefes sin out-of-memory |
| **3. Parches de bytecode (UTMT)** | 1–2 semanas | Stubs de `steam_*` si crashean. Vista a 960×544. Mapeo de botones (gamepad → Vita, L/R, menú). Rutas de `data_*` | Controles completos y la imagen a pantalla completa sin recortes |
| **4. Rendimiento** | 2–3 semanas | Perfilar las partículas (`part_*`), el `draw_primitive` y las salas grandes. Bajar la densidad de partículas si hace falta. 444 MHz | 60 fps estables (o 30 bloqueados) en las zonas pesadas |
| **5. Partida completa (QA)** | 2–3 semanas | Campaña completa con Wyatt y Reina, todos los finales, guardado/carga, suspensión de la Vita | Estado "Playable" en OLED y Slim |
| **6. Distribución** | 1–2 semanas | `build_vita.py` + parche xdelta + guía; LiveArea (icono y bg propios, sin assets oficiales); reporte a la lista de compatibilidad | Un tercero monta el port solo con su copia de Steam |
| **7. (Opcional)** | Meses | Patcher en la Vita, fork del loader con hooks propios o soporte BC13 en Butterscotch | — |

**Total estimado de la ruta A:** de 2 a 3 meses a tiempo parcial. Si falla la fase 1
y se va por Butterscotch o un port nativo, la estimación pasa a 6–12 meses.

## 7. Riesgos

| Riesgo | Prob. | Impacto | Mitigación |
|---|---|---|---|
| Ningún runner Android acepta BC13 | Media | Bloqueante | Probar varios donantes de GMS 1.x; fork del loader; Butterscotch |
| Out-of-memory por texturas | Alta | Alto | PVRTC2 4bpp (unos 190 MB); 2bpp como último recurso (se nota en pixel art) |
| `steam_*` sin implementar en el runner Android | Media | Medio | Parchear las llamadas en UTMT (devolver false) |
| Partículas y primitivas lentas | Media | Medio | Parches de bytecode para bajar los recuentos; overclock |
| Música OGG en streaming con cortes en SD2Vita | Baja | Bajo | Externalizar a `assets/`; probar en memory card oficial |
| Derechos de autor | — | — | No distribuir assets ni el `data.win`; solo parches y scripts |

## 8. Próximos pasos inmediatos

1. Montar una Vita con Ensō, plugins y YoYo Loader.
2. Abrir el `data.win` de Steam en UndertaleModTool y anotar la versión exacta con el script de guesstimate.
3. Conseguir un donante GMS 1.x y hacer el asset swap: fase 1, go/no-go.

## Fuentes

- [YoYo Loader Vita (GitHub)](https://github.com/Rinnegatamante/yoyoloader_vita) · [Lista de compatibilidad](https://www.rinnegatamante.eu/yoyo/) · [Issues de compatibilidad](https://github.com/Rinnegatamante/YoYo-Loader-Vita-Compatibility/issues)
- [Guía de asset swap (gist)](https://gist.github.com/CatoTheYounger97/fa47e7eef92f772e4004d4dac22f9bdb) · [Guía PVRTC2 (gist)](https://gist.github.com/CatoTheYounger97/ddc2dd4ec459212466ea6f9887bc764b)
- [GameBrew: YoYo Loader Vita](https://gamebrew.org/wiki/YoYo_Loader_Vita) · [GameBrew: Void Stranger Vita](https://www.gamebrew.org/wiki/Void_Stranger_Vita)
- [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch) · [OpenGMK](https://awesome.ecosyste.ms/projects/github.com%2Fopengmk%2Fopengmk) · [PhosphorVM](https://github.com/AsuMagic/phosphorvm)
- [PortMaster Wiki – GameMaker Studio](https://jantrueno.github.io/PortMaster-Wiki/contribute/porting/engines/gamemaker-studio)
- [Steam: Valdis Story](https://store.steampowered.com/app/252030)
