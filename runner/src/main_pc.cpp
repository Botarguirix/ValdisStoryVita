// Plataforma PC (pruebas): SDL2 + OpenGL 2.1 compat. Ventana 960x544 (= Vita) por defecto.
#include "runtime.h"
#include "bgio.h"
#ifdef VS_OPSTATS
namespace vs { void dumpOpStats(); }
#endif
namespace vs { void geomTouchObj(int obj); extern uint64_t gGeomEpoch; }
#include "gfx.h"
#include "debugtour.h"
#include "audio.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstring>
#include <vector>
#include <utility>
#include <string>
#include <sys/stat.h>

namespace vs { extern int gWinW, gWinH; extern std::vector<std::string> gLangDirs; }

// Fuente alternativa del menu de MODS en PC (solo para probar): una TTF de Windows con stb_truetype. En la Vita se usa
// la fuente del sistema de la consola (main_vita.cpp); ningun archivo de fuente va en el VPK ni en el repositorio.
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"
namespace vs {
extern bool (*gFontOpen)(float px, float& ascent, float& lineH);
extern bool (*gFontGlyph)(uint32_t cp, std::vector<uint8_t>& alpha, int& w, int& h, int& xoff, int& yoff, float& adv);
}
static std::vector<unsigned char> gTtf; static stbtt_fontinfo gTtfInfo; static float gTtfScale = 1;
static bool pcFontOpen(float px, float& ascent, float& lineH) {
    for (const char* path : {"C:/Windows/Fonts/arialbd.ttf", "C:/Windows/Fonts/arial.ttf"}) {
        FILE* f = std::fopen(path, "rb"); if (!f) continue;
        std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        gTtf.resize(n); size_t rd = std::fread(gTtf.data(), 1, n, f); std::fclose(f);
        if ((long)rd != n || !stbtt_InitFont(&gTtfInfo, gTtf.data(), 0)) continue;
        int a, d, g; stbtt_GetFontVMetrics(&gTtfInfo, &a, &d, &g);
        gTtfScale = stbtt_ScaleForPixelHeight(&gTtfInfo, px);
        ascent = a * gTtfScale; lineH = (a - d) * gTtfScale;
        return true;
    }
    return false;
}
static bool pcFontGlyph(uint32_t cp, std::vector<uint8_t>& alpha, int& w, int& h, int& xoff, int& yoff, float& adv) {
    int gi = stbtt_FindGlyphIndex(&gTtfInfo, (int)cp); if (gi == 0 && cp != 32) return false;
    int aw, lsb; stbtt_GetGlyphHMetrics(&gTtfInfo, gi, &aw, &lsb); adv = aw * gTtfScale;
    int x0, y0, x1, y1; stbtt_GetGlyphBitmapBox(&gTtfInfo, gi, gTtfScale, gTtfScale, &x0, &y0, &x1, &y1);
    w = std::max(0, x1 - x0); h = std::max(0, y1 - y0); xoff = x0; yoff = y0;
    alpha.assign((size_t)w * h, 0);
    if (w > 0 && h > 0) stbtt_MakeGlyphBitmap(&gTtfInfo, alpha.data(), w, h, w, gTtfScale, gTtfScale, gi);
    return true;
}
using namespace vs;

static int mapKey(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) return 'A' + (k - SDLK_a);
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + (k - SDLK_0);
    switch (k) {
        case SDLK_LEFT: return 37; case SDLK_UP: return 38; case SDLK_RIGHT: return 39; case SDLK_DOWN: return 40;
        case SDLK_ESCAPE: return 27; case SDLK_RETURN: return 13; case SDLK_SPACE: return 32; case SDLK_BACKSPACE: return 8;
        case SDLK_TAB: return 9; case SDLK_LSHIFT: case SDLK_RSHIFT: return 16; case SDLK_LCTRL: case SDLK_RCTRL: return 17;
        case SDLK_LALT: case SDLK_RALT: return 18;
        default: break;
    }
    if (k >= SDLK_F1 && k <= SDLK_F12) return 112 + (k - SDLK_F1);
    return -1;
}

static void audioCb(void*, Uint8* stream, int len) { audio::mix((int16_t*)stream, len / 4); }

static void saveBmp(const std::string& path, int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, px.data());
    int stride = (w * 3 + 3) & ~3;
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return;
    uint32_t sz = 54 + stride * h;
    uint8_t hd[54] = {'B', 'M'};
    auto put = [&](int o, uint32_t v) { hd[o] = v; hd[o + 1] = v >> 8; hd[o + 2] = v >> 16; hd[o + 3] = v >> 24; };
    put(2, sz); put(10, 54); put(14, 40); put(18, w); put(22, h); hd[26] = 1; hd[28] = 24; put(34, stride * h);
    std::fwrite(hd, 1, 54, f);
    std::vector<uint8_t> row(stride, 0);
    for (int y = 0; y < h; y++) { std::memcpy(row.data(), &px[(size_t)y * w * 3], w * 3); std::fwrite(row.data(), 1, stride, f); }
    std::fclose(f);
}

// ---- perfilador de muestreo (solo PC/Windows): cada ~1 ms congela el hilo principal y anota su RIP.
// Las direcciones (relativas a la base del exe) se vuelcan a un archivo para resolverlas con addr2line.
#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#include <thread>
#include <atomic>
#include <map>
static std::atomic<bool> gSampOn{false}, gSampStop{false};
static std::map<uint64_t, uint32_t> gSamples;
static std::thread* gSampThread = nullptr;
static void startSampler() {
    HANDLE main; DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main, 0, FALSE, DUPLICATE_SAME_ACCESS);
    uint64_t base = (uint64_t)GetModuleHandle(nullptr);
    timeBeginPeriod(1);
    gSampThread = new std::thread([main, base]() {
        while (!gSampStop) {
            Sleep(1);
            if (!gSampOn) continue;
            if (SuspendThread(main) == (DWORD)-1) continue;
            CONTEXT c; c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(main, &c)) gSamples[c.Rip - base]++;
            ResumeThread(main);
        }
    });
}
static void stopSampler(const char* path) {
    gSampStop = true; if (gSampThread) gSampThread->join();
    FILE* f = std::fopen(path, "w");
    for (auto& [a, n] : gSamples) std::fprintf(f, "%llx %u\n", (unsigned long long)a, n);
    std::fclose(f);
}
#endif

int main(int argc, char** argv) {
    std::string dataDir = argc > 1 ? argv[1] : "build/vsdata";
    std::string gameDir = argc > 2 ? argv[2] : "C:/Program Files (x86)/Steam/steamapps/common/ValdisStoryAbyssalCity";
    std::string saveDir = argc > 3 ? argv[3] : "build/save";
    int detSeed = -1, hashEvery = 0; bool noAot = false; const char* samplePath = nullptr;
    int benchFrom = -1; double benchStep = 0, benchDraw = 0; long benchN = 0;
    int discardEvery = 0;
    int maxFrames = -1; int scale = 1; bool turbo = false, prof = false, profCode = false; int budgetMB = 1024;
    std::vector<std::pair<int, std::string>> shots;
    std::vector<std::pair<int, int>> keyEvents; // frame, +key / -key
    struct Warp { std::string from; int delay; std::string to; };
    std::vector<Warp> warps;
    struct SetPos { int frame, x, y; };
    std::vector<SetPos> setpos;
    std::vector<std::pair<std::string, std::pair<int, int>>> roomKeys; // room, (delay, key)
    int roomEnterFrame = 0, lastRoom = -1;
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--shot") && i + 2 < argc) { int fr = std::atoi(argv[i + 1]); shots.push_back({fr, argv[i + 2]}); i += 2; }
        else if (!std::strcmp(argv[i], "--key") && i + 2 < argc) {
            // --key FRAME KEYCODE : pulsa la tecla 6 frames
            int fr = std::atoi(argv[i + 1]), k = std::atoi(argv[i + 2]); keyEvents.push_back({fr, k}); keyEvents.push_back({fr + 6, -k}); i += 2;
        }
        else if (!std::strcmp(argv[i], "--onroom") && i + 3 < argc) {
            // --onroom ROOM DELAY KEY : pulsa KEY cuando lleve DELAY frames en ROOM (se resuelve en el bucle)
            roomKeys.push_back({argv[i + 1], {std::atoi(argv[i + 2]), std::atoi(argv[i + 3])}}); i += 3;
        }
        else if (!std::strcmp(argv[i], "--hold") && i + 3 < argc) {
            // --hold FRAME KEYCODE DURACION
            int fr = std::atoi(argv[i + 1]), k = std::atoi(argv[i + 2]), du = std::atoi(argv[i + 3]);
            keyEvents.push_back({fr, k}); keyEvents.push_back({fr + du, -k}); i += 3;
        }
        else if (!std::strcmp(argv[i], "--warp") && i + 3 < argc) {
            // --warp ROOM DELAY DESTINO : room_goto(DESTINO) al llevar DELAY frames en ROOM (depuracion)
            warps.push_back({argv[i + 1], std::atoi(argv[i + 2]), argv[i + 3]}); i += 3;
        }
        else if (!std::strcmp(argv[i], "--setpos") && i + 3 < argc) {
            // --setpos FRAME X Y : mueve a player_wyatt (depuracion)
            setpos.push_back({std::atoi(argv[i + 1]), std::atoi(argv[i + 2]), std::atoi(argv[i + 3])}); i += 3;
        }
        else if (!std::strcmp(argv[i], "--det") && i + 1 < argc) detSeed = std::atoi(argv[++i]);           // determinista
        else if (!std::strcmp(argv[i], "--hash") && i + 1 < argc) hashEvery = std::atoi(argv[++i]);        // huella cada N frames
        else if (!std::strcmp(argv[i], "--noaot")) noAot = true;
        else if (!std::strcmp(argv[i], "--sample") && i + 1 < argc) samplePath = argv[++i];   // perfilador de muestreo
        else if (!std::strcmp(argv[i], "--bench") && i + 1 < argc) benchFrom = std::atoi(argv[++i]); // media de ms desde el frame N
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--turbo")) turbo = true;
        else if (!std::strcmp(argv[i], "--discard-every") && i + 1 < argc) discardEvery = std::atoi(argv[++i]);   // prueba: 1 de cada N frames sin dibujar
        else if (!std::strcmp(argv[i], "--prof")) prof = true;
        else if (!std::strcmp(argv[i], "--profcode")) profCode = true;                       // scripts/builtins mas caros                               // [perf] cada segundo
        else if (!std::strcmp(argv[i], "--budget") && i + 1 < argc) budgetMB = std::atoi(argv[++i]); // MB de texturas (Vita: 140)
    }
    mkdir(saveDir.c_str());

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { std::printf("SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int pw = 960 * scale, ph = 544 * scale;
    SDL_Window* win = SDL_CreateWindow("Valdis Story Vita (PC)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, pw, ph, SDL_WINDOW_OPENGL);
    SDL_GLContext ctx = win ? SDL_GL_CreateContext(win) : nullptr;
    if (!ctx) { std::printf("GL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetSwapInterval(turbo ? 0 : 1);

    SDL_AudioSpec want{}, have{};
    want.freq = audio::kRate; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audioCb;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev) SDL_PauseAudioDevice(dev, 0);

    static Game game;
    gfx::init(dataDir + "/tex", (size_t)budgetMB * 1024 * 1024);
    std::string err;
    // tamanos de pagina antes de cargar la primera room
    {
        GameData tmp; if (!tmp.load(dataDir + "/game.vsd", err)) { std::printf("error: %s\n", err.c_str()); return 1; }
        for (size_t i = 0; i < tmp.textures.size(); i++) gfx::setPageSize((int)i, tmp.textures[i].w, tmp.textures[i].h);
        gWinW = tmp.winW; gWinH = tmp.winH;
    }
    if (detSeed >= 0) { game.det = true; game.detSeed = (uint32_t)detSeed; }
    vs::gLangDirs = {saveDir + "/../lang", "lang"};   // traduccion: junto a las partidas o en la carpeta lang del repo
    if (!game.init(dataDir, gameDir, saveDir, err)) { std::printf("error: %s\n", err.c_str()); return 1; }
    vs::gFontOpen = pcFontOpen; vs::gFontGlyph = pcFontGlyph;
    vs::debugTourInit(game, saveDir);   // save/tour.txt, stress.txt, godmode
    if (noAot) game.aotEnabled = false;

    game.perfVerbose = prof; game.prof = profCode;
    SDL_GameController* pad = nullptr;
    bool quit = false;
    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    double acc = 0; int fpsFrames = 0; Uint64 fpsT = last;
    while (!quit && !game.endRequested) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                int k = mapKey(e.key.keysym.sym);
                if (k >= 0) game.in.phys[k] = e.type == SDL_KEYDOWN;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F10) quit = true;
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
            else if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad) { SDL_GameControllerClose(pad); pad = nullptr; }
        }
        game.in.gpConnected = pad != nullptr || std::getenv("VS_FAKEPAD");   // VS_FAKEPAD: el juego cree que hay mando (iconos)
        if (pad) {
            static const SDL_GameControllerButton map[16] = {
                SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
                SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_INVALID,
                SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
                SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
            for (int b = 0; b < 16; b++) game.in.gpBtn[b] = map[b] != SDL_CONTROLLER_BUTTON_INVALID && SDL_GameControllerGetButton(pad, map[b]);
            game.in.gpBtn[6] = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
            game.in.gpBtn[7] = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
            game.in.gpAxis[0] = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.f;
            game.in.gpAxis[1] = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.f;
            game.in.gpAxis[2] = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.f;
            game.in.gpAxis[3] = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.f;
        }

        Uint64 now = SDL_GetPerformanceCounter();
        acc += (double)(now - last) / freq; last = now;
        double dt = 1.0 / std::max(1, game.roomSpeed);
        int steps = 0; double stepMs = 0;
        if (turbo) acc = dt;
        while (acc >= dt && steps < 4) {
            if (game.room != lastRoom) { lastRoom = game.room; roomEnterFrame = game.frame; }
            for (auto& rk : roomKeys)
                if (rk.second.first >= 0 && game.d.rooms[game.room].name == rk.first && game.frame - roomEnterFrame == rk.second.first) {
                    keyEvents.push_back({game.frame, rk.second.second}); keyEvents.push_back({game.frame + 6, -rk.second.second});
                    rk.second.first = -1;
                }
            for (auto& w : warps)
                if (w.delay >= 0 && game.d.rooms[game.room].name == w.from && game.frame - roomEnterFrame == w.delay) {
                    for (size_t r = 0; r < game.d.rooms.size(); r++) if (game.d.rooms[r].name == w.to) game.pendingRoom = (int)r;
                    w.delay = -1;
                }
            for (auto& sp : setpos) if (sp.frame == game.frame)
                for (size_t o = 0; o < game.d.objects.size(); o++) if (game.d.objects[o].name == "player_wyatt")
                    for (auto* in : game.instancesOf((int)o)) { in->x = sp.x; in->y = sp.y; vs::geomTouchObj(in->obj); vs::gGeomEpoch++; }
            for (auto& [fr, k] : keyEvents) if (fr == game.frame) { if (k > 0) game.in.phys[k] = true; else game.in.phys[-k] = false; }
            {   // menus TRUCOS/MODS (o VS_CHEATSEQ="iter:bits,..." para pruebas con capturas; mismos bits que en la Vita)
                static long cheatIter = 0; static std::vector<std::pair<long, uint32_t>> seq; static bool seqRead = false;
                if (!seqRead) { seqRead = true; if (const char* e = std::getenv("VS_CHEATSEQ")) { long it; unsigned bt; int n;
                    while (std::sscanf(e, "%ld:%u%n", &it, &bt, &n) == 2) { seq.push_back({it, bt}); e += n; if (*e == ',') e++; else break; } } }
                const Uint8* ks = SDL_GetKeyboardState(nullptr);
                // F2 = TRUCOS (SELECT + L + R en la Vita), F3 = MODS (SELECT)
                uint32_t cb = (ks[SDL_SCANCODE_F2] ? 64u | 128u | 256u : 0u) | (ks[SDL_SCANCODE_F3] ? 64u : 0u);
                for (auto& [it, bt] : seq) if (it == cheatIter) cb |= bt;
                cheatIter++;
                if (vs::cheatsInput(game, cb)) { acc -= dt; steps++; continue; }
            }
            {   // VS_TESTRESTART=FRAME: llama a game_restart() en ese frame (prueba de "Salir" -> menu principal)
                static int rf = std::getenv("VS_TESTRESTART") ? std::atoi(std::getenv("VS_TESTRESTART")) : -1;
                if (rf > 0 && game.frame == rf) { game.restartRequested = true; rf = -1; }
            }
            game.inputBeginFrame();
            vs::debugTourBeforeStep(game);
            Uint64 s0 = SDL_GetPerformanceCounter();
            int roomBefore = game.room;
            game.step();
            if (game.prof && game.room != roomBefore) game.profDump(); // perfil aislado del frame de cambio de room
            if (hashEvery > 0 && game.frame % hashEvery == 0) std::printf("hash %d %s %016llx\n", game.frame, game.d.rooms[game.room].name.c_str(), (unsigned long long)game.stateHash());
            stepMs += (SDL_GetPerformanceCounter() - s0) * 1000.0 / freq;
            acc -= dt; steps++;
            if (maxFrames > 0 && game.frame >= maxFrames) quit = true;
        }
        if (steps == 0) { SDL_Delay(1); continue; }
        int dw, dh; SDL_GL_GetDrawableSize(win, &dw, &dh);
        gfx::setScreen(dw, dh, gWinW, gWinH);
        Uint64 d0 = SDL_GetPerformanceCounter();
        gfx::setDiscard(discardEvery > 0 && game.frame % discardEvery == 0);
        bool noDraw = !game.drawEventsEnabled;   // draw_enable_drawevent(false): ese frame no se dibuja (GMS1)
        if (!noDraw) game.draw();
        glFinish();
        double drawMs = (SDL_GetPerformanceCounter() - d0) * 1000.0 / freq;
        game.perfFrame(stepMs, drawMs);
        vs::debugTourAfterFrame(game, stepMs, drawMs, stepMs + drawMs);
        if (benchFrom >= 0 && game.frame >= benchFrom) { benchStep += stepMs; benchDraw += drawMs; benchN += steps; }
#ifdef _WIN32
        if (samplePath && benchFrom >= 0) { if (!gSampThread) startSampler(); gSampOn = game.frame >= benchFrom; }
#endif
        if (game.prof) { game.profPhase[11] += (SDL_GetPerformanceCounter() - d0) * 1e6 / freq; if (game.profFrames >= 600) game.profDump(); }
        static long renderN = 0; renderN++;   // --shot -N: captura en la imagen N (sirve con el juego en pausa: menu de trucos)
        for (auto& [fr, path] : shots) if ((fr > 0 && game.frame >= fr) || (fr < 0 && renderN >= -fr)) { saveBmp(path, dw, dh); std::printf("captura %s (frame %d, room %s)\n", path.c_str(), game.frame, game.d.rooms[game.room].name.c_str()); fr = 0; }
        if (!noDraw) SDL_GL_SwapWindow(win);
        fpsFrames++;
        if (now - fpsT > freq) {
            game.fps = fpsFrames; int np; size_t nb; gfx::stats(np, nb);
            char t[160]; std::snprintf(t, sizeof t, "Valdis Story Vita (PC) - %d fps - room %s - %zu inst - tex %d (%zu MB)",
                fpsFrames, game.d.rooms[game.room].name.c_str(), game.insts.size(), np, nb >> 20);
            SDL_SetWindowTitle(win, t);
            fpsFrames = 0; fpsT = now;
        }
    }
#ifdef VS_OPSTATS
    vs::dumpOpStats();
#endif
#ifdef _WIN32
    if (samplePath && gSampThread) stopSampler(samplePath);
#endif
    if (benchN) std::printf("bench: logica %.3f ms/frame, render %.3f ms/frame (%ld frames)\n", benchStep / benchN, benchDraw / benchN, benchN);
    std::printf("fin: frame=%d room=%s instrucciones=%llu\n", game.frame, game.d.rooms[game.room].name.c_str(), (unsigned long long)game.insCount);
    if (dev) SDL_CloseAudioDevice(dev);
    bgio::flush();
    SDL_Quit();
    return 0;
}
