cmake_minimum_required(VERSION 3.16)
project(ValdisStoryVita C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Codigo nativo (AOT) generado con vsaot a partir de TU game.vsd: build/aot_gen. Opcional; sin el, todo se interpreta.
option(VS_AOT "Compilar los scripts del juego a codigo nativo (build/aot_gen)" ON)
set(VS_AOT_DIR "${CMAKE_SOURCE_DIR}/../build/aot_gen" CACHE PATH "Carpeta con el codigo AOT generado por vsaot")
set(AOT_DIR ${VS_AOT_DIR})
# Solo herramientas de PC (vsconv, vsaot, vsdinfo), sin el runner de PC ni SDL2: lo usa tools/build_aot_vpk.sh
option(VS_TOOLS_ONLY "Compilar solo las herramientas de PC" OFF)
set(AOT_SRC "")
if(VS_AOT AND EXISTS ${AOT_DIR}/aot_table.cpp)
    file(GLOB AOT_SRC ${AOT_DIR}/aot_*.cpp)
    set_source_files_properties(${AOT_SRC} PROPERTIES COMPILE_OPTIONS "-fno-exceptions;-Wno-unused-label;-Wno-unused-variable;-Wno-unused-but-set-variable")
    # Vita: el codigo AOT es enorme (34 MB) y la cache de instrucciones de la Cortex-A9 es de 32 KB: scripts como
    # nme_v_demon_script ocupaban 107 KB con -O3 y se ejecutan en cada trampa/enemigo y frame. -Os reduce su tamano.
    # 2026-11-19: con -Os el codigo baja de 36 a 21 MB. El cuelgue al probarlo resulto ser el bloqueo de archivos del
    # agente (con -O3 tambien se colgaba hasta reiniciar la Vita), asi que falta medirlo bien. Por defecto -O3.
    option(VS_AOT_OS "AOT con -Os en la Vita (pendiente de medir)" OFF)
    if(VITA AND VS_AOT_OS)
        set_property(SOURCE ${AOT_SRC} APPEND PROPERTY COMPILE_OPTIONS "-Os")
    endif()
    message(STATUS "AOT: ${AOT_DIR}")
endif()

set(CORE_SRC
    src/vsd.cpp src/vm.cpp src/runtime.cpp
    src/fn_core.cpp src/fn_draw.cpp src/fn_audio.cpp src/particles.cpp
    src/gfx.cpp src/audio.cpp src/bgio.cpp
    src/convert.cpp src/convert_vsd.cpp src/stbimpl.cpp src/debugtour.cpp src/lang.cpp)

# vsdinfo: herramienta de volcado (sin dependencias)
if(NOT VITA)
    add_executable(vsdinfo src/vsd.cpp tools/vsdinfo.cpp)
    add_executable(vsaot src/vsd.cpp tools/vsaot.cpp)
    target_include_directories(vsaot PRIVATE src)
    add_executable(vsconv src/convert.cpp src/convert_vsd.cpp src/stbimpl.cpp tools/vsconv.cpp)
    target_include_directories(vsconv PRIVATE src)
    target_link_libraries(vsconv PRIVATE z)
    target_include_directories(vsdinfo PRIVATE src)
endif()

if(VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    set(VITA_APP_NAME "Valdis Story PSVITA")
    set(VITA_TITLEID "VALDIS001")
    set(VITA_VERSION "00.06")
    add_executable(vsvita ${CORE_SRC} ${AOT_SRC} src/main_vita.cpp)
    if(AOT_SRC)
        target_compile_definitions(vsvita PRIVATE VS_HAVE_AOT)
        target_include_directories(vsvita PRIVATE ${AOT_DIR})
    endif()
    target_include_directories(vsvita PRIVATE src)
    # el runtime registra las funciones con constructores estaticos: no descartar objetos
    target_compile_options(vsvita PRIVATE -O3 -Wall -Wno-unused-function -fno-fast-math)
    target_link_libraries(vsvita PRIVATE
        vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub
        vorbisfile vorbis ogg z
        SceKernelDmacMgr_stub SceCommonDialog_stub SceGxm_stub SceDisplay_stub SceSysmodule_stub SceAppMgr_stub
        SceCtrl_stub SceTouch_stub ScePvf_stub SceAudio_stub ScePower_stub SceRtc_stub SceLibKernel_stub SceIofilemgr_stub
        pthread m c)
    vita_create_self(eboot.bin vsvita UNSAFE)
    # Arte de la burbuja y la LiveArea (docs/LIVEAREA.md): el original (tools/make_livearea.py) va siempre en el VPK
    # publico (sin AOT). La version con AOT, que es de uso personal, usa vita/sce_sys_personal si existe (arte de tu
    # copia del juego, tools/make_livearea_personal.py; no se sube a git).
    set(SCE_ART ${CMAKE_SOURCE_DIR}/vita/sce_sys)
    if(VS_AOT AND EXISTS ${CMAKE_SOURCE_DIR}/vita/sce_sys_personal/icon0.png)
        set(SCE_ART ${CMAKE_SOURCE_DIR}/vita/sce_sys_personal)
    endif()
    message(STATUS "LiveArea: ${SCE_ART}")
    vita_create_vpk(vsvita.vpk ${VITA_TITLEID} eboot.bin
        VERSION ${VITA_VERSION}
        NAME ${VITA_APP_NAME}
        FILE ${SCE_ART}/icon0.png sce_sys/icon0.png
        FILE ${SCE_ART}/pic0.png sce_sys/pic0.png
        FILE ${SCE_ART}/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
        FILE ${SCE_ART}/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
        FILE ${CMAKE_SOURCE_DIR}/vita/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
        FILE ${CMAKE_SOURCE_DIR}/../lang/es.txt lang/es.txt)
elseif(NOT VS_TOOLS_ONLY)
    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    add_executable(vsrunner ${CORE_SRC} ${AOT_SRC} src/main_pc.cpp)
    if(AOT_SRC)
        target_compile_definitions(vsrunner PRIVATE VS_HAVE_AOT)
    endif()
    target_include_directories(vsrunner PRIVATE src ${SDL2_INCLUDE_DIRS})
    target_compile_options(vsrunner PRIVATE -O2 -Wall -Wno-unused-function -fno-fast-math)
    target_link_libraries(vsrunner PRIVATE ${SDL2_LIBRARIES} OpenGL::GL vorbisfile vorbis ogg z)
    if(WIN32)
        target_link_libraries(vsrunner PRIVATE winmm)
    endif()
    if(WIN32)
        target_link_options(vsrunner PRIVATE -mconsole)
    endif()
endif()
