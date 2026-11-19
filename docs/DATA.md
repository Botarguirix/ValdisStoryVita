# Datos y conversión

El repositorio no contiene datos del juego. Todo se genera desde la copia de Steam del usuario (build 722041, App ID 252030).

## En la consola (lo normal)
Al primer arranque, `src/convert_vsd.cpp` y `src/convert.cpp` leen `ux0:data/valdis/game/data.win` y generan `ux0:data/valdis/vsdata/`:

| Salida | Contenido |
|---|---|
| `game.vsd` | Formato propio: objetos, eventos, salas, sprites, fuentes, bytecode (GMS1, bytecode 13) |
| `tex/N.vdx` | Páginas de textura en DXT1/DXT5 comprimidas con LZ4 (VDX4) |
| `snd/N.wav` | Sonidos incrustados |
| `data_*/x.png.vdx` | Junto a cada PNG externo (`sprite_add`/`background_add`) |

Pico de memoria de la conversión en la Vita: ~49 MB. Si se interrumpe, continúa en el siguiente arranque. Para regenerar todo, borrar `ux0:data/valdis/vsdata/`.

## En PC
- `build/pc/vsconv all <data.win> <carpeta_steam> <salida>`: el mismo convertidor que la Vita.
- `build/pc/vsdinfo <game.vsd> [objects|rooms|vars|funcs|events|strings|collisions|code NOMBRE|spritepages TEXTO]`: volcados para investigar.
- `build/pc/vsaot <game.vsd> <salida>`: traduce todo el bytecode a C++ (`aot_*.cpp`) para la versión rápida. **Ese código es derivado del juego: no se sube ni se comparte.**

## Notas del juego
- Las carpetas `data_3/` y `data_4/` que menciona el código no existen en Steam (referencias muertas).
- `data_5/` es imprescindible: ataques de armas alternativas y Limit Breaks. `anim_kudir.png` (6220 px) y `fx_life_blade.png` (4235 px) superan el máximo de textura de la Vita y el runner las parte en trozos.
- Más detalles en [NOTAS_DECOMP.md](NOTAS_DECOMP.md).
