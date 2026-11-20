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

## 2. Crear el Release v0.5.0-alpha

1. En la web del repo: **Releases → Draft a new release**.
2. **Choose a tag:** escribe `v0.5.0-alpha` → *Create new tag on publish* (sobre `main`).
3. **Release title:** `Valdis Story PSVITA v0.5.0-alpha`
4. Marca **Set as a pre-release**.
5. Arrastra el VPK **sin código del juego**: `build\vita_noaot\vsvita.vpk` (dentro de la carpeta del repo) (1,3 MB).
   - **No subas** `build\vita\vsvita.vpk` (15 MB): es la versión AOT y lleva código derivado del juego. Es solo para uso personal.
   - Probado en la Vita el 19-11: arranca, carga la partida y se juega (Azudor 57–59 fps; sewer_11 ~34 fps).
6. Pega las notas de abajo en la descripción y pulsa **Publish release**.

### Notas del release (copiar y pegar)

```markdown
## Valdis Story PSVITA v0.5.0-alpha

Port nativo de **Valdis Story: Abyssal City** para PS Vita: un runner propio de GameMaker Studio 1.x escrito en C++17 (no es emulador ni YoYo Loader). El juego se convierte desde **tu copia de Steam** en la propia consola.

> ⚠️ **No incluye el juego.** Necesitas una copia legal de Steam (probado con la build 722041, App ID 252030). Este VPK solo trae el motor. Valdis Story es propiedad de Endless Fluff Games; proyecto de fans sin ánimo de lucro.

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
- Corregido: animaciones de ataque de enemigos que no salían, retratos de diálogo vacíos y tirones al entrar en salas vecinas.
- **Este VPK es la versión de intérprete** (sin código del juego, por eso se puede compartir). Medido en la Vita: el jefe Azudor va a 57–59 fps, pero las salas más cargadas (p. ej. sewer_11) bajan a ~34 fps. La versión rápida (AOT, 60 fps en esas salas) se compila desde tu propia copia (ver README).

### Problemas conocidos
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
- [ ] Revisar las notas con lo último que se haya medido (sección "Estado").
