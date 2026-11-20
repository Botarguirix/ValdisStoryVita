# Icono y LiveArea de un port a PS Vita (guía base)

Base común para todos los ports (Touhou123Vita, DDVita, ValdisStoryVita…). Copia este archivo y los dos scripts (`tools/make_livearea.py`, `tools/make_livearea_personal.py`) al proyecto nuevo y cambia solo el dibujo y las coordenadas.

## 1. Archivos y formato exacto

| Archivo en el VPK | Tamaño | Qué es |
|---|---|---|
| `sce_sys/icon0.png` | **128×128** | La burbuja del menú |
| `sce_sys/pic0.png` | **960×544** | Imagen que se ve mientras arranca el juego (opcional, recomendada) |
| `sce_sys/livearea/contents/bg.png` | **840×500** | Fondo de la LiveArea |
| `sce_sys/livearea/contents/startup.png` | **280×158** | Botón "Iniciar" (la puerta) |
| `sce_sys/livearea/contents/template.xml` | — | Dice qué imagen es cada cosa |

**Todas las PNG deben ser de 8 bits con paleta (modo `P`).** Con color verdadero, o si Pillow guarda con menos bits porque hay pocos colores, la instalación falla con el error **0x8010113D**. La forma segura (Pillow):

```python
q = img.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
pal = q.getpalette()[:768]
q.putpalette(pal + [0] * (768 - len(pal)))   # paleta completa de 256 entradas -> siempre 8 bits por píxel
q.save(path, optimize=True)
```

Comprobación rápida: `Image.open(p).mode == 'P'` y `len(getpalette()) == 768`. Los iconos del propio sistema (`vs0:app/NPXS*`) siguen ese formato.

`template.xml` mínimo (estilo `a1`: fondo + puerta):

```xml
<?xml version="1.0" encoding="utf-8"?>
<livearea style="a1" format-ver="01.00" content-rev="1">
  <livearea-background>
    <image>bg.png</image>
  </livearea-background>
  <gate>
    <startup-image>startup.png</startup-image>
  </gate>
</livearea>
```

Composición: el sistema dibuja la puerta (`startup.png`) en el centro del fondo, así que el logo o el título del fondo deben ir hacia un lado (o arriba) para que no lo tape.

## 2. Dos versiones: original y personal

| | Original | Personal |
|---|---|---|
| Arte | Dibujado por script, **sin nada del juego** | Arte de la copia del usuario (pantalla de título, logo) |
| Script | `tools/make_livearea.py` | `tools/make_livearea_personal.py <origen>` |
| Carpeta | `<motor>/vita/sce_sys/` (en git) | `<motor>/vita/sce_sys_personal/` (**en `.gitignore`**) |
| Va en | El VPK público (Release) | Solo el VPK personal |

CMake elige: el VPK público siempre lleva el original; el personal usa `sce_sys_personal/` si existe. En Valdis:

```cmake
set(SCE_ART ${CMAKE_SOURCE_DIR}/vita/sce_sys)
if(VS_AOT AND EXISTS ${CMAKE_SOURCE_DIR}/vita/sce_sys_personal/icon0.png)
    set(SCE_ART ${CMAKE_SOURCE_DIR}/vita/sce_sys_personal)
endif()
vita_create_vpk(... FILE ${SCE_ART}/icon0.png sce_sys/icon0.png
                    FILE ${SCE_ART}/pic0.png sce_sys/pic0.png
                    FILE ${SCE_ART}/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
                    FILE ${SCE_ART}/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
                    FILE ${CMAKE_SOURCE_DIR}/vita/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
```

## 3. De dónde sacar el arte personal

En orden de calidad:
1. **Las texturas del propio juego** (pantalla de título y logo), exportadas de la copia del usuario. Valdis: página 65, `ui_splash` (fondo del título) y `ui_logo`; las coordenadas salen de `vsdinfo game.vsd bg <n>`. Para encontrar qué imagen usa una pantalla: `vsdinfo roombgs <sala>`, o una hoja de contacto de todas las páginas.
2. **La caché de Steam** del usuario: `Steam/appcache/librarycache/<AppID>/` trae `library_hero.jpg`, `library_600x900.jpg`, `header.jpg` y a veces `logo.png` (DDVita usa `library_hero` y `logo`). Suelen ser de menos resolución o capturas de juego.
3. El icono del `.exe` (exportado a PNG de 256 px), para la burbuja.

Nunca se suben al repo ni se adjuntan a un Release.

## 4. Comprobar en la consola

1. Instalar el VPK (si el juego ya estaba instalado, la LiveArea se actualiza al reinstalar).
2. Ver la burbuja en el menú y abrir la LiveArea tocándola (el agente no puede pulsar el botón PS: esto lo hace el usuario).
3. `pic0.png` aparece un instante al lanzar el juego; después sale la animación de vitaGL si el motor la usa.

## 5. Lista para un proyecto nuevo

- [ ] Copiar `tools/make_livearea.py`, cambiar colores, título y dibujo.
- [ ] Copiar `tools/make_livearea_personal.py`, cambiar el origen del arte y las coordenadas.
- [ ] `template.xml` con el estilo `a1`.
- [ ] CMake con `SCE_ART` y `pic0.png`.
- [ ] `.gitignore` con `vita/sce_sys_personal/`.
- [ ] Comprobar modo `P` y paleta de 768 en las cuatro PNG.
- [ ] Instalar y mirar la burbuja y la LiveArea en la Vita.
