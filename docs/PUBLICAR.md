# Publicar en GitHub (GitHub Desktop) y crear el Release

El repositorio local ya está listo: la carpeta del repo (`ValdisStoryVita`), rama `main`, con el commit inicial *"Publicar el runner nativo de Valdis Story para PS Vita (v0.5.0-alpha)"*. El `.gitignore` deja fuera todo lo del juego (`build/`, `decomp/`, `ref/`, `tools/utmt_cli`, VPK, partidas y `tools/local.env`). Ver [ESTRUCTURA_REPOS.md](ESTRUCTURA_REPOS.md).

## 1. Subir el repositorio

1. Abre **GitHub Desktop** e inicia sesión con la cuenta **Botarguirix** (con tu verificación en dos pasos).
2. **File → Add local repository…** → elige la carpeta del repo (`ValdisStoryVita`) → **Add repository**.
3. Pulsa **Publish repository**:
   - **Name:** `ValdisStoryVita`
   - **Description:** `Port nativo de Valdis Story: Abyssal City para PS Vita (runner propio de GameMaker 1.x en C++)`
   - Desmarca **Keep this code private** si lo quieres público, como Touhou123Vita y DDVita.
4. Comprueba en la web que **no** aparecen las carpetas `build/`, `decomp/` ni `ref/`.

> Commits nuevos: en GitHub Desktop, revisa la lista de cambios antes de **Commit to main** + **Push origin**. Si aparece cualquier archivo del juego, un `.vpk`, un `.dat` o `tools/local.env`, no lo subas.

## Release v0.9.3-alpha (25-11)

Etiqueta `v0.9.3-alpha`, pre-release, título `Valdis Story PSVITA v0.9.3-alpha`. VPK público (intérprete): `build\vita_noaot\vsvita.vpk`, 2 173 535 bytes, SHA-256 `d99279a499067c4001a331649ef67102d8a0f01bb243cf6c6279b46d4e9a4775`. **Este VPK no se ha probado en la Vita** (en la consola se probó la versión AOT del mismo código): se adjunta más tarde. Nunca subir el AOT.

### Notas del release v0.9.3 (copiar y pegar)

```markdown
## Valdis Story PSVITA v0.9.3-alpha

Port nativo de **Valdis Story: Abyssal City** para PS Vita: un runner propio de GameMaker Studio 1.x en C++17. El juego se convierte desde **tu copia de Steam** en la propia consola.

**ULTRA IA VIBECODED!** Todo el código, las herramientas y la documentación los escribió una IA con Claude Code (Claude Opus 5.5 y Claude Sonnet 5.5); el usuario dirige y prueba en una PS Vita real.

> ⚠️ **No incluye el juego.** Necesitas una copia legal de Steam (build 722041, App ID 252030; ver README). Valdis Story es propiedad de Endless Fluff Games; proyecto de fans sin ánimo de lucro.

### Novedades
- **Modo Masterquest** (encima del menú principal; se desbloquea al terminar el juego): multiplica los enemigos de cada sala (x1, x2 o x3) y añade de 0 a 6 enemigos extra al azar, de la zona o de cualquier tipo. Las salas con jefe no cambian.
- **Modo Lvl1:** ahora empieza además con las 16 armaduras y 3 Master Scroll.

### Estado de las pruebas
- Masterquest en la Vita (versión AOT): menú, submenú y partida nueva funcionan. Quedan cosas por resolver. El rendimiento con x3 no está medido; con muchos enemigos se esperan bajadas de fps, sobre todo en esta versión pública.
- Las novedades de Lvl1 solo se han comprobado en PC.
- El VPK público de esta versión todavía no se ha probado en la consola.

### Problemas conocidos
- El submenú de Masterquest está solo en español.
- Algunas descripciones del bestiario (Lore) siguen en inglés.
- En las ranuras de partida, "Lv/Time/Crew/Location" siguen en inglés: forman parte del dibujo del juego.
- Se han visto cierres por error de la GPU con el reloj de la consola subido.

### Instalación
1. Instala `vsvita.vpk` con VitaShell. Necesitas `ur0:data/libshacccg.suprx` (extráelo con ShaRKBR33D).
2. Copia `data.win`, `data_1/`, `data_2/`, `data_5/` y los `music_*.ogg` de tu carpeta de Steam a `ux0:data/valdis/game/`.
3. Abre el juego: el primer arranque convierte los datos. Pulsa SELECT para elegir español y el resto de opciones.
```

## 2. Crear el Release v0.9.2-alpha (25-11)

1. En la web del repo: **Releases → Draft a new release** (o abrir el borrador si ya está creado).
2. **Choose a tag:** `v0.9.2-alpha` (ya subida).
3. **Release title:** `Valdis Story PSVITA v0.9.2-alpha`
4. Marca **Set as a pre-release**.
5. Arrastra el VPK **sin código del juego**: `build\vita_noaot\vsvita.vpk` (dentro de la carpeta del repo), 2 156 074 bytes, SHA-256 `1ee2013bb2c075bb56a1f8126a5f28622c337e832b3cbc6e1b10e0ccff121407`.
   - Probado en la Vita el 25-11: se instala (la consola acepta las imágenes nuevas) y arranca a 60 fps, AOT en la consola activo, sin fallos.
   - **No subas** `build\vita\vsvita.vpk`: es la versión AOT, lleva código derivado del juego y es solo para uso personal.
6. Pega las notas de abajo en la descripción y pulsa **Publish release**.

### Notas del release v0.9.2 (copiar y pegar)

```markdown
## Valdis Story PSVITA v0.9.2-alpha

Port nativo de **Valdis Story: Abyssal City** para PS Vita: un runner propio de GameMaker Studio 1.x en C++17 (no es emulador ni YoYo Loader). El juego se convierte desde **tu copia de Steam** en la propia consola.

**ULTRA IA VIBECODED!** Todo el código, las herramientas y la documentación los escribió una IA (Claude Opus 5.5 como modelo principal y Claude Sonnet 5.5 en subagentes, con Claude Code); el usuario dirigió y probó cada build en una PS Vita real.

> ⚠️ **No incluye el juego.** Necesitas una copia legal de Steam (build 722041, App ID 252030; ver README). Este VPK solo trae el motor y la traducción al español (sin el texto original). Valdis Story es propiedad de Endless Fluff Games; proyecto de fans sin ánimo de lucro.

### Novedades
- **Icono y LiveArea nuevos:** ilustraciones originales para la burbuja, la imagen de arranque, el fondo de la LiveArea y el botón Iniciar.
- La versión se ve en la burbuja y en la LiveArea (**v0.9.2 PÚB** en esta versión, **v0.9.2 AOT** en la que compiles tú), colocada donde no tapa la ilustración.

Todo lo de la v0.9.1 sigue igual: Modo Lvl1 en el menú principal, traducción del juego en la propia Vita más rápida, GFX Low corregido y logros en la pausa (Mapa/Opc. → Logros).

### Problemas conocidos
- Algunas descripciones del bestiario (Lore) siguen en inglés.
- En las ranuras de partida, "Lv/Time/Crew/Location" siguen en inglés: forman parte del dibujo del juego.
- Se han visto cierres por error de la GPU con el reloj de la consola subido (PSVshell al máximo); si te pasa, prueba con los relojes de serie.

### Instalación
1. Instala `vsvita.vpk` con VitaShell. Necesitas `ur0:data/libshacccg.suprx` (extráelo con ShaRKBR33D).
2. Copia `data.win`, `data_1/`, `data_2/`, `data_5/` y los `music_*.ogg` de tu carpeta de Steam a `ux0:data/valdis/game/`.
3. Abre el juego: el primer arranque convierte los datos (unos minutos). Pulsa SELECT para elegir español y el resto de opciones.
```

## Release anterior: v0.9.1-alpha (24-11, publicado)

### Pasos que se siguieron (v0.9.1)

1. En la web del repo: **Releases → Draft a new release** (o abrir el borrador si ya está creado).
2. **Choose a tag:** `v0.9.1-alpha` (ya subida).
3. **Release title:** `Valdis Story PSVITA v0.9.1-alpha`
4. Marca **Set as a pre-release**.
5. Arrastra el VPK **sin código del juego**: `build\vita_noaot\vsvita.vpk` (dentro de la carpeta del repo), 1 763 817 bytes, SHA-256 `f89592fdd94a74ca9133cc5d0da45c472352f5ba63e938e7039932a6c82f9945`.
   - Probado en la Vita el 24-11: arranca hasta el menú principal, AOT en la consola activo, sin fallos.
   - **No subas** `build\vita\vsvita.vpk`: es la versión AOT, lleva código derivado del juego y es solo para uso personal.
6. Pega las notas de abajo en la descripción y pulsa **Publish release**.

### Notas del release v0.9.1 (copiar y pegar)

```markdown
## Valdis Story PSVITA v0.9.1-alpha

Port nativo de **Valdis Story: Abyssal City** para PS Vita: un runner propio de GameMaker Studio 1.x en C++17 (no es emulador ni YoYo Loader). El juego se convierte desde **tu copia de Steam** en la propia consola.

**ULTRA IA VIBECODED!** Todo el código, las herramientas y la documentación los escribió una IA (Claude Opus 5.5 como modelo principal y Claude Sonnet 5.5 en subagentes, con Claude Code); el usuario dirigió y probó cada build en una PS Vita real.

> ⚠️ **No incluye el juego.** Necesitas una copia legal de Steam (build 722041, App ID 252030; ver README). Este VPK solo trae el motor y la traducción al español (sin el texto original). Valdis Story es propiedad de Endless Fluff Games; proyecto de fans sin ánimo de lucro.

### Novedades
- **Modo Lvl1:** nueva entrada **-Modo Lvl1-** en el menú principal, debajo de Boss Rush (aparece al terminar el juego). Partida nueva en la que nunca se sube de nivel, pero con las 5 armas y su hechizo base desde el principio y el doble salto. Las mejoras de hechizos se siguen comprando.
- **Más rápido sin AOT de PC:** la traducción del juego que hace la propia Vita ahora fusiona las operaciones más frecuentes. En El Manawar la lógica baja de 8,1 a **6,5 ms** por frame (el intérprete de v0.8 tardaba 14,6 ms).

### Correcciones
- **GFX : Low:** el juego iba demasiado rápido (unos 79 pasos por segundo en vez de 60) y a tirones. Ahora va a su velocidad.
- **Icono de los objetos que sueltan los enemigos:** a veces salía el del objeto siguiente; ahora coincide con el del aviso.
- **L y R** también se leen por la lectura extendida del mando.

### Problemas conocidos
- En las ranuras de partida, "Lv/Time/Crew/Location" siguen en inglés: forman parte del dibujo del juego.
- Se han visto cierres por error de la GPU con el reloj de la consola subido (PSVshell al máximo); si te pasa, prueba con los relojes de serie.
- Boss Rush: Ruckjahl y Dhistahn poseído solo aparecen si ya los grabaste jugando la historia.

### Instalación
1. Instala `vsvita.vpk` con VitaShell. Necesitas `ur0:data/libshacccg.suprx` (extráelo con ShaRKBR33D).
2. Copia `data.win`, `data_1/`, `data_2/`, `data_5/` y los `music_*.ogg` de tu carpeta de Steam a `ux0:data/valdis/game/`.
3. Abre el juego: el primer arranque convierte los datos (unos minutos). Pulsa SELECT para elegir español y el resto de opciones. Los logros están en la pausa: Mapa/Opc. → Logros.
```

## Release anterior: v0.9.0-alpha (24-11)

### Pasos que se siguieron (v0.9.0)

1. En la web del repo: **Releases → Draft a new release** (o abrir el borrador si ya está creado).
2. **Choose a tag:** `v0.9.0-alpha` (ya subida).
3. **Release title:** `Valdis Story PSVITA v0.9.0-alpha`
4. Marca **Set as a pre-release**.
5. Arrastra el VPK **sin código del juego**: `build\vita_noaot\vsvita.vpk` (dentro de la carpeta del repo), 1 750 225 bytes, SHA-256 `7970e930eeb3104bc166e97a05830a5562036b9e9bddba7c9dc399d234a81287`.
   - Probada en la Vita el 24-11: El Manawar con los tres modos (intérprete, fase 1 y ARM), sin cuelgues; burbuja y LiveArea con el sello "v0.9.0 PÚBLICA".
   - El tag `v0.9.0-alpha` quedó en el commit anterior a la LiveArea nueva (moverlo necesita un push forzado): el VPK adjunto es el bueno.
   - **No subas** `build\vita\vsvita.vpk`: es la versión AOT, lleva código derivado del juego y es solo para uso personal.
6. Pega las notas de abajo en la descripción y pulsa **Publish release**.

### Notas del release v0.9.0 (copiar y pegar)

```markdown
## Valdis Story PSVITA v0.9.0-alpha

Port nativo de **Valdis Story: Abyssal City** para PS Vita: un runner propio de GameMaker Studio 1.x en C++17 (no es emulador ni YoYo Loader). El juego se convierte desde **tu copia de Steam** en la propia consola.

**ULTRA IA VIBECODED!** Todo el código, las herramientas y la documentación los escribió una IA (Claude Opus 5.5 como modelo principal y Claude Sonnet 5.5 en subagentes, con Claude Code); el usuario dirigió y probó cada build en una PS Vita real.

> ⚠️ **No incluye el juego.** Necesitas una copia legal de Steam (build 722041, App ID 252030; ver README). Este VPK solo trae el motor y la traducción al español (sin el texto original). Valdis Story es propiedad de Endless Fluff Games; proyecto de fans sin ánimo de lucro.

### Novedad principal: AOT en la consola
Hasta ahora, la versión rápida (AOT) había que compilarla en un PC desde tu copia del juego. **Ahora la propia Vita traduce el código del juego** la primera vez que ejecuta cada parte, sin PC ni compilador y sin nada del juego dentro del VPK. Es automático y viene activado.
- **En la Vita** (El Manawar, la pelea de los rayos): de 49 fps de media con 14,1 ms de lógica por frame a **56,5 fps con 8,1 ms** (−43 %). El AOT compilado en PC da 59–63 fps en esa pelea.
- El resultado es idéntico al del juego interpretado (comprobado cuadro a cuadro en un recorrido de 170 salas).
- Si quieres comparar, crea el archivo vacío `ux0:data/valdis/save/novaot` para volver al intérprete.
- Experimental: con `ux0:data/valdis/save/vaot_arm` la Vita además genera código ARM nativo. Funciona, pero en las pruebas no mejora a la traducción normal, así que viene apagado.

### Icono y LiveArea nuevos
- Arte original nuevo (ciudad abisal y emblema), con la versión y el tipo de build en la burbuja y la LiveArea: **v0.9.0 PÚBLICA** en esta, **v0.9.0 AOT** en la que compiles tú.

### Logros
- **Pantalla de logros dentro del juego:** pausa → **Mapa/Opc. → Logros** (sustituye a "Full Screen", que en la Vita no hacía nada). Cuadrícula con todos los logros, nombre, descripción y estado.
- **80 logros** (faltaban los 12 de rango [S] al terminar), con nombre y descripción en español.
- **Imágenes de cada logro (opcional):** en un PC, `python tools/logros_iconos.py` baja los iconos de la página pública de logros de Steam; copia la carpeta `build/vsdata/logros/` a `ux0:data/valdis/vsdata/logros/`. Sin ellas se usan los sprites del juego.
- TRUCOS (SELECT + L + R) → **Reiniciar logros**, con confirmación.

### Correcciones y mejoras
- **HUD reducido:** cada elemento se encoge hacia su sitio según su centro (cartel y texto juntos); el panel de puntuación del jefe siempre al 70 % o menos, para no tapar las recompensas.
- **Fuentes del sistema** que no son negrita, uno o dos tamaños más grandes.
- **El Manawar** medido en la Vita: 59–63 fps toda la pelea.
- Si el juego se cerró por un fallo, al volver a abrirlo lo anota en `vsrunner.log`; los errores de audio también quedan ahí.

### Problemas conocidos
- En las ranuras de partida, "Lv/Time/Crew/Location" siguen en inglés: forman parte del dibujo del juego.
- A veces el icono del objeto que sale de un cofre no coincide con lo que es (en investigación).
- "GFX : Low" hace el juego más lento en vez de más fluido (pendiente).
- Boss Rush: Ruckjahl y Dhistahn poseído solo aparecen si ya los grabaste jugando la historia.

### Instalación
1. Instala `vsvita.vpk` con VitaShell. Necesitas `ur0:data/libshacccg.suprx` (extráelo con ShaRKBR33D).
2. Copia `data.win`, `data_1/`, `data_2/`, `data_5/` y los `music_*.ogg` de tu carpeta de Steam a `ux0:data/valdis/game/`.
3. Abre el juego: el primer arranque convierte los datos (unos minutos). Pulsa SELECT para elegir español y el resto de opciones.
```

## Release anterior: v0.8.0-alpha (24-11, publicado)

### Pasos que se siguieron (v0.8.0)

1. En la web del repo: **Releases → Draft a new release** (o abrir el borrador si ya está creado).
2. **Choose a tag:** `v0.8.0-alpha` (ya subida).
3. **Release title:** `Valdis Story PSVITA v0.8.0-alpha`
4. Marca **Set as a pre-release**.
5. Arrastra el VPK **sin código del juego** (probado en la Vita el 24-11: arranca en español, menú MODS con las opciones nuevas, 60 fps en el título): `build\vita_noaot\vsvita.vpk` (dentro de la carpeta del repo), 1 653 039 bytes, SHA-256 `051d276bb5604bc6868a5447555ffd5807a419b8ff07add60fe0c3dfdb9e2e25`.
   - **No subas** `build\vita\vsvita.vpk`: es la versión AOT, lleva código derivado del juego y es solo para uso personal.
6. Pega las notas de abajo en la descripción y pulsa **Publish release**.

### Notas del release v0.8.0 (copiar y pegar)

```markdown
## Valdis Story PSVITA v0.8.0-alpha

Port nativo de **Valdis Story: Abyssal City** para PS Vita: un runner propio de GameMaker Studio 1.x en C++17 (no es emulador ni YoYo Loader). El juego se convierte desde **tu copia de Steam** en la propia consola.

**ULTRA IA VIBECODED!** Todo el código, las herramientas y la documentación los escribió una IA (Claude Opus 5.5 como modelo principal y Claude Sonnet 5.5 en subagentes, con Claude Code); el usuario dirigió y probó cada build en una PS Vita real.

> ⚠️ **No incluye el juego.** Necesitas una copia legal de Steam (build 722041, App ID 252030; ver README). Este VPK solo trae el motor y la traducción al español (sin el texto original). Valdis Story es propiedad de Endless Fluff Games; proyecto de fans sin ánimo de lucro.

Esta versión sale del reporte de una partida larga jugada a mano en la Vita.

### Correcciones
- **Cuelgue al abrir un cofre tras una sesión larga:** la memoria de los efectos de sonido no se liberaba nunca y acababa agotándose. Ahora tiene un tope y, si alguna vez falta memoria, se salta ese sonido en vez de cerrar el juego.
- **El Manawar y Abbigail ya no bajan a 10–30 fps:** las colisiones de los rayos giratorios se calculaban píxel a píxel sobre un área enorme. Ahora solo se revisa donde puede estar el rayo, con el mismo resultado que antes.
- **Al bajar del tren** el personaje ya no se queda detrás de los edificios.
- **Recompensas de los jefes:** ya no quedan tapadas por el panel de puntuación.

### Novedades (menú MODS, SELECT)
- **HUD más pequeño:** 100 / 85 / 70 / 60 / 50 %. La vida, el maná, los combos y los avisos se encogen hacia su esquina; los diálogos y los menús quedan igual.
- **Tres fuentes nuevas:** además de la original y la del sistema en negrita, ahora están la del sistema normal, la estrecha y la de remates.
- **Tamaño del texto:** 100 / 90 / 80 / 70 %. Las letras se achican sin cambiar el ancho de las cajas, así las descripciones caben.
- **Logros locales:** los 68 logros del juego se guardan en la consola, con aviso e icono al conseguirlos y la lista en MODS → Logros.

### Traducción
- Nombres de zona en español (Pasarela occidental, Restos de Tagrum, Prisión hundida…).
- Diálogos que faltaban (Gilda, Reina, Vladyn), avisos de tutorial y nombres del bestiario (Templario ángel, Zelote ángel…).

### Rendimiento de este VPK
Es la versión de **intérprete**: no lleva código del juego, por eso se puede compartir. En las salas más cargadas baja a unos 34 fps. La versión rápida (AOT, 60 fps) se genera desde tu propia copia con `tools/build_aot_vpk.sh` (guía en `docs/AOT_USUARIOS.md`).

### Problemas conocidos
- En las ranuras de partida, "Lv/Time/Crew/Location" siguen en inglés: forman parte del dibujo del juego.
- A veces el icono del objeto que sale de un cofre no coincide con lo que es (en investigación).
- "GFX : Low" hace el juego más lento en vez de más fluido (pendiente).
- Boss Rush: Ruckjahl y Dhistahn poseído solo aparecen si ya los grabaste jugando la historia.

### Instalación
1. Instala `vsvita.vpk` con VitaShell. Necesitas `ur0:data/libshacccg.suprx` (extráelo con ShaRKBR33D).
2. Copia `data.win`, `data_1/`, `data_2/`, `data_5/` y los `music_*.ogg` de tu carpeta de Steam a `ux0:data/valdis/game/`.
3. Abre el juego: el primer arranque convierte los datos (unos minutos). Pulsa SELECT para elegir español y el resto de opciones.
```

## Release anterior: v0.7.0-alpha (20-11, publicado)

> La v0.6.0 no llegó a publicarse: la 0.7.0 incluye todo lo de la 0.6.0 (MODS, español, iconos, panel trasero, TRUCOS) más el Boss Rush. Si ya publicaste la 0.6.0, publica esta igual como nueva versión.

1. En la web del repo: **Releases → Draft a new release**.
2. **Choose a tag:** escribe `v0.7.0-alpha` → *Create new tag on publish* (sobre `main`).
3. **Release title:** `Valdis Story PSVITA v0.7.0-alpha`
4. Marca **Set as a pre-release**.
5. Arrastra el VPK **sin código del juego** `build\vita_noaot\vsvita.vpk` (dentro de la carpeta del repo), 1 622 138 bytes, SHA-256 `be4a7dd855838580bb1ccae35c2a08561704304227b493a941ef3d847e5ff1b4`. El mismo código se probó en la Vita el 20-11 (versión AOT: menú principal con Boss Rush, submenú y primera pelea; versión sin AOT: título e intro en español a 60 fps).
   - **No subas** `build\vita\vsvita.vpk`: es la versión AOT, lleva código derivado del juego y es solo para uso personal.
6. Pega las notas de abajo en la descripción y pulsa **Publish release**.

### Notas del release v0.7.0 (copiar y pegar)

```markdown
## Valdis Story PSVITA v0.7.0-alpha

Port nativo de **Valdis Story: Abyssal City** para PS Vita: un runner propio de GameMaker Studio 1.x en C++17 (no es emulador ni YoYo Loader). El juego se convierte desde **tu copia de Steam** en la propia consola.

**ULTRA IA VIBECODED!** Todo el código, las herramientas y la documentación los escribió una IA (Claude Opus 5.5 como modelo principal y Claude Sonnet 5.5 en subagentes, con Claude Code); el usuario dirigió y probó cada build en una PS Vita real.

> ⚠️ **No incluye el juego.** Necesitas una copia legal de Steam (build 722041, App ID 252030; ver README). Este VPK solo trae el motor y la traducción al español (sin el texto original). Valdis Story es propiedad de Endless Fluff Games; proyecto de fans sin ánimo de lucro.

### Novedades
- **Modo Boss Rush** en el menú principal (aparece al terminar el juego con cualquier personaje, como los modos extra del original): los 20 jefes de la historia seguidos, cada uno en su propia sala con su presentación.
  - **Fiel a cada encuentro:** el port graba tu estado justo antes de cada jefe mientras juegas la historia (nivel, armas, hechizos, objetos) y el Boss Rush lo usa. Los jefes que aún no grabaste usan tu partida más avanzada con todo el equipo.
  - **Aleatorio:** equipo al azar en cada jefe.
  - Tus partidas no se tocan durante el modo. Al final, tabla con el tiempo de cada jefe.
- **Versión rápida (AOT) con un solo comando:** `tools/build_aot_vpk.sh` genera tu VPK de 60 fps desde tu copia de Steam (guía en `docs/AOT_USUARIOS.md`).
- **Menú de MODS (SELECT):**
  - **Idioma:** inglés original o **español**. La traducción de fans, en **español latino**, cubre el 99,5 % del texto: diálogos de los 4 personajes, historia, libro del saber, menús, objetos y habilidades.
  - **Fuente:** la del juego, que ahora también dibuja tildes, ñ y ¿ ¡, o la **fuente del sistema de la Vita**, más legible.
  - **Botones:** iconos de PS Vita (✕ ○ □ △ L R) en tutoriales y avisos, en lugar de los nombres de mando de Xbox (A, B, LB…).
  - **Panel trasero:** por defecto solo responde la parte de arriba, para no pulsar L1/R1 sin querer al sujetar la consola; también "entero" o "apagado".
- **Menú de TRUCOS (SELECT + L + R):** modo dios, maná infinito, subir nivel, +5 puntos, personajes desbloqueados, ir a cualquier sala y contador de rendimiento.
- **Salir → Menú principal** ya vuelve al título.
- **"GFX : Low"** ya no causa parpadeos.
- **Arranque más rápido:** de 21,6 a 7,6 s hasta el primer frame.
- **Menos pausas al volver a salas ya visitadas:** se mantienen más texturas en memoria (en una sesión de 75 min, las entradas de más de 0,3 s bajaron de 9 a 4).
- Corregido el número de fps del contador de rendimiento de TRUCOS.
- Con teclado en PC: flechas, Enter y Escape en el menú del Boss Rush.

### Pruebas en la Vita (versión AOT, la de 60 fps)
- Partida de 42 minutos jugada a mano (Wyatt, hasta el jefe de hielo): sin cuelgues.
- Partida nueva automática y recorrido de unas 220 salas con cada personaje: Wyatt, Reina y Gilda sin cuelgues; con Vladyn hubo un cuelgue suelto que no se ha vuelto a repetir.
- **28 jefes, cada uno en su propia sala:** sin cuelgues, a 60 pasos por segundo.
- **Sesión larga de 74 minutos** (las 245 salas tres veces, con botones al azar): sin cuelgues, 0,42 % de frames perdidos y **sin fugas de memoria**. La memoria del juego se estabiliza en unos 78 MB a partir del minuto 40.

### Rendimiento de este VPK
Es la versión de **intérprete**: no lleva código del juego, por eso se puede compartir. Va bien en la mayoría de salas, pero en las más cargadas baja a unos 34 fps. La versión rápida (AOT) se compila desde tu propia copia del juego (ver README).

### Problemas conocidos
- En las ranuras de partida, "Lv/Time/Crew/Location" siguen en inglés: forman parte del dibujo del juego.
- Con la fuente original, algún texto en español puede quedar justo en su cuadro; la fuente del sistema es más estrecha.
- Primera entrada a una sala lejana (al cargar partida o saltar con TRUCOS): pausa de 0,3–0,6 s leyendo texturas de la tarjeta.
- Boss Rush: Ruckjahl y Dhistahn poseído solo aparecen si ya los grabaste jugando la historia; el modo aleatorio es una primera versión.

### Instalación
1. Instala `vsvita.vpk` con VitaShell. Necesitas `ur0:data/libshacccg.suprx` (extráelo con ShaRKBR33D).
2. Copia `data.win`, `data_1/`, `data_2/`, `data_5/` y los `music_*.ogg` de tu carpeta de Steam a `ux0:data/valdis/game/`.
3. Abre el juego: el primer arranque convierte los datos (unos minutos). Pulsa SELECT para elegir español.
```

## Release anterior: v0.5.0-alpha (19-11, publicado)

### Pasos que se siguieron

1. En la web del repo: **Releases → Draft a new release**.
2. **Choose a tag:** escribe `v0.5.0-alpha` → *Create new tag on publish* (sobre `main`).
3. **Release title:** `Valdis Story PSVITA v0.5.0-alpha`
4. Marca **Set as a pre-release**.
5. Arrastra el VPK **sin código del juego** (probado en la Vita el 20-11: título en español y menú MODS): `build\vita_noaot\vsvita.vpk` (dentro de la carpeta del repo) (1,3 MB).
   - **No subas** `build\vita\vsvita.vpk` (15 MB): es la versión AOT y lleva código derivado del juego. Es solo para uso personal.
   - Probado en la Vita el 19-11: arranca, carga la partida y se juega (Azudor 57–59 fps; sewer_11 ~34 fps).
6. Pega las notas de abajo en la descripción y pulsa **Publish release**.

### Notas del release (copiar y pegar)

```markdown
## Valdis Story PSVITA v0.5.0-alpha

Port nativo de **Valdis Story: Abyssal City** para PS Vita: un runner propio de GameMaker Studio 1.x escrito en C++17 (no es emulador ni YoYo Loader). El juego se convierte desde **tu copia de Steam** en la propia consola.

**ULTRA IA VIBECODED!** Todo el código, las herramientas y la documentación los escribió una IA (Claude Opus 5.5 como modelo principal y Claude Sonnet 5.5 en subagentes, con Claude Code); el usuario dirigió y probó cada build en una PS Vita real.

> ⚠️ **No incluye el juego.** Necesitas una copia legal de Steam (probado con la build 722041, App ID 252030; `data.win` SHA-256 `0a9c9133…647d010f`, ver README). Este VPK solo trae el motor. Valdis Story es propiedad de Endless Fluff Games; proyecto de fans sin ánimo de lucro.

### Qué incluye
- Las 187 funciones y 51 variables integradas de GameMaker que usa el juego.
- Conversión de `data.win` en la consola (texturas DXT con LZ4, sonidos, salas) con barra de progreso.
- Carga de texturas y música en segundo plano, precarga de salas vecinas, 4 núcleos (con CapUnlocker).
- Herramientas de prueba en `ux0:data/valdis/save/` (saltos de sala, modo sin morir, recorrido, estrés, perfiles): ver `docs/TESTING.md`.

### Estado (medido en la consola con la versión compilada AOT)
- Recorrido de 245 salas: 211 sin ningún frame perdido; 0,32 % de frames perdidos en total.
- Jefe Azudor, outer_slum_2 (430 instancias), sewer_11 y temple_5 a 60 fps.
- Límite actual: cada enemigo activo cuesta ~0,9 ms; en las salas más pesadas caben 3–4 enemigos extra antes de bajar de 60.
- Si una sala pesada no llega, el motor salta el dibujo de un frame (nunca dos seguidos) para que la velocidad del juego no cambie.
- Recorrido automático de las 245 salas con Gilda en la Vita sin cuelgues (0,81 % de frames perdidos); con los 4 personajes en PC, también sin cuelgues.
- Boss Rush de prueba (36 jefes seguidos) en PC sin cuelgues.
- Corregido: tirón de ~1 s al aparecer algunos jefes (se leía el sonido entero de la tarjeta para saber su duración).
- Corregido: animaciones de ataque de enemigos que no salían, retratos de diálogo vacíos y tirones al entrar en salas vecinas.
- **Este VPK es la versión de intérprete** (sin código del juego, por eso se puede compartir). Medido en la Vita: el jefe Azudor va a 57–59 fps, pero las salas más cargadas (p. ej. sewer_11) bajan a ~34 fps. La versión rápida (AOT, 60 fps en esas salas) se compila desde tu propia copia (ver README).

### Problemas conocidos
- La Arena del pueblo baja a ~40 pasos/s si se mete un jefe con la herramienta de prueba (sus NPC reaccionan), pero eso no pasa jugando: Ragano en su sala real va a 60 pasos/s también con este VPK (0,4 % de frames perdidos).
- Primera entrada a algunas salas: pausa de 0,3–0,7 s cargando texturas.
- Campaña completa todavía sin jugar de principio a fin en la Vita (sí probadas todas las salas y jefes con herramientas).
- Iconos de botones del juego como mando de PC.

### Instalación
1. Instala `vsvita.vpk` con VitaShell. Necesitas `ur0:data/libshacccg.suprx` (extráelo con ShaRKBR33D).
2. Copia `data.win`, `data_1/`, `data_2/`, `data_5/` y los `music_*.ogg` de tu carpeta de Steam a `ux0:data/valdis/game/`.
3. Abre el juego: el primer arranque convierte los datos (unos minutos).
```

## Pendiente antes del Release
- [x] Probar `build\vita_noaot\vsvita.vpk` en la Vita (19-11: arranca y se juega; Azudor 57–59 fps, sewer_11 ~34 fps).
- [ ] Conversión desde cero con este VPK (se probó con la versión AOT, que usa el mismo código de conversión).
- [x] Revisar las notas con lo último que se haya medido (sección "Estado").
- [x] VPK público recompilado y probado en la Vita el 19-11 (carga partida y combate en la Arena; ~40 pasos/s en jefes).
