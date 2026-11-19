# Notas de decompilación (2026-11-11)

> ⚠️ `decomp/` y `build/` contienen material con copyright de Endless Fluff Games. Son solo
> de referencia local: **no se distribuyen**. El port se genera desde la copia de Steam del usuario.

## Herramientas
- UndertaleModTool CLI 0.9.2.0 en `tools/utmt_cli/`.
- Decompilar: `tools/utmt_cli/UndertaleModCli.exe dump "<steam>/data.win" -o decomp -c UMT_DUMP_ALL -s`
- Construir el APK: `python -I tools/build_vita.py --donor <donante.apk>`

## Resultado
- 6072 entradas de código decompiladas a GML, unas 297.000 líneas, sin fallos de decompilación.
- 3056 eventos de objeto, 658 scripts, 254 creates de room y 2104 creates de instancia.
- `data.win` de la build de Steam 722041: SHA-256 `0a9c9133…647d010f`.
- Personajes jugables: Wyatt (`player_wyatt` es el objeto base del jugador), Reina, Gilda y Vladyn.
  Sus diálogos están en `dia_*_script` y la carga de sus sprites en `load_<nombre>_script`.

## Hallazgos que afectan al port
1. **Rutas de Windows:** 524 `sprite_add` y 237 `background_add` usan `"data_N\archivo.png"`.
   → El parche `patches/vita_paths.csx` normaliza las 716 cadenas a `/`.
2. **`data_5/` es imprescindible:** contiene los ataques de las armas alternativas de Wyatt, Reina
   y Vladyn. Las versiones dentro del `data.win` son de relleno.
3. **`data_3/` y `data_4/` no existen en Steam:** son 462 referencias muertas, sobre todo de jefes
   y enemigos. En PC `sprite_add` falla sin avisar y se queda el sprite incrustado. Si en la Vita
   cuesta tiempo de carga, se pueden quitar esas llamadas con otro parche.
4. **Control:** con `global.gp_on == 1` usa `gamepad_*`, que el runner Android soporta. Las teclas por
   defecto son las flechas, D = atacar/aceptar, A = bloquear/cancelar, S = magia, Q = asistencia y
   Esc = menú. Si el mando falla, se puede usar la emulación de teclado de YoYo Loader con ese mapa.
5. **Vista:** `screen_adjust_script` configura las vistas a 1024×600 → 800×468, según
   `global.opt_ratio`. Es el sitio donde parchear 960×544.
6. **Steam:** las llamadas `steam_*` son logros, cloud save (`cloud_save_script`) y leaderboards
   (`upload_score_script`, `leader_board_script`). Si el runner no las acepta, se cambian por stubs.
7. **Guardado:** `vs_prof.ini`, `vsac_sv_N.ini`, `vs_settings.ini`, `boss_save.ini` y `vvv*.dat`,
   todos en el directorio de trabajo. Funciona tal cual en el sandbox del loader.
