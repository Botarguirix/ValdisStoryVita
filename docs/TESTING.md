# Menús MODS y TRUCOS, y herramientas de prueba

## Menú de MODS (en el juego)

**SELECT** (pulsar y soltar) abre el menú de MODS; en PC, **F3**. Son ajustes del port, no trampas. Se guardan en `save/mods.ini`. Mientras está abierto, el juego queda en pausa.

| Opción | Efecto |
|---|---|
| Idioma / Language | English (original) o Español (traducción de fans, `lang/es.txt` dentro del VPK). Se aplica al volver al menú principal (Salir) o al abrir el juego |
| Fuente / Font | Original (la fuente de sprites del juego; las tildes, la ñ y ¿ ¡ las dibuja el runner) o Sistema de la Vita (fuente latina del sistema con scePvf: no se distribuye ningún archivo de fuente) |
| Botones / Buttons | PS Vita (✕ ○ □ △ L R L1 R1 START SELECT dibujados por el runner) u Original (nombres de mando de Xbox: A, B, X, Y, LB, RT...) |
| Panel trasero / Rear touch | Solo arriba (por defecto: dos zonas anchas en la parte de arriba con un hueco central, como Shotgun King Vita), Entero (mitades izquierda/derecha) o Apagado |

## Menú de TRUCOS (en el juego)

**SELECT + L + R** abre el menú (mantener SELECT y pulsar L y R; en PC, **F2**). SELECT no tiene uso en los controles por defecto del juego. Mientras está abierto, el juego queda en pausa.

| Opción | Efecto |
|---|---|
| Modo dios | No mueres en combate (`hp` a 999999 en cada frame; un foso sí puede matarte). Se guarda como el archivo `save/godmode` |
| Maná infinito | El maná se rellena en cada frame (`mp = global.set_mp`). Archivo `save/infmana` |
| Subir un nivel | Pone la experiencia al máximo y el propio juego sube el nivel (con su aviso y +1 punto de atributo y de habilidad). Máximo 20 |
| +5 puntos | +5 puntos de atributo y +5 de habilidad, para gastar en el menú de pausa |
| Personajes y Boss Rush desbloqueados | Los cuatro personajes en la selección y la entrada Boss Rush del menú principal. Archivo `save/unlockchars` |
| Ir a sala | Izquierda/derecha elige la sala (L/R saltan de 10 en 10) y X viaja. Solo dentro de la partida. Si el personaje queda fuera de la sala se le coloca dentro, pero puede aparecer dentro de una pared: elige otra sala |
| Mostrar rendimiento | fps, lógica y dibujo en ms, abajo a la derecha. Archivo `save/showperf` |

Las opciones de encendido y apagado se recuerdan entre sesiones (son los mismos archivos de las herramientas de prueba). **Ojo:** el autoguardado guarda lo que cambies (nivel, puntos, la vida del modo dios); las partidas de la Vita no importan para probar.

## Boss Rush (menú principal)

Aparece como **-Boss Rush-** debajo de **-Salir-** en el menú principal cuando el juego está terminado con cualquier personaje (la misma señal que desbloquea los modos Goibniu y Arena del juego: `global.unlock_char`). El truco "Personajes y Boss Rush desbloqueados" también lo muestra. Código: `runner/src/bossrush.cpp`.

- **Personaje:** Wyatt y Reina siempre; Vladyn y Gilda si están desbloqueados.
- **Modo fiel a cada encuentro:** justo antes de cada jefe de la historia el juego guarda una copia completa del estado (lo que usa "Reintentar"). El runner la copia en `save/bossrush/pC_bN.ini` (C = personaje, N = número del jefe) cada vez que llegas a un jefe jugando normal, y el Boss Rush la carga con los mismos scripts que "Reintentar": nivel, atributos, armas, hechizos y objetos son los que tenías en ese encuentro.
- **Jefes sin grabar:** se parte de tu partida guardada más avanzada de ese personaje con **todo el equipo** (armas, armaduras, accesorios, almas y remates). Funciona en 18 de 20; Ruckjahl y Dhistahn poseído solo con su grabación (su escena de historia necesita más estado), y sin ella se saltan. Entre jefe y jefe el juego vuelve al título por dentro y carga desde allí, como "Reintentar".
- **Modo aleatorio:** como el fiel, pero armaduras, accesorios, almas y remates al azar, y armas extra al azar. Lo equipado se queda para no romper la partida.
- **Tus partidas no se tocan:** durante el Boss Rush los guardados van a una copia en memoria (`bgio::setSandbox`) y al salir se descartan.
- Al vencer a un jefe (pantalla de puntuación del juego) se pasa solo al siguiente. Si caes: reintentar ese jefe o terminar. Al final, tabla con el tiempo de cada jefe.
- En PC: flechas, Enter y Escape en el menú del Boss Rush.

Pruebas en PC: `VS_BRKILL=N` deja la vida del jefe en 0 a los N frames, `VS_BRSKIP=N` pasa al siguiente si no termina en N frames, `VS_BRONLY=1,10,...` elige jefes por número y `VS_BRENT=L|R|O` fuerza la entrada en los jefes sin grabar.

## Herramientas de prueba (archivos)

Se activan creando archivos en `ux0:data/valdis/save/` (en PC, en la carpeta de partidas que se pase al runner). Si no existen, el juego funciona normal. También valen con un sufijo antes de la extensión (`godmode_0815`, `keys_0815.txt`).

**Con vita-agent:** el agente no deja volver a crear un archivo en una ruta de la que antes movió otro: la subida dice "completa", pero el archivo no aparece. Sube cada archivo con un nombre nuevo a `pruebas/` y luego muévelo con `fs move` a su sitio, que sí funciona. Con la pantalla apagada (`screen off`), el juego se congela: vitaGL espera a la pantalla. Haz las pruebas con la pantalla encendida.

| Archivo | Efecto |
|---|---|
| `warp.txt` | Líneas `SALA FRAMES DESTINO`: tras `FRAMES` frames en `SALA`, salta a `DESTINO`. Sirve para llegar a cualquier sala sin jugar |
| `tour.txt` | Recorrido: `FRAMES n` y una sala por línea. Visita cada sala y escribe una línea `[recorrido]` con fps, frames perdidos, peor frame e instancias. Informe: `python tools/tour_report.py vsrunner.log` |
| `stress.txt` | Estrés: `SALA CADA_FRAMES CUANTOS MAXIMO OBJ1 [OBJ2 ...]`. En `SALA` crea enemigos junto al jugador por tandas y escribe una línea `[estres]` por tanda |
| `unlockchars` | Los cuatro personajes en la selección (mantiene `global.unlock_char = 1`; el juego lo activa al desbloquear el lore 63) |
| `monkey` | Botones al azar en las salas de juego: moverse, saltar, atacar, magia, bloquear (nunca magia + bloqueo juntos, que abre el menú de hechizos). Con `godmode` y `tour.txt` prueba el código de cada personaje en todas las salas |
| `bossrush.txt` | Boss Rush de prueba. Dos formatos: `SALA FRAMES JEFE1 JEFE2 ...` (todos en una sala, p. ej. `arena_1 1800 azudor shu`) o `salas FRAMES` seguido de líneas `JEFE SALA` (cada jefe en su propia sala; si ya está colocado en ella no se crea otro). Va a la sala y saca cada jefe junto al jugador; línea `[jefe]` con coste, frames perdidos, si sigue vivo y avisos nuevos. Con `monkey` y `godmode` hay combate real |
| `keys.txt` | Pulsaciones programadas, igual que `--key`/`--onroom` del PC: líneas `at FRAME TECLA` y `onroom SALA RETRASO TECLA` (códigos de tecla GMS: 68 = D, 39 = derecha, 13 = Enter). Sirve para empezar partida sola con un personaje concreto |
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
