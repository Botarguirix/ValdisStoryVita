# Guía base para publicar un port de PS Vita

Plantilla para todos los ports publicados en <https://github.com/Botarguirix> (Touhou123Vita, DDVita, ValdisStoryVita…). Copia este archivo a `docs/ESTRUCTURA_REPOS.md` del repo nuevo y sigue las secciones en orden. Va junto con [LIVEAREA.md](LIVEAREA.md) (icono y LiveArea) y [PUBLICAR.md](PUBLICAR.md) (los pasos concretos de este repo).

## 1. Regla de oro: qué NO se sube nunca

| Nunca | Por qué |
|---|---|
| Archivos del juego original (`data.win`, `.dat`, `_Data/`, `.exe`, `.dll`, música, sprites, textos) | Copyright del autor |
| Material extraído, convertido o decompilado del juego (`decomp/`, `extracted/`, `data/`, código traducido tipo AOT) | Es derivado del juego |
| Fragmentos de código decompilado copiados en la documentación | Igual: en los docs se citan nombres de scripts o variables, nunca su código |
| VPK que lleve código o recursos del juego | Solo se publica el VPK con el motor |
| Arte del juego para el icono/LiveArea (`sce_sys_personal/`) | Copyright; ver LIVEAREA.md |
| Partidas, logs, capturas de la consola con datos personales | Privacidad |
| Credenciales, tokens, `.env`, rutas locales con el nombre de usuario, IP de la consola | Seguridad y privacidad (usar `tools/local.env`, ignorado) |
| Correo personal en los commits | Usar el correo *noreply* de GitHub (sección 7) |
| Herramientas de terceros empaquetadas (`.exe`, `.zip`, repos vendorizados) | Licencias aparte: se enlazan, no se copian |

El VPK publicado **solo** trae el motor; el usuario aporta su copia legal y la consola convierte los datos.

## 2. Árbol del repositorio

```
<Proyecto>Vita/
├── README.md              # bilingüe ES/EN (sección 3)
├── .gitignore             # sección 5
├── .gitattributes         # * text=auto eol=lf ; *.png binary
├── docs/
│   ├── controls.png       # mapa de controles (sección 4), generado por tools/make_controls.py
│   ├── ROADMAP.md         # estado, medidas en la consola, pendientes, reglas del proyecto
│   ├── TESTING.md         # herramientas de prueba / trucos y cómo se verifica
│   ├── DATA.md            # qué archivos del juego hacen falta y cómo se convierten
│   ├── ESTRUCTURA_REPOS.md# esta guía
│   ├── LIVEAREA.md        # icono y LiveArea: formatos exactos y versión original/personal
│   ├── PUBLICAR.md        # pasos concretos de publicación y notas del release de este repo
│   └── (VIABILIDAD.md, NOTAS_*.md)
├── <motor>/ o src/ + include/   # código propio del port (CMake)
│   └── vita/sce_sys/      # icono y LiveArea PROPIOS (sin arte del juego); sce_sys_personal/ ignorado
├── tools/                 # scripts propios: build, deploy, conversión, informes, make_controls.py, make_livearea*.py
│   └── local.env          # (ignorado) rutas de esta máquina
├── tests/                 # pruebas en host si las hay
└── .github/workflows/     # CI opcional (pruebas en host)
```

## 3. README.md (orden fijo)

1. `# <Juego> PSVITA`, debajo **`ULTRA IA VIBECODED!`** y los enlaces `[Español](#español) · [English](#english)`.
2. La imagen de controles: `![Mapa de controles de PS Vita / PS Vita control map](docs/controls.png)`.
3. **Español**
   1. **Qué es:** port nativo / runner propio (no emulador) y que los datos salen de la copia del usuario.
   2. `**Versión X.Y.Z(-alpha|-beta)** · Title ID ... · Estado: ...`
   3. **`### 🤖 Proyecto 100 % "vibecoded"`** (sección 3.1).
   4. **`### ⚠️ Aviso legal`** (sección 3.2).
   5. **`### Versión de Steam necesaria`** (sección 3.3).
   6. `### Requisitos`: consola (HENkaku/Enso, VitaShell, `libshacccg.suprx`, plugins si hacen falta), espacio libre y **relojes**: el port va con los de serie (500/222 MHz); subirlos es decisión del usuario, con más calor y desgaste.
   7. `### Instalación`: descargar de Releases, carpeta `ux0:data/<proyecto>/`, primer arranque.
   8. `### Controles`: tabla con la **acción del juego** de cada botón (no "face1/face2") y cómo se regenera la imagen.
   9. `### Compilar`: comandos CMake/VitaSDK.
   10. `### Créditos`: autor del juego, VitaSDK, vitaGL y librerías, herramientas de análisis usadas solo en local.
   11. `### Documentación`: enlaces a `docs/`.
4. **English**: lo mismo resumido, con el aviso legal, la versión de Steam y los modelos de IA completos.

### 3.1 Bloque "vibecoded" y modelos de IA

Siempre se dice claro que **todo** (código, herramientas, pruebas, documentación) lo escribió una IA, qué hizo el usuario (dirigir, probar en la Vita real, dar feedback) y que puede haber errores sin revisar a mano. Tabla de modelos:

| Modelo de IA | Uso |
|---|---|
| **<Modelo principal>** (empresa) | Modelo principal: diseño, código, depuración en la Vita, documentación |
| **<Modelo secundario>** (empresa) | Subagentes / tareas concretas |

Más la herramienta (Claude Code, Codex…). **Cómo saber qué modelos se usaron** (no adivinar): en Claude Code, los registros de la sesión en `~/.claude/projects/<carpeta-del-proyecto>/*.jsonl` llevan el campo `"model"`; contarlos con:

```bash
grep -ohE '"model":"[a-z0-9.-]+"' ~/.claude/projects/<carpeta>/*.jsonl ~/.claude/projects/<carpeta>/*/*.jsonl | sort | uniq -c
```

Si se usó Codex u otra herramienta, añadirla con su modelo (Touhou123Vita usó también Codex: ver su `docs/CODEX_TAREAS.md`).

### 3.2 Aviso legal (plantilla)

- **Este proyecto NO apoya, promueve ni facilita la piratería.** Hace falta la **propia copia legal** del juego (tienda). Sin enlaces a copias no autorizadas; no pedir ni compartir archivos del juego.
- El repositorio y el VPK **no incluyen el juego** (ni código, gráficos, música, textos ni ejecutable); el VPK solo trae el motor y no funciona solo.
- Si existe una versión con código traducido del juego (AOT, recompilación): **solo uso personal**, no se comparte. Si el VPK publicado lleva código traducido (caso Touhou123Vita), decirlo explícitamente.
- Propietario del juego, proyecto de fans sin ánimo de lucro, sin relación con el estudio, la tienda ni Sony. Tener una copia no da permiso para redistribuirla.

### 3.3 Versión de la tienda necesaria

Tabla con: juego y tienda, **App ID**, **número de build** probado y el **tamaño y SHA-256** del archivo principal, más el comando para comprobarlo:

```powershell
Get-FileHash "<ruta de Steam>\<archivo principal>" -Algorithm SHA256
```

- El número de build está en `Steam/steamapps/appmanifest_<AppID>.acf` (`"buildid"`).
- El hash: `sha256sum <archivo>` en Git Bash o `Get-FileHash` en PowerShell.
- Si hay parches oficiales necesarios (como Touhou 12.3 → 1.10a), enlazar la página oficial del desarrollador.

## 4. Imagen de controles (`docs/controls.png`)

- Se genera con un script propio (`tools/make_controls.py`, Pillow): una Vita dibujada a mano (sin fotos ni arte del juego), las etiquetas en **español / inglés** con líneas a cada botón, la versión arriba a la derecha y una barra inferior con combinaciones y notas.
- Estilo común (Touhou123Vita, ValdisStoryVita): fondo claro `#EEF2F7`, consola azul oscuro, fuentes Segoe UI (`segoeui.ttf` / `segoeuib.ttf`) y **Segoe UI Symbol** (`seguisym.ttf`) para △ ○ ✕ □ (las otras fuentes no traen esos símbolos y salen como cuadros). 1800×1120.
- Las acciones salen **del código**, no de memoria: el mapeo Vita → mando del port (en Valdis `runner/src/main_vita.cpp`) y los valores por defecto del juego (en Valdis `global.joy_*` de `vs_settings.ini`; constantes de GameMaker: 32769 ✕, 32770 ○, 32771 □, 32772 △, 32773 L1, 32774 R1, 32775 L2, 32776 R2, 32777 SELECT, 32778 START).
- Revisar la imagen antes de subirla: que ningún texto se salga ni tape la consola.
- Para un proyecto nuevo: copiar el script y cambiar `TITLE`, `VERSION`, `SCREEN` y las etiquetas.

## 5. .gitignore mínimo

```
# Datos del juego y material derivado
/data/  /decomp/  /extracted/  /original/  /assets/  /analysis/  /ref/
# Builds, paquetes y binarios
**/build*/  **/CMakeFiles/  CMakeCache.txt  /dist/  /artifacts/
*.vpk *.self *.elf *.velf *.exe *.dll *.so *.a *.o *.obj *.bin *.zip
# Arte personal del juego
<motor>/vita/sce_sys_personal/
# Partidas, logs, capturas
*.dat *.sav *.log *.jpg (salvo docs/)  gmon.out
# Python
__pycache__/  *.pyc
# Local y credenciales
tools/local.env  .env  .env.*  *.pem  *.key  .claude/  .codex/  .vs/  .vscode/
```

## 6. Revisión antes de publicar (obligatoria)

1. `git ls-files`: solo código propio, docs, scripts y el arte original de `sce_sys/`.
2. Buscar datos personales **en todo el historial**, no solo en los archivos actuales:
   ```bash
   for p in <usuario_windows> <nombre> <correo> 192.168 "C:/Users" 'C:\\Users'; do git log --all --oneline -S"$p"; done
   ```
   Si aparece algo, corregir los archivos y reescribir el historial **antes** de publicar (después ya no sirve): `git filter-branch --tree-filter "<script que corrige>" main`.
3. Buscar código del juego en los docs: bloques de código con GML/C# decompilado.
4. Correo de los commits: `git log --format='%ae' | sort -u` debe dar solo el *noreply* (sección 7).

## 7. Correo *noreply* de GitHub

El correo de cada commit es público. Usar `<ID>+<usuario>@users.noreply.github.com`:

- El ID numérico sale de `https://api.github.com/users/<usuario>` (campo `"id"`). Botarguirix: `332337963+Botarguirix@users.noreply.github.com`.
- En el repo: `git config user.email <noreply>`.
- Si ya hay commits con el correo personal y el repo **no** se ha publicado: `git filter-branch --env-filter "export GIT_AUTHOR_EMAIL=<noreply> GIT_COMMITTER_EMAIL=<noreply>" main`.
- En GitHub → Settings → Emails, activar "Keep my email addresses private" y "Block command line pushes that expose my email".

## 8. Subir con GitHub Desktop

La cuenta usa verificación en dos pasos, así que la IA no inicia sesión: el usuario publica desde GitHub Desktop.

1. **File → Add local repository…** → la carpeta del repo.
2. **Publish repository**: nombre `<Proyecto>Vita`, descripción corta, desmarcar **Keep this code private**.
3. Comprobar en la web que no aparecen `build/`, `decomp/`, `ref/` ni VPK.
4. Commits nuevos: revisar la lista de cambios antes de **Commit to main** + **Push origin**.

## 9. Releases

- **Etiqueta:** `vMAYOR.MENOR.PARCHE` con sufijo `-alpha` (jugable con fallos conocidos), `-beta` (todo el juego probado, puliendo) o sin sufijo (`v1.0.0`: terminado).
- **Título:** `<Juego> PSVITA vX.Y.Z-alpha`. Marcar *pre-release* si es alpha/beta.
- **Archivo adjunto:** el VPK **solo con el motor** (nombre fijo, p. ej. `vsvita.vpk`), **instalado y probado en la Vita** con esa misma build antes de publicar.
- **Notas (en este orden):** qué es · "vibecoded" y modelos · aviso legal corto con la versión de la tienda · novedades · estado medido en la consola (fps, salas/zonas probadas) · problemas conocidos · instalación en 3 pasos.
- Nunca adjuntar datos del juego ni builds con código del juego (salvo que el proyecto lo declare, como Touhou123Vita).

## 10. Commits

- Mensajes en español, en presente, explicando el porqué.
- Antes de cada push, repetir la revisión de la sección 6 sobre los commits nuevos.
