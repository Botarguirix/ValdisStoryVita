# Estructura común de los repos de ports a PS Vita

Plantilla para todos los ports publicados en <https://github.com/Botarguirix> (Touhou123Vita, DDVita, ValdisStoryVita…). Cada repo nuevo debe seguirla; copia este archivo a `docs/ESTRUCTURA_REPOS.md` del repo nuevo.

## 1. Regla de oro: qué NO se sube nunca

| Nunca | Por qué |
|---|---|
| Archivos del juego original (`data.win`, `.dat`, `_Data/`, `.exe`, `.dll`, música, sprites, textos) | Copyright del autor |
| Material extraído, convertido o decompilado del juego (`decomp/`, `extracted/`, `data/`, código traducido tipo AOT) | Es derivado del juego |
| VPK que lleve código o recursos del juego | Igual: solo se publica el VPK con el motor |
| Partidas, logs, capturas de la consola con datos personales | Privacidad |
| Credenciales, tokens, `.env`, rutas locales con el nombre de usuario | Seguridad y privacidad (usar `tools/local.env`, ignorado) |
| Herramientas de terceros empaquetadas (`.exe`, `.zip`, repos vendorizados) | Licencias aparte: se enlazan, no se copian |

El VPK publicado **solo** trae el motor; el usuario aporta su copia legal y la consola convierte los datos.

## 2. Árbol del repositorio

```
<Proyecto>Vita/
├── README.md              # bilingüe ES/EN (ver sección 3)
├── .gitignore             # ver sección 4
├── .gitattributes         # * text=auto eol=lf ; *.png binary
├── docs/
│   ├── ROADMAP.md         # estado, medidas en la consola, pendientes, reglas del proyecto
│   ├── TESTING.md         # herramientas de prueba / trucos y cómo se verifica
│   ├── DATA.md            # qué archivos del juego hacen falta y cómo se convierten
│   ├── ESTRUCTURA_REPOS.md# esta guía
│   └── (VIABILIDAD.md, NOTAS_*.md, imágenes como controls.png)
├── <motor>/ o src/ + include/   # código propio del port (CMake)
│   └── vita/sce_sys/      # icono y LiveArea PROPIOS (sin arte del juego)
├── tools/                 # scripts propios: build, deploy, conversión, informes
│   └── local.env          # (ignorado) rutas de esta máquina
├── tests/                 # pruebas en host si las hay
└── .github/workflows/     # CI opcional (pruebas en host)
```

## 3. README.md (orden fijo)

1. `# <Juego> PSVITA` y enlaces `[Español](#español) · [English](#english)`. Opcional: imagen de controles (`docs/controls.png`).
2. **Español**
   1. Qué es (port nativo / runner propio, no emulador) y de dónde salen los datos (copia de Steam del usuario).
   2. `**Versión X.Y.Z(-alpha|-beta)** · Title ID ... · Estado: ...`
   3. `### ⚠️ Aviso legal`: no piratería, el repo/VPK no incluye el juego, build de Steam probada (número y App ID), propietario del juego, proyecto sin ánimo de lucro.
   4. `### Requisitos` (consola: HENkaku/Enso, VitaShell, `libshacccg.suprx`, plugins si hacen falta, espacio libre; archivos del juego necesarios).
   5. `### Instalación` (descargar de Releases, carpeta en `ux0:data/<proyecto>/`, primer arranque).
   6. `### Controles` (tabla).
   7. `### Compilar` (comandos CMake/VitaSDK).
   8. `### Documentación` (enlaces a `docs/`).
3. **English**: resumen equivalente (aviso legal completo, instalación y enlace a compilar).

## 4. .gitignore mínimo

```
# Datos del juego y material derivado
/data/  /decomp/  /extracted/  /original/  /assets/  /analysis/  /ref/
# Builds, paquetes y binarios
**/build*/  **/CMakeFiles/  CMakeCache.txt  /dist/  /artifacts/
*.vpk *.self *.elf *.velf *.exe *.dll *.so *.a *.o *.obj *.bin *.zip
# Partidas, logs, capturas
*.dat *.sav *.log *.jpg (salvo docs/)  gmon.out
# Python
__pycache__/  *.pyc
# Local y credenciales
tools/local.env  .env  .env.*  *.pem  *.key  .claude/  .codex/  .vs/  .vscode/
```

## 5. Releases

- **Etiqueta:** `vMAYOR.MENOR.PARCHE` con sufijo `-alpha` (jugable con fallos conocidos), `-beta` (todo el juego probado, puliendo) o sin sufijo (`v1.0.0`: terminado).
- **Título:** `<Juego> PSVITA vX.Y.Z-alpha`.
- **Archivo adjunto:** el VPK **solo con el motor** (nombre fijo, p. ej. `vsvita.vpk`), comprobado en la consola antes de publicar.
- **Notas (en este orden):** qué es · aviso legal corto · novedades · estado medido en la consola (fps, salas/zonas probadas) · problemas conocidos · instalación en 3 pasos · build de Steam probada.
- Nunca adjuntar datos del juego ni builds con código del juego.

## 6. Commits

- Mensajes en español, en presente, explicando el porqué.
- Antes de subir: `git status` y revisar que no entra nada de la sección 1 (buscar el nombre de usuario de Windows, IPs y rutas `C:/Users/...`).
