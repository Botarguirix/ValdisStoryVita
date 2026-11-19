# Herramientas de prueba (futuro menú de TRUCOS)

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

## En PC

`build/pc/vsrunner <vsdata> <carpeta_juego> <carpeta_partidas> [opciones]`:
`--turbo` (sin esperar al vsync), `--frames N`, `--warp SALA FRAMES DESTINO`, `--key FRAME TECLA`, `--hold FRAME TECLA DURACION`,
`--det SEMILLA` + `--hash N` (huellas deterministas del estado: comparar dos builds), `--bench FRAME`, `--sample archivo`, `--profcode`, `--noaot`.

**Regla:** cualquier optimización debe dar exactamente las mismas huellas (`--det --hash`) que la versión anterior.

## Con vita-agent (pruebas automáticas en la consola)

- `tools/va.sh` ejecuta comandos del agente (configura `tools/local.env`). Nunca dos clientes a la vez.
- `tools/deploy.sh` sube e instala el VPK. `tools/vita_input/*.json` son pulsaciones (`tools/va.sh input submit ...`; en el menú del título usar `hold_CROSS.json`).
- **No leer `vsrunner.log` mientras el juego corre:** descargarlo cada minuto bloqueó la escritura del registro en la Vita (el juego siguió funcionando, pero el registro se quedó congelado). Descargarlo solo al cerrar el juego.
- **Partidas:** nunca `fs trash` sobre una ruta que se vaya a reutilizar (la papelera del agente actúa tarde, por ruta, y borra el archivo nuevo). `fs move` no sobrescribe y `fs download` puede devolver una copia vieja: comprobar con `fs list`. `tools/restore_saves.sh <copia>` restaura de forma segura.
