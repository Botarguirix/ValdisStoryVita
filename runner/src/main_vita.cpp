// Plataforma PS Vita: vitaGL, SceCtrl (como mando XInput para el juego), SceAudioOut, datos en ux0:data/valdis.
//
//   ux0:data/valdis/vsdata/   game.vsd + tex/ + snd/   (generado en PC con tools/build_vsdata.py)
//   ux0:data/valdis/game/     data_1/ data_2/ data_5/ music_*.ogg  (copiados de tu carpeta de Steam)
//   ux0:data/valdis/save/     partidas (.ini) y vsrunner.log
//
// Controles (mando del juego): Cruz=face1 Circulo=face2 Cuadrado=face3 Triangulo=face4, L/R fisicos = L2/R2
// (gatillos), panel trasero izquierda/derecha = L1/R1 (hombros), Select/Start, cruceta y stick izquierdo.
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/message_dialog.h>
#include <psp2/power.h>
#include <vitaGL.h>
#include <algorithm>
#include <cstdio>
#include <malloc.h>
#include <cstring>
#include <functional>
#include "sysmutex.h"
#include <pthread.h>
#include "convert.h"
#include "runtime.h"
#include "bgio.h"
#include "gfx.h"
#include "debugtour.h"
#include "audio.h"

// 120 MB: con el codigo nativo (AOT, ~36 MB de ejecutable) 150 MB dejaban el proceso sin memoria antes de main
int _newlib_heap_size_user = 120 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024; // scripts GML anidados (script_execute en cascada)

namespace vs { extern int gWinW, gWinH; }
using namespace vs;

static constexpr const char* kRoot = "ux0:data/valdis";
static volatile bool gAudioRun = false;

static int audioThread(SceSize, void*) {
    const int grain = 1024;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, grain, audio::kRate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return 0;
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB};
    sceAudioOutSetVolume(port, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol);
    static int16_t buf[2][grain * 2];
    int which = 0;
    while (gAudioRun) { audio::mix(buf[which], grain); sceAudioOutOutput(port, buf[which]); which ^= 1; }
    sceAudioOutOutput(port, nullptr);
    sceAudioOutReleasePort(port);
    return 0;
}

static void showError(const std::string& msg) {
    SceMsgDialogUserMessageParam user{};
    user.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    user.msg = (const SceChar8*)msg.c_str();
    SceMsgDialogParam param; sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG; param.userMsgParam = &user;
    if (sceMsgDialogInit(&param) < 0) return;
    while (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        glClearColor(0.05f, 0.05f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT); vglSwapBuffers(GL_TRUE);
    }
    sceMsgDialogTerm();
}

// Primer arranque: convierte los archivos originales de Steam (data.win, imagenes) a los datos del runner.
// Corre en un hilo; el principal mantiene viva la barra de progreso nativa del sistema.
struct ConvJob {
    vs::SysMutex mx; std::string what = "Iniciando..."; float frac = 0; bool done = false, ok = false;
    std::string err;
    std::function<bool(const vs::ConvertProgress&, std::string&)> work;
};
static void* convThread(void* p) {
    ConvJob* j = (ConvJob*)p;
    std::string err;
    bool ok = j->work([j](const char* w, float f) { vs::SysLock lk(j->mx); j->what = w; j->frac = f; }, err);
    vs::SysLock lk(j->mx);
    j->ok = ok; j->err = err; j->done = true;
    return nullptr;
}

// trabajo largo en un hilo con la barra de progreso nativa del sistema (conversion, recompresion de texturas)
static bool runJob(const char* kTitle, std::function<bool(const vs::ConvertProgress&, std::string&)> work, std::string& err) {
    static ConvJob job;
    { vs::SysLock lk(job.mx); job.what = "Iniciando..."; job.frac = 0; job.done = job.ok = false; job.err.clear(); }
    job.work = std::move(work);
    SceMsgDialogProgressBarParam pb{};
    pb.barType = SCE_MSG_DIALOG_PROGRESSBAR_TYPE_PERCENTAGE;
    pb.msg = (const SceChar8*)kTitle;
    SceMsgDialogParam param; sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_PROGRESS_BAR; param.progBarParam = &pb;
    bool dlg = sceMsgDialogInit(&param) >= 0;

    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 1024 * 1024);
    pthread_t th;
    if (pthread_create(&th, &at, convThread, &job) != 0) { if (dlg) sceMsgDialogTerm(); err = "no se pudo crear el hilo de conversion"; return false; }
    std::string lastWhat; int lastPct = -1;
    for (;;) {
        std::string what; float frac; bool done;
        { vs::SysLock lk(job.mx); what = job.what; frac = job.frac; done = job.done; }
        if (done) break;
        int pct = (int)(frac * 100.f);
        if (dlg) {
            if (pct != lastPct) { sceMsgDialogProgressBarSetValue(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, pct); lastPct = pct; }
            if (what != lastWhat) {
                std::string m = std::string(kTitle) + "\n\n" + what;
                sceMsgDialogProgressBarSetMsg(SCE_MSG_DIALOG_PROGRESSBAR_TARGET_BAR_DEFAULT, (const SceChar8*)m.c_str());
                lastWhat = what;
            }
        }
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
        glClearColor(0.05f, 0.05f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT); vglSwapBuffers(GL_TRUE);
    }
    pthread_join(th, nullptr);
    if (dlg) {
        sceMsgDialogClose();
        while (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) { glClear(GL_COLOR_BUFFER_BIT); vglSwapBuffers(GL_TRUE); }
        sceMsgDialogTerm();
    }
    err = job.err;
    return job.ok;
}

static bool runConversion(const std::string& root, const std::string& gameDir, std::string& err) {
    return runJob("Preparando los datos del juego a partir de tus archivos de Steam.\nSolo se hace la primera vez; puede tardar unos minutos.\nNo cierres la aplicacion.",
                  [root, gameDir](const vs::ConvertProgress& p, std::string& e) { return vs::convertGame(gameDir + "/data.win", gameDir, root, p, e); }, err);
}

namespace vs { extern bool gProfPhaseOnly; extern bool gProfLight; }
extern "C" int sceAppMgrGrowMemory(unsigned int size, int killMiniApps); // SceAppMgr (lo habilita el plugin DMR)

// marcas de arranque escritas directamente (sin newlib/bgio): dicen hasta donde llego el programa si se cuelga
static void bootMark(const char* s) {
    SceUID f = sceIoOpen("ux0:data/valdis/save/boot.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
    if (f >= 0) { sceIoWrite(f, s, std::strlen(s)); sceIoWrite(f, "\n", 1); sceIoClose(f); }
}

// ---- muestreador de perfiles (save/sampler): un hilo en otro nucleo mira ~2000 veces por segundo que script
// (Game::curCode) y que funcion integrada (gSampFn) esta ejecutando el hilo principal y en que fase del frame.
// No cronometra nada en el hilo principal: los porcentajes son fieles al coste real en la Vita.
namespace vs { extern volatile int gSampFn; }
static volatile int gSampPhase = 0;            // 0 esperando (vsync/entrada), 1 logica, 2 dibujo
static vs::Game* gSampGame = nullptr;
static volatile bool gSampOn = false;
static std::vector<uint32_t> gSampCode, gSampFnH;   // tamano fijo antes de arrancar el hilo
static uint32_t gSampPh[3] = {0, 0, 0}, gSampGml[3] = {0, 0, 0};
static int samplerThread(SceSize, void*) {
    for (;;) {
        sceKernelDelayThread(500);
        if (!gSampOn) continue;
        int ph = gSampPhase; if (ph < 0 || ph > 2) ph = 0;
        gSampPh[ph]++;
        if (ph == 0) continue;
        int c = gSampGame->curCode, f = vs::gSampFn;
        if (c >= 0 && c < (int)gSampCode.size()) gSampCode[c]++;
        if (f >= 0 && f < (int)gSampFnH.size()) gSampFnH[f]++; else gSampGml[ph]++;
    }
    return 0;
}
static void samplerDump(vs::Game& g) {
    uint32_t tot = gSampPh[0] + gSampPh[1] + gSampPh[2];
    if (!tot) return;
    vs::logf("[muestreo] %u muestras (sala %s, %d instancias): espera %.1f%%, logica %.1f%%, dibujo %.1f%% | codigo GML propio %.1f%%\n",
             tot, g.d.rooms[g.room].name.c_str(), (int)g.insts.size(), 100.0 * gSampPh[0] / tot, 100.0 * gSampPh[1] / tot,
             100.0 * gSampPh[2] / tot, 100.0 * (gSampGml[1] + gSampGml[2]) / tot);
    std::vector<std::pair<uint32_t, int>> v;
    for (size_t k = 0; k < gSampCode.size(); k++) if (gSampCode[k]) v.push_back({gSampCode[k], (int)k});
    std::sort(v.rbegin(), v.rend());
    for (size_t k = 0; k < v.size() && k < 30; k++)
        vs::logf("[muestreo] script %5.1f%%  %s\n", 100.0 * v[k].first / tot, g.d.codes[v[k].second].name.c_str());
    v.clear();
    for (size_t k = 0; k < gSampFnH.size(); k++) if (gSampFnH[k]) v.push_back({gSampFnH[k], (int)k});
    std::sort(v.rbegin(), v.rend());
    for (size_t k = 0; k < v.size() && k < 20; k++)
        vs::logf("[muestreo] funcion %5.1f%%  %s\n", 100.0 * v[k].first / tot, g.d.functions[v[k].second].c_str());
    std::fill(gSampCode.begin(), gSampCode.end(), 0); std::fill(gSampFnH.begin(), gSampFnH.end(), 0);
    for (int k = 0; k < 3; k++) { gSampPh[k] = 0; gSampGml[k] = 0; }
}

static bool gDrawSkip = true;
long gDiscardFrames = 0;   // frames sin dibujar (se informan en [perf])

int main(int, char**) {
    { SceUID f = sceIoOpen("ux0:data/valdis/save/boot.txt", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777); if (f >= 0) sceIoClose(f); }
    bootMark("main");
    // Relojes: 500 MHz CPU / 222 MHz GPU. Sin overclock (PSVshell) el sistema se queda en 444 como maximo.
    if (scePowerSetArmClockFrequency(500) < 0) scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    // DMR: ampliar el presupuesto de memoria MAIN (hasta 381 MiB) ANTES de iniciar vitaGL, que toma la RAM libre
    // para texturas. Sin el plugin la llamada falla y se sigue con la memoria normal.
    int grownMB = 0;
    for (int mb = 128; mb >= 16; mb /= 2) if (sceAppMgrGrowMemory((unsigned)mb << 20, 1) >= 0) { grownMB = mb; break; }
    // hilo principal (logica GML + dibujo) fijo en el nucleo 0
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
    bootMark(grownMB ? "dmr ok" : "dmr no");
    vglInitExtended(0, 960, 544, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
    bootMark("vgl ok");
    vglWaitVblankStart(GL_TRUE);   // tope fijo de 60 fps (vsync)
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

    std::string root = kRoot, dataDir = root + "/vsdata", gameDir = root + "/game", saveDir = root + "/save";
    sceIoMkdir(root.c_str(), 0777); sceIoMkdir(saveDir.c_str(), 0777);

    static Game game;
    std::string err;
    {
        // datos aun sin preparar (o conversion interrumpida): se generan aqui desde data.win de Steam
        SceIoStat cst;
        bool haveVsd = sceIoGetstat((dataDir + "/game.vsd").c_str(), &cst) >= 0;
        bool partial = sceIoGetstat((dataDir + "/convirtiendo").c_str(), &cst) >= 0;
        if (!haveVsd || partial) {
            if (sceIoGetstat((gameDir + "/data.win").c_str(), &cst) < 0) {
                showError("No se encuentran los datos del juego.\n\nCopia el CONTENIDO de tu carpeta de Steam de Valdis Story (data.win, data_1, data_2, data_5 y los music_*.ogg) a\nux0:data/valdis/game/\n\nLa primera vez que abras el juego se preparan solos.");
                sceKernelExitProcess(0); return 0;
            }
            std::string cerr;
            if (!runConversion(root, gameDir, cerr)) {
                showError("Error al preparar los datos del juego:\n" + cerr + "\n\nComprueba que hay espacio libre (se necesitan unos 300 MB) y que data.win es el de la version de Steam.");
                bgio::flush(); sceKernelExitProcess(0); return 0;
            }
        }
        // datos convertidos con una version anterior: recomprimir las texturas a VDX4 (una vez, ~1-2 min)
        if (!vs::vdx4Ready(dataDir)) {
            std::string uerr;
            if (!runJob("Optimizando las texturas para que carguen mas rapido.\nSolo se hace una vez; tarda uno o dos minutos.\nNo cierres la aplicacion.",
                        [dataDir, gameDir](const vs::ConvertProgress& p, std::string& e) { return vs::upgradeVdx(dataDir, gameDir, p, e); }, uerr))
                logf("[vdx4] no se pudo recomprimir: %s (se siguen usando las texturas anteriores)\n", uerr.c_str());
        }
        GameData tmp;
        if (!tmp.load(dataDir + "/game.vsd", err)) {
            showError("No se encuentran los datos del juego.\n\nGenera vsdata en el PC con tools/build_vsdata.py a partir de tu copia de Steam y copialo a\nux0:data/valdis/vsdata/\n\n(" + err + ")");
            sceKernelExitProcess(0); return 0;
        }
        // texturas DXT (.vdx): ~190 MB todas; el resto de la memoria de vitaGL queda para buffers
        gfx::init(dataDir + "/tex", (size_t)140 * 1024 * 1024);
        for (size_t i = 0; i < tmp.textures.size(); i++) gfx::setPageSize((int)i, tmp.textures[i].w, tmp.textures[i].h);
        gWinW = tmp.winW; gWinH = tmp.winH;
    }
    SceIoStat st;
    if (sceIoGetstat((gameDir + "/data_5").c_str(), &st) < 0)
        showError("Faltan los archivos externos del juego.\n\nCopia data_1, data_2, data_5 y los music_*.ogg de tu carpeta de Steam a\nux0:data/valdis/game/");
    if (!game.init(dataDir, gameDir, saveDir, err)) { showError("Error al iniciar: " + err); bgio::flush(); sceKernelExitProcess(0); return 0; }
    logf("[sistema] CPU %d MHz, GPU %d MHz | DMR: %s | 4.o nucleo: %s | memoria vitaGL libre: VRAM %u MB, RAM %u MB, phycont %u MB, total %u MB\n",
         scePowerGetArmClockFrequency(), scePowerGetGpuClockFrequency(),
         grownMB ? (std::to_string(grownMB) + " MB extra").c_str() : "no (sin plugin, memoria normal)",
         vs::coreAvailable(SCE_KERNEL_CPU_MASK_SYSTEM) ? "si (CapUnlocker)" : "no",
         (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 20), (unsigned)(vglMemFree(VGL_MEM_RAM) >> 20),
         (unsigned)(vglMemFree(VGL_MEM_PHYCONT) >> 20), (unsigned)(vglMemFree(VGL_MEM_ALL) >> 20));

    bootMark("init ok");
    if (sceIoGetstat((saveDir + "/prof").c_str(), &st) >= 0) game.prof = true; // perfilador: crea el archivo save/prof
    if (sceIoGetstat((saveDir + "/profphase").c_str(), &st) >= 0) { game.prof = true; vs::gProfPhaseOnly = true; } // fases + scripts con AOT
    if (sceIoGetstat((saveDir + "/proffases").c_str(), &st) >= 0) { game.prof = true; vs::gProfPhaseOnly = true; vs::gProfLight = true; } // solo fases
    if (sceIoGetstat((saveDir + "/perfverbose").c_str(), &st) >= 0) game.perfVerbose = true; // una linea [perf] por segundo
    gAudioRun = true;
    // audio en el nucleo 1 (la mezcla no debe competir con la logica del nucleo 0)
    SceUID th = sceKernelCreateThread("vs_audio", audioThread, 0x10000100, 0x10000, 0, SCE_KERNEL_CPU_MASK_USER_1, nullptr);
    if (th >= 0) sceKernelStartThread(th, 0, nullptr);

    // Depuracion: save/warp.txt con lineas "SALA FRAMES DESTINO" = room_goto(DESTINO) tras FRAMES frames en SALA
    // (como --warp en PC; sirve para medir entradas a salas con vita-agent sin jugar hasta ellas)
    struct Warp { std::string from; int delay; std::string to; };
    std::vector<Warp> warps;
    if (FILE* wf = std::fopen((saveDir + "/warp.txt").c_str(), "r")) {
        char a[128], c[128]; int dl;
        while (std::fscanf(wf, "%127s %d %127s", a, &dl, c) == 3) warps.push_back({a, dl, c});
        std::fclose(wf);
        vs::logf("[warp] %d saltos de depuracion cargados de save/warp.txt\n", (int)warps.size());
    }
    int roomEnterFrame = 0, lastRoom = -1;
    vs::debugTourInit(game, saveDir);   // save/tour.txt, stress.txt, godmode (pruebas de rendimiento)
    {   // save/nodrawskip: desactiva el dibujo a 30 fps (para medir el coste real de cada frame)
        SceIoStat ns; gDrawSkip = sceIoGetstat((saveDir + "/nodrawskip").c_str(), &ns) < 0;
        vs::logf("[sistema] dibujo a 30 fps cuando la logica no llega: %s\n", gDrawSkip ? "activo" : "desactivado (save/nodrawskip)");
    }
    bool discardNext = false;
    uint64_t sampDumpAt = 0;
    {
        SceIoStat sst;
        if (sceIoGetstat((saveDir + "/sampler").c_str(), &sst) >= 0) {
            gSampGame = &game; gSampCode.assign(game.d.codes.size(), 0); gSampFnH.assign(game.d.functions.size(), 0);
            SceUID st = sceKernelCreateThread("vs_sampler", samplerThread, 0x10000100, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_2, nullptr);
            if (st >= 0 && sceKernelStartThread(st, 0, nullptr) >= 0) { gSampOn = true; sampDumpAt = sceKernelGetProcessTimeWide() + 10000000; vs::logf("[muestreo] activo (nucleo 2, cada 0,5 ms, informe cada 10 s)\n"); }
        }
    }
    uint64_t prevFrameT = sceKernelGetProcessTimeWide();

    game.in.gpConnected = true;
    uint64_t fpsT = sceKernelGetProcessTimeWide(); int fpsFrames = 0;
    while (!game.endRequested) {
        if (!warps.empty()) {
            if (game.room != lastRoom) { lastRoom = game.room; roomEnterFrame = game.frame; }
            for (auto& w : warps)
                if (w.delay >= 0 && game.d.rooms[game.room].name == w.from && game.frame - roomEnterFrame == w.delay) {
                    for (size_t r = 0; r < game.d.rooms.size(); r++) if (game.d.rooms[r].name == w.to) game.pendingRoom = (int)r;
                    w.delay = -1;
                }
        }
        SceCtrlData pad{}; sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t b = pad.buttons;
        bool* g = game.in.gpBtn;
        g[0] = b & SCE_CTRL_CROSS; g[1] = b & SCE_CTRL_CIRCLE; g[2] = b & SCE_CTRL_SQUARE; g[3] = b & SCE_CTRL_TRIANGLE;
        g[4] = false; g[5] = false; g[6] = b & SCE_CTRL_LTRIGGER; g[7] = b & SCE_CTRL_RTRIGGER;
        g[8] = b & SCE_CTRL_SELECT; g[9] = b & SCE_CTRL_START; g[10] = false; g[11] = false;
        g[12] = b & SCE_CTRL_UP; g[13] = b & SCE_CTRL_DOWN; g[14] = b & SCE_CTRL_LEFT; g[15] = b & SCE_CTRL_RIGHT;
        SceTouchData touch{};
        if (sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch, 1) > 0)
            for (int t = 0; t < (int)touch.reportNum; t++) { if (touch.report[t].x < 960) g[4] = true; else g[5] = true; }
        auto axis = [](uint8_t v) { float f = (v - 128) / 127.f; return std::fabs(f) < 0.15f ? 0.f : std::max(-1.f, std::min(1.f, f)); };
        game.in.gpAxis[0] = axis(pad.lx); game.in.gpAxis[1] = axis(pad.ly);
        game.in.gpAxis[2] = axis(pad.rx); game.in.gpAxis[3] = axis(pad.ry);

        // Dibujo a 30 fps cuando la logica no llega (regla del proyecto: minimo 30 fps en las salas exigentes): si el
        // frame anterior se paso del presupuesto, este ejecuta logica y eventos Draw pero no dibuja ni espera a la
        // pantalla. Nunca dos seguidos. Asi el juego sigue a 60 pasos por segundo en vez de ir a camara lenta.
        bool discard = gDrawSkip && discardNext;
        gfx::setDiscard(discard);
        uint32_t cb = 0;   // menu de trucos: SELECT lo abre
        if (b & SCE_CTRL_UP) cb |= 1; if (b & SCE_CTRL_DOWN) cb |= 2; if (b & SCE_CTRL_LEFT) cb |= 4; if (b & SCE_CTRL_RIGHT) cb |= 8;
        if (b & SCE_CTRL_CROSS) cb |= 16; if (b & SCE_CTRL_CIRCLE) cb |= 32; if (b & SCE_CTRL_SELECT) cb |= 64;
        if (b & SCE_CTRL_LTRIGGER) cb |= 128; if (b & SCE_CTRL_RTRIGGER) cb |= 256;
        bool cheatPause = vs::cheatsInput(game, cb);
        uint64_t t0 = sceKernelGetProcessTimeWide();
        if (!cheatPause) {
            game.inputBeginFrame();
            vs::debugTourBeforeStep(game);
            gSampPhase = 1;
            game.step();
        }
        uint64_t t1 = sceKernelGetProcessTimeWide();
        gfx::setScreen(960, 544, gWinW, gWinH);
        gSampPhase = 2;
        // draw_enable_drawevent(false) (opcion "GFX : Low" del juego: lo alterna un frame si y otro no): en GMS1 ese
        // frame no se dibuja nada ni se refresca la pantalla, se queda la imagen anterior. Antes se dibujaba la sala
        // con los sprites por defecto en lugar de los eventos Draw -> parpadeos y graficos rotos.
        bool noDraw = !game.drawEventsEnabled;
        if (!noDraw) game.draw();
        gSampPhase = 0;
        uint64_t t2 = sceKernelGetProcessTimeWide();
        if (sampDumpAt && t2 >= sampDumpAt) { samplerDump(game); sampDumpAt = t2 + 10000000; }
        {   // memoria cada minuto (sesiones largas: fugas de RAM o VRAM)
            static uint64_t memAt = 0; static int memMin = 0;
            if (!memAt) memAt = t2 + 60000000;
            if (t2 >= memAt) {
                memAt = t2 + 60000000; memMin++;
                struct mallinfo mi = mallinfo();
                vs::logf("[memoria] min %d | heap usado %u KB (reservado %u KB) | vitaGL libre: VRAM %u KB, RAM %u KB, phycont %u KB | instancias %d | sala %s\n",
                         memMin, (unsigned)(mi.uordblks >> 10), (unsigned)(mi.arena >> 10), (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 10),
                         (unsigned)(vglMemFree(VGL_MEM_RAM) >> 10), (unsigned)(vglMemFree(VGL_MEM_PHYCONT) >> 10), (int)game.insts.size(),
                         game.d.rooms[game.room].name.c_str());
            }
        }
        if (noDraw) {   // sin dibujo ni vsync: esperar al resto del frame para que el juego siga a 60 pasos por segundo
            uint64_t tn = sceKernelGetProcessTimeWide();
            if (tn - t0 < 16600) sceKernelDelayThread((SceUInt)(16600 - (tn - t0)));
        } else if (!discard) vglSwapBuffers(GL_FALSE); else gDiscardFrames++;
        discardNext = !noDraw && !discard && (t2 - t0) > 15500;
        gfx::perf().swapMs += (sceKernelGetProcessTimeWide() - t2) / 1000.0;   // espera a la GPU / vsync
        game.perfFrame((t1 - t0) / 1000.0, (t2 - t1) / 1000.0);
        { uint64_t tf = sceKernelGetProcessTimeWide(); vs::debugTourAfterFrame(game, (t1 - t0) / 1000.0, (t2 - t1) / 1000.0, (tf - prevFrameT) / 1000.0); prevFrameT = tf; }
        if (game.prof) { game.profPhase[11] += (double)(t2 - t1); if (game.profFrames >= 600) game.profDump(); }

        fpsFrames++;
        uint64_t now = sceKernelGetProcessTimeWide();
        if (now - fpsT >= 1000000) { game.fps = fpsFrames; fpsFrames = 0; fpsT = now; }
    }
    gAudioRun = false;
    sceKernelWaitThreadEnd(th, nullptr, nullptr);
    bgio::flush();
    sceKernelExitProcess(0);
    return 0;
}
