# Herramientas de prueba (futuro menú de TRUCOS)

Se activan creando archivos en `ux0:data/valdis/save/` (en PC, en la carpeta de partidas que se pase al runner). Si no existen, el juego funciona normal. Al terminar el port pasarán a un menú de trucos dentro del juego (roadmap, sección 8).

| Archivo | Efecto |
|---|---|
| `warp.txt` | Líneas `SALA FRAMES DESTINO`: tras `FRAMES` frames en `SALA`, salta a `DESTINO`. Sirve para llegar a cualquier sala sin jugar |
| `tour.txt` | Recorrido: `FRAMES n` y una sala por línea. Visita cada sala y escribe una línea `[recorrido]` con fps, frames perdidos, peor frame e instancias. Informe: `python tools/tour_report.py vsrunner.log` |
| `stress.txt` | Estrés: `SALA CADA_FRAMES CUANTOS MAXIMO OBJ1 [OBJ2 ...]`. En `SALA` crea enemigos junto al jugador por tandas y escribe una línea `[estres]` por tanda |
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
- **Partidas:** nunca `fs trash` sobre una ruta que se vaya a reutilizar (la papelera del agente actúa tarde, por ruta, y borra el archivo nuevo). `fs move` no sobrescribe y `fs download` puede devolver una copia vieja: comprobar con `fs list`. `tools/restore_saves.sh <copia>` restaura de forma segura.
