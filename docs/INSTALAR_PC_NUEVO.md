# Montar el entorno completo en un PC nuevo (solo con Claude instalado)

Guía para que **Claude Code** (app de escritorio de Claude) deje un PC con Windows 10/11 recién instalado igual que el equipo donde se hizo este port: compilar el runner para PC y Vita, convertir los datos del juego, generar la versión AOT, la LiveArea personal, y controlar la Vita desde el PC. Sirve de base para otros ports (cambia solo las secciones 6–8).

> **Para Claude:** ejecuta las secciones en orden, comprueba cada paso con su comando de verificación y no sigas si falla. Usa la herramienta Bash (Git Bash) salvo donde diga PowerShell o MSYS2. Pide permiso antes de cada descarga (nombre, origen, tamaño). Los pasos marcados con 🙋 los tiene que hacer el usuario: avísale y espera.

## 0. Lo que tiene que hacer el usuario (no se puede automatizar)

| Paso | Por qué |
|---|---|
| 🙋 Aceptar los avisos de Windows (UAC) de cada instalador | Permisos de administrador |
| 🙋 Reiniciar tras instalar WSL y crear el usuario de Ubuntu | Lo pide Windows |
| 🙋 Iniciar sesión en Steam y descargar el juego | Su cuenta |
| 🙋 Iniciar sesión en GitHub Desktop (verificación en dos pasos) | Su cuenta |
| 🙋 Aprobar el emparejamiento del agente en la Vita (botón OK en la consola) | Seguridad del agente |
| 🙋 En la app de Claude: Settings → System → Computer use (si quiere que Claude maneje GitHub Desktop) | Ajuste de seguridad |

## 1. Programas base (PowerShell)

```powershell
winget install --id Git.Git -e
winget install --id Python.Python.3.12 -e
winget install --id MSYS2.MSYS2 -e
winget install --id GitHub.GitHubDesktop -e
winget install --id Valve.Steam -e
```

Verificar (en una terminal nueva): `git --version` · `py -3.12 --version` · `Test-Path C:\msys64\usr\bin\bash.exe`.

Python: `py -3.12 -m pip install Pillow cryptography`. Comprobar: `py -3.12 -c "import PIL; print(PIL.__version__)"`.

Versiones del equipo original (19-11-2026): Git 2.56, Python 3.12.10 + Pillow 13.0, MSYS2 con GCC 16.2 / CMake 4.4 / Ninja 1.13, VitaSDK con arm-vita-eabi-gcc 15.2.

## 2. MSYS2: compilador de PC y dependencias

En la terminal **MSYS2 UCRT64** (`C:\msys64\ucrt64.exe`), o desde Bash con `C:/msys64/usr/bin/bash -lc "..."`:

```bash
pacman -Syu --noconfirm      # puede pedir cerrar y volver a ejecutar
pacman -S --needed --noconfirm git make curl tar unzip \
  mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-libogg mingw-w64-ucrt-x86_64-libvorbis
```

Verificar: `C:/msys64/ucrt64/bin/gcc --version`, `cmake --version`, `ninja --version`.

En Git Bash, para compilar se usa siempre: `export PATH=/c/msys64/ucrt64/bin:$PATH`.

## 3. VitaSDK (con vitaGL, vitashark, math-neon y SceShaccCgExt)

En MSYS2 UCRT64:

```bash
export VITASDK=/c/vitasdk
export PATH=$VITASDK/bin:$PATH
git clone https://github.com/vitasdk/vdpm /c/vdpm && cd /c/vdpm
./bootstrap-vitasdk.sh
./install-all.sh             # todos los paquetes de vdpm, incluidos vitaGL, vitashark, mathneon, libvorbis, zlib
```

Variable para todo el sistema (PowerShell): `[Environment]::SetEnvironmentVariable('VITASDK','C:\vitasdk','User')`.

Verificar que existen estas librerías en `C:\vitasdk\arm-vita-eabi\lib\`: `libvitaGL.a libvitashark.a libmathneon.a libSceShaccCgExt.a libSceShaccCg_stub.a libvorbisfile.a libvorbis.a libogg.a libz.a libtaihen_stub.a`.

```bash
ls /c/vitasdk/arm-vita-eabi/lib | grep -cE "^lib(vitaGL|vitashark|mathneon|SceShaccCgExt|SceShaccCg_stub|vorbisfile|vorbis|ogg|z|taihen_stub)\.a$"   # debe dar 10
```

## 4. El repositorio

```bash
mkdir -p ~/Documents && cd ~/Documents
git clone https://github.com/Botarguirix/ValdisStoryVita
cd ValdisStoryVita
git config user.name Botarguirix
git config user.email 332337963+Botarguirix@users.noreply.github.com   # correo privado: ver ESTRUCTURA_REPOS.md §7
```

🙋 En GitHub Desktop: File → Add local repository → esa carpeta (para hacer push con su cuenta).

Lo que **no** viene en git y hay que regenerar en el PC nuevo: `build/` (todo), `decomp/`, `tools/utmt_cli/`, `tools/local.env`, `runner/vita/sce_sys_personal/`.

## 5. Runner de PC y herramientas

```bash
export PATH=/c/msys64/ucrt64/bin:$PATH
cmake -S runner -B build/pc -G Ninja && cmake --build build/pc
```

Genera `build/pc/vsrunner.exe`, `vsconv.exe`, `vsaot.exe`, `vsdinfo.exe`.

## 6. El juego y sus datos

1. 🙋 Instalar **Valdis Story: Abyssal City** en Steam (App ID 252030).
2. Comprobar que es la build probada:
   ```bash
   grep '"buildid"' "/c/Program Files (x86)/Steam/steamapps/appmanifest_252030.acf"     # 722041
   sha256sum "/c/Program Files (x86)/Steam/steamapps/common/ValdisStoryAbyssalCity/data.win"
   # 0a9c913393a30039f696df796d94f0b290c2871febb44a58ad9178f5647d010f
   ```
3. **UndertaleModTool CLI** (para exportar el `data.win`): descargar el zip de la CLI para Windows de las releases de <https://github.com/UnderminersTeam/UndertaleModTool/releases> (el equipo original usa la 0.9.2.0, autocontenida con .NET 10) y descomprimirlo en `tools/utmt_cli/` (debe quedar `tools/utmt_cli/UndertaleModCli.exe`).
4. Convertir los datos en el PC (para el runner de PC y para generar el AOT):
   ```bash
   py -3.12 -I tools/build_vsdata.py --steam "C:/Program Files (x86)/Steam/steamapps/common/ValdisStoryAbyssalCity"
   ```
   Sale `build/ux0_data_valdis/` (`vsdata/` + `game/`).
5. Opcional (solo consulta local, nunca se sube): decompilar a `decomp/` con el comando de [NOTAS_DECOMP.md](NOTAS_DECOMP.md).
6. Probar en PC: `build/pc/vsrunner.exe build/ux0_data_valdis/vsdata build/ux0_data_valdis/game build/save`.

## 7. VPK de la Vita

```bash
export VITASDK=/c/vitasdk PATH=/c/vitasdk/bin:/c/msys64/ucrt64/bin:$PATH
# Público (solo motor, el de Releases)
cmake -S runner -B build/vita_noaot -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DVS_AOT=OFF
cmake --build build/vita_noaot
# Personal y rápido (AOT; ~30 min la primera vez; mantener el PC despierto)
build/pc/vsaot.exe build/ux0_data_valdis/vsdata/game.vsd build/aot_gen
cmake -S runner -B build/vita -G Ninja -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
cmake --build build/vita -j 8
```

LiveArea personal (arte de la copia del usuario, ver [LIVEAREA.md](LIVEAREA.md)): exportar la página de textura 65 a `build/tex_png_backup/65.png` y ejecutar
`py -3.12 -I tools/make_livearea_personal.py build/tex_png_backup/65.png "C:/Program Files (x86)/Steam/steamapps/common/ValdisStoryAbyssalCity/Valdis_Story_AC.exe"`, luego recompilar `build/vita`.

## 8. Controlar la Vita desde el PC (vita-agent-use en WSL)

La consola ya tiene instalados los plugins (DMR, CapUnlocker, PSVshell y el agente; si es una Vita nueva, ver `local/vita-plugins/INSTALAR.md` del repo de Touhou123Vita). En el PC solo falta el cliente:

1. PowerShell (admin): `wsl --install -d Ubuntu` → 🙋 reiniciar y crear el usuario de Ubuntu.
2. En Ubuntu:
   ```bash
   sudo apt update && sudo apt install -y python3 python3-pip python3-venv git
   git clone https://github.com/shoui520/vita-agent-use ~/vita-agent-use
   cd ~/vita-agent-use && git checkout ae140b9      # commit con el que se compilaron los plugins de la consola
   python3 -m venv ~/va-venv && ~/va-venv/bin/pip install cryptography
   cat > ~/wsl-agent.sh <<'EOF'
   #!/usr/bin/env bash
   export VITA_AGENT_CONFIG_DIR="$HOME/vita-agent-config"
   cd ~/vita-agent-use
   exec ~/va-venv/bin/python client/vita_agent.py "$@"
   EOF
   chmod +x ~/wsl-agent.sh
   ```
3. Emparejar: `~/wsl-agent.sh bootstrap` (asistente; solo lee) y `~/wsl-agent.sh session pair` → 🙋 tocar OK en la Vita. La Vita y el PC en la misma red.
4. En el repo, `tools/local.env` (ignorado por git):
   ```
   VITA_AGENT_SCRIPT=/home/<usuario_ubuntu>/wsl-agent.sh
   ```
5. Verificar: `tools/va.sh system snapshot` (debe responder la consola) y `tools/deploy.sh --no-launch` instala `build/vita/vsvita.vpk`.

## 9. Reglas del proyecto (la memoria de Claude no viaja entre PCs)

Copiar estas reglas al inicio de la primera conversación en el PC nuevo (o a `CLAUDE.md` local, sin subirlo):

- Prioridad: **estabilidad y juego 1:1 terminado**; las partidas no importan (se pueden borrar o mover). Las herramientas de prueba se conservan: al final serán el menú de **trucos** (cheats). Después: Boss Rush, "masterquest" (más enemigos), randomizer y editor de inventario.
- Rendimiento: objetivo 60 fps; en las salas más pesadas se acepta un mínimo de 30 fps **solo** si la lógica sigue a 60 pasos/s (salto de dibujo).
- Relojes por defecto 500/222 MHz; subirlos lo hace el usuario (avisar de calor y desgaste).
- Nunca subir nada del juego, ni el VPK AOT, ni el arte personal, ni rutas con el usuario de Windows, IPs o correo personal ([ESTRUCTURA_REPOS.md](ESTRUCTURA_REPOS.md)).
- Descargas: pedir permiso con nombre, origen y tamaño.
- No tocar `ur0:tai/config.txt` sin permiso explícito.
- vita-agent: **nunca dos clientes a la vez**; no regenerar ni borrar `~/vita-agent-config`; PS + SELECT detiene el agente; emparejamientos y permisos los aprueba el usuario en la consola.
- Archivos en la Vita: nunca `fs trash` sobre una ruta que se va a reutilizar (la papelera actúa tarde y borra el archivo nuevo); `fs move` no sobrescribe. Apartar con `fs move` a `ux0:data/valdis/pruebas/<nombre único>`.
- No descargar `vsrunner.log` con el juego abierto (bloquea el log): cerrar la app, mover el log a `pruebas/` con nombre único y descargar esa copia.
- Modos de prueba: archivos vacíos en `ux0:data/valdis/save/` (`monkey`, `godmode`, `unlockchars`, `bossrush.txt`, `tour.txt`, `stress.txt`, `warp.txt`, `prof`…, ver [TESTING.md](TESTING.md)). **Quitarlos** antes de que el usuario juegue o publique.
- Publicar: el usuario lo hace con GitHub Desktop (verificación en dos pasos); Claude no intenta iniciar sesión.
- En Bash, los parches con heredoc rompen los `\n` dentro de cadenas de C: escribir los scripts de parche con la herramienta Write o usar Edit.
- Comunicación con el usuario en español.

## 10. Lista final

- [ ] `build/pc/vsrunner.exe` abre el juego en PC.
- [ ] `build/vita_noaot/vsvita.vpk` (~1,4 MB) y `build/vita/vsvita.vpk` (~15 MB) compilan.
- [ ] `tools/va.sh system snapshot` responde.
- [ ] `tools/deploy.sh` instala y el juego arranca en la Vita (captura con `tools/va.sh screen capture --output <ruta WSL>.jpg`).
- [ ] `git status` limpio y GitHub Desktop ve el repo.
