// Modos de prueba de rendimiento (archivos en la carpeta save/; no afectan al juego normal si no existen):
//   tour.txt   recorrido: primera linea "FRAMES n" (opcional, 360 por defecto) y luego una sala por linea. Cuando se
//              llega a una sala de juego, salta a cada sala de la lista y se queda n frames. Al salir de cada una
//              escribe una linea [recorrido] con fps, peor frame, frames de mas de 16,7 ms e instancias.
//   stress.txt estres: "SALA CADA_FRAMES CUANTOS MAXIMO OBJ1 [OBJ2 ...]". En SALA crea CUANTOS enemigos cada
//              CADA_FRAMES frames junto al jugador (alternando los objetos) hasta MAXIMO, y por cada tanda escribe una
//              linea [estres] con el coste medido.
//   monkey     botones al azar en las salas de juego (prueba de estabilidad de cada personaje)
//   unlockchars  personajes desbloqueados en la seleccion (global.unlock_char = 1 en cada frame)
//   bossrush.txt  Boss Rush de prueba: "SALA FRAMES JEFE1 JEFE2 ...". Va a SALA y saca los jefes de uno en uno
//              (FRAMES frames cada uno, con monkey y godmode tiene combate real). Linea [jefe] por cada uno: coste,
//              frames perdidos, instancias, si sigue vivo y avisos nuevos.
//   godmode    el jugador no muere en combate: hp a 999999 en cada frame (un foso si puede matarlo; el
//              recorrido sigue en la sala siguiente). OJO: el autoguardado puede guardar ese hp: restaurar las partidas. (Reiniciar tambien el contador "death"
//              dejaba al jugador atrapado en un bucle de golpes que repetia un sonido sin parar.)
#include "debugtour.h"
#include "runtime.h"
#include "gfx.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>
#include <dirent.h>

namespace vs {
extern long gWarnCount;   // vm.cpp: avisos distintos emitidos

namespace {
struct Stats {
    int frames = 0, over = 0, maxInst = 0;
    double cpuSum = 0, cpuMax = 0, stepSum = 0, stepMax = 0, drawSum = 0, drawMax = 0, frameMax = 0, entryMax = 0, wallSum = 0;
    void add(double step, double draw, double frame, int inst, bool entry) {
        double cpu = step + draw;
        if (entry) { entryMax = std::max(entryMax, std::max(cpu, frame)); return; }
        frames++; wallSum += frame; cpuSum += cpu; stepSum += step; drawSum += draw;
        cpuMax = std::max(cpuMax, cpu); stepMax = std::max(stepMax, step); drawMax = std::max(drawMax, draw);
        frameMax = std::max(frameMax, frame);
        if (frame > 17.5) over++;   // un frame de vsync son 16,7 ms; >17,5 = se perdio al menos un refresco
        maxInst = std::max(maxInst, inst);
    }
};
const int kEntryFrames = 20;   // los primeros frames de cada sala/tanda se cuentan aparte (carga)

bool gGod = false; bool gMonkey = false; bool gUnlockChars = false; int gVarUnlockChar = -1; int gVarHp = -1, gVarHpMax = -1, gVarDeath = -1, gObjPlayer = -1;
// recorrido
std::vector<std::string> gTour; int gTourFrames = 360; int gTourIdx = -1; int gTourAt = 0; Stats gTourSt; bool gTourOn = false;
// estres
struct StressCfg { std::string room; int every = 300, add = 2, max = 60; std::vector<int> objs; } gStress; bool gStressOn = false;
int gStressCount = 0, gStressAt = -1, gStressNext = 0; Stats gStressSt; uint32_t gRand = 12345;

bool isMenuRoom(const std::string& n) {
    static const char* menus[] = {"start_game", "status_room", "control_room", "store_room", "spell_room", "eqp_room", "profile_room",
                                  "lore_room", "char_room", "death_room", "after_life", "attract_room", "ship_room", "ending_room", "end_demo"};
    for (const char* m : menus) if (n == m) return true;
    return false;
}
int roomByName(Game& g, const std::string& n) { for (size_t r = 0; r < g.d.rooms.size(); r++) if (g.d.rooms[r].name == n) return (int)r; return -1; }
int objByName(Game& g, const std::string& n) { for (size_t o = 0; o < g.d.objects.size(); o++) if (g.d.objects[o].name == n) return (int)o; return -1; }
int varByName(Game& g, const char* n) { for (size_t v = 0; v < g.d.varNames.size(); v++) if (g.d.varNames[v] == n) return (int)v; return -1; }
bool fileExists(const std::string& p) { if (FILE* f = std::fopen(p.c_str(), "r")) { std::fclose(f); return true; } return false; }
// Archivos de prueba en save/: vale el nombre exacto ("godmode", "keys.txt") o con un sufijo "_loquesea" antes de la
// extension ("godmode_0815", "keys_0815.txt"). vita-agent no deja volver a crear un archivo en una ruta de la que antes
// movio otro (la subida dice "completa" pero el archivo no aparece): cada prueba sube los suyos con nombres nuevos.
std::vector<std::string> gSaveList; std::string gSaveListDir;
std::string testPath(const std::string& dir, const std::string& base) {
    if (fileExists(dir + "/" + base)) return dir + "/" + base;
    if (gSaveListDir != dir) {
        gSaveListDir = dir; gSaveList.clear();
        if (DIR* d = opendir(dir.c_str())) { while (dirent* e = readdir(d)) gSaveList.push_back(e->d_name); closedir(d); }
        std::sort(gSaveList.begin(), gSaveList.end());
    }
    size_t dot = base.rfind('.'); std::string stem = base.substr(0, dot), ext = dot == std::string::npos ? "" : base.substr(dot);
    for (auto it = gSaveList.rbegin(); it != gSaveList.rend(); ++it) {   // el mas reciente por nombre
        const std::string& n = *it;
        if (n.size() > stem.size() + 1 + ext.size() && n.compare(0, stem.size() + 1, stem + "_") == 0 &&
            n.compare(n.size() - ext.size(), ext.size(), ext) == 0) return dir + "/" + n;
    }
    return "";
}
bool testExists(const std::string& dir, const std::string& base) { return !testPath(dir, base).empty(); }
}  // namespace (reabierto abajo)
bool testFileExists(const std::string& dir, const std::string& base) { return testExists(dir, base); }
namespace {

void logStats(const char* tag, const std::string& what, const Stats& s) {
    if (s.frames == 0) { logf("%s %s: sin frames medidos\n", tag, what.c_str()); return; }
    double cpu = s.cpuSum / s.frames;
    const char* lim = s.over == 0 ? "[OK]" : (s.stepSum > s.drawSum ? "[CPU-bound]" : "[CPU-bound dibujo]");
    double steps = s.wallSum > 0 ? s.frames * 1000.0 / s.wallSum : 0;   // pasos de logica por segundo (60 = velocidad normal)
    logf("%s %s %s | %.0f pasos/s | %d frames, %d perdidos (%.1f%%) | cpu media %.1f ms, max %.1f | logica %.1f/%.1f | dibujo %.1f/%.1f | peor frame %.1f ms | entrada %.0f ms | instancias %d\n",
         tag, lim, what.c_str(), steps, s.frames, s.over, 100.0 * s.over / s.frames, cpu, s.cpuMax, s.stepSum / s.frames, s.stepMax,
         s.drawSum / s.frames, s.drawMax, s.frameMax, s.entryMax, s.maxInst);
}

// Monkey: botones al azar (moverse, saltar, atacar, magia, bloquear) para ejercitar el codigo de cada personaje en
// todas las salas. Nunca pausa ni mapa. Mando: botones 0 salto, 1 bloqueo, 2 ataque, 3 ataque 2, 7 magia, 14/15
// izquierda/derecha; teclado (PC): flechas, D, A, S, W.
struct Monkey { int dir = 0, dirLeft = 0; int hold[8] = {}; } gMk;
uint32_t mkRand() { gRand = gRand * 1103515245u + 12345u; return gRand >> 16; }
void monkeyStep(Game& g) {
    auto& in = g.in;
    if (gMk.dirLeft-- <= 0) { gMk.dir = (int)(mkRand() % 3) - 1; gMk.dirLeft = 30 + (int)(mkRand() % 90); }
    const int btn[5] = {0, 1, 2, 3, 7}; const int key[5] = {38, 65, 68, 87, 83};
    const int chance[5] = {40, 120, 8, 30, 90};   // 1 de cada N frames empieza a pulsarlo
    for (int k = 0; k < 5; k++) {
        if (gMk.hold[k] > 0) gMk.hold[k]--;
        else if (mkRand() % chance[k] == 0) gMk.hold[k] = 2 + (int)(mkRand() % (k == 1 ? 40 : 8));
    }
    // magia + bloqueo a la vez abre el menu de hechizos (general_control Step): nunca juntos
    if (gMk.hold[1] > 0 && gMk.hold[4] > 0) gMk.hold[4] = 0;
    auto setKey = [&](int kc, bool on) { in.down[kc] = on; in.pressed[kc] = on && !in.prev[kc]; in.released[kc] = !on && in.prev[kc]; };
    for (int k = 0; k < 5; k++) { in.gpBtn[btn[k]] = gMk.hold[k] > 0; setKey(key[k], gMk.hold[k] > 0); }
    in.gpBtn[14] = gMk.dir < 0; in.gpBtn[15] = gMk.dir > 0; in.gpAxis[0] = (float)gMk.dir;
    setKey(37, gMk.dir < 0); setKey(39, gMk.dir > 0);
}

// ---- Teclas programadas (save/keys.txt), lo mismo que --key / --onroom del PC pero en cualquier plataforma.
// Lineas: "at FRAME TECLA" o "onroom SALA RETRASO TECLA" (codigos de tecla de GMS: 68 = D, 39 = derecha, 13 = Enter).
// Cada linea se dispara una sola vez (onroom: RETRASO frames despues de entrar en SALA).
struct KeyEv { bool onroom = false; int room = -1, at = 0, key = 0; bool done = false; };
std::vector<KeyEv> gKeys; int gKeyHold[256] = {}; int gKeysRoomAt = -1, gKeysLastRoom = -1;
void keysInit(Game& g, const std::string& saveDir) {
    FILE* f = std::fopen(testPath(saveDir, "keys.txt").c_str(), "r");
    if (!f) return;
    char w[128], room[128];
    while (std::fscanf(f, "%127s", w) == 1) {
        KeyEv e;
        if (!std::strcmp(w, "at") && std::fscanf(f, "%d %d", &e.at, &e.key) == 2) gKeys.push_back(e);
        else if (!std::strcmp(w, "onroom") && std::fscanf(f, "%127s %d %d", room, &e.at, &e.key) == 3) {
            e.onroom = true; e.room = roomByName(g, room);
            if (e.room >= 0) gKeys.push_back(e); else logf("[teclas] sala desconocida: %s\n", room);
        }
    }
    std::fclose(f);
    logf("[teclas] %d pulsaciones programadas\n", (int)gKeys.size());
}
void keysStep(Game& g) {
    if (gKeys.empty()) return;
    if (g.room != gKeysLastRoom) { gKeysLastRoom = g.room; gKeysRoomAt = g.frame; }
    for (auto& e : gKeys) {
        if (e.done) continue;
        bool fire = e.onroom ? (g.room == e.room && g.frame - gKeysRoomAt >= e.at) : g.frame >= e.at;
        if (fire) { e.done = true; if (e.key > 0 && e.key < 256) gKeyHold[e.key] = 6; logf("[teclas] frame %d tecla %d\n", g.frame, e.key); }
    }
    // como una tecla fisica: entra en el siguiente inputBeginFrame (tambien vk_anykey, que pide el titulo)
    for (int k = 1; k < 256; k++) {
        if (gKeyHold[k] > 0) { g.in.phys[k] = true; if (--gKeyHold[k] == 0) gKeyHold[k] = -1; }
        else if (gKeyHold[k] < 0) { g.in.phys[k] = false; gKeyHold[k] = 0; }
    }
}

// ---- Boss Rush de prueba
// bossrush.txt, dos formatos:
//   "SALA FRAMES JEFE1 JEFE2 ..."          todos los jefes en la misma sala (la Arena del pueblo, 19-11)
//   "salas FRAMES" + lineas "JEFE SALA"     cada jefe en su propia sala de combate (decision del usuario, 20-11)
struct BossRush { int frames = 1800; std::vector<int> objs, rooms; int idx = -1, at = 0, enteredAt = -1, lastRoom = -1; long warn0 = 0; Stats st; } gBR;
bool gBROn = false;
void bossRushInit(Game& g, const std::string& saveDir) {
    FILE* f = std::fopen(testPath(saveDir, "bossrush.txt").c_str(), "r");
    if (!f) return;
    char room[128], ob[128];
    if (std::fscanf(f, "%127s %d", room, &gBR.frames) == 2) {
        if (!std::strcmp(room, "salas")) {
            while (std::fscanf(f, "%127s %127s", ob, room) == 2) {
                int o = objByName(g, ob), r = roomByName(g, room);
                if (o >= 0 && r >= 0) { gBR.objs.push_back(o); gBR.rooms.push_back(r); }
                else logf("[jefe] desconocido: %s en %s\n", ob, room);
            }
        } else {
            int r = roomByName(g, room);
            while (std::fscanf(f, "%127s", ob) == 1) {
                int o = objByName(g, ob);
                if (o >= 0 && r >= 0) { gBR.objs.push_back(o); gBR.rooms.push_back(r); } else logf("[jefe] objeto desconocido: %s\n", ob);
            }
        }
    }
    std::fclose(f);
    gBROn = !gBR.objs.empty();
    if (gBROn) logf("[jefe] Boss Rush de prueba: %d jefes, %d frames cada uno\n", (int)gBR.objs.size(), gBR.frames);
}
void bossRushStep(Game& g) {
    if (!gBROn || g.pendingRoom >= 0) return;
    const std::string& rn = g.d.rooms[g.room].name;
    if (g.room != gBR.lastRoom) { gBR.lastRoom = g.room; gBR.enteredAt = g.frame; }
    if (gBR.idx < 0) {
        if (isMenuRoom(rn) || g.frame < 60) return;
        gBR.idx = 0; gBR.at = 0;
    }
    int target = gBR.rooms[gBR.idx], o = gBR.objs[gBR.idx];
    if (g.room != target) {   // a la sala del jefe (o de vuelta, si una puerta o una muerte lo saca)
        if (gBR.at != 0) logf("[jefe] %s: el jugador salio de %s, se repite\n", g.d.objects[o].name.c_str(), g.d.rooms[target].name.c_str());
        gBR.at = 0; g.pendingRoom = target; return;
    }
    if (gBR.at == 0) {
        if (g.frame - gBR.enteredAt < 90) return;   // que la sala termine de cargar y de colocar a todos
        Instance* pl = gObjPlayer >= 0 ? g.firstOf(gObjPlayer) : nullptr;
        if (pl) {   // como el salto de sala de los trucos: dentro de los limites de la sala
            pl->x = std::max(48.0, std::min(pl->x, (double)g.roomW - 48));
            pl->y = std::max(48.0, std::min(pl->y, (double)g.roomH - 96));
        }
        double px = pl ? pl->x : g.roomW / 2.0, py = pl ? pl->y : g.roomH / 2.0;
        gBR.warn0 = gWarnCount;
        logf("[jefe] sale %s en %s\n", g.d.objects[o].name.c_str(), rn.c_str());
        double bx = px + 220 < g.roomW - 64 ? px + 220 : px - 220;
        bool placed = false; for (Instance* i : g.instancesOf(o)) if (i->alive) placed = true;
        if (placed) logf("[jefe] %s ya esta colocado en la sala: no se crea otro\n", g.d.objects[o].name.c_str());
        else g.createInstance(bx, py - 60, o, true);
        gBR.at = g.frame; gBR.st = Stats{};
        return;
    }
    if (g.frame - gBR.at >= gBR.frames) {   // cerrar el jefe actual
        int alive = 0;
        for (Instance* i : g.instancesOf(o)) if (i->alive) alive++;
        logStats("[jefe]", std::to_string(gBR.idx + 1) + "/" + std::to_string(gBR.objs.size()) + " " + g.d.objects[o].name + " en " + rn +
                 (alive ? " (sigue vivo)" : " (derrotado)") + " avisos nuevos " + std::to_string(gWarnCount - gBR.warn0), gBR.st);
        for (Instance* i : g.instancesOf(o)) if (i->alive) g.destroyInstance(i, false);
        gBR.idx++; gBR.at = 0;
        if (gBR.idx >= (int)gBR.objs.size()) { logf("[jefe] Boss Rush terminado\n"); gBROn = false; }
    }
}
} // namespace

std::string gSaveDir; double gLastStep = 0, gLastDraw = 0;
void cheatsInit(Game& g);
void bossRushModeInit(Game& g);
bool bossRushModeInput(Game& g, uint32_t b, bool menuOpen);
void bossRushModeStep(Game& g);
bool bossRushModeOverlay(Game& g);
void debugTourInit(Game& g, const std::string& saveDir) {
    gSaveDir = saveDir;
    gGod = testExists(saveDir, "godmode");
    gMonkey = testExists(saveDir, "monkey");
    bossRushInit(g, saveDir);
    keysInit(g, saveDir);
    { extern int gDrawDebug; if (testExists(saveDir, "drawdebug")) gDrawDebug = 400; }
    if (testExists(saveDir, "drawdebug")) gfx::gUploadDebug = true;
    gUnlockChars = testExists(saveDir, "unlockchars"); gVarUnlockChar = varByName(g, "unlock_char");
    if (gUnlockChars) logf("[prueba] personajes desbloqueados (global.unlock_char = 1, variable %d)\n", gVarUnlockChar);
    gVarHp = varByName(g, "hp"); gVarHpMax = varByName(g, "hp_max"); gVarDeath = varByName(g, "death"); gObjPlayer = objByName(g, "player_wyatt");
    cheatsInit(g);
    if (gGod) logf("[prueba] modo sin morir activo (hp=%d hp_max=%d jugador=%d)\n", gVarHp, gVarHpMax, gObjPlayer);
    if (FILE* f = std::fopen(testPath(saveDir, "tour.txt").c_str(), "r")) {
        char w[128];
        while (std::fscanf(f, "%127s", w) == 1) {
            if (!std::strcmp(w, "FRAMES")) { int n; if (std::fscanf(f, "%d", &n) == 1) gTourFrames = std::max(60, n); continue; }
            if (roomByName(g, w) >= 0) gTour.push_back(w); else logf("[recorrido] sala desconocida: %s\n", w);
        }
        std::fclose(f);
        gTourOn = !gTour.empty();
        if (gTourOn) logf("[recorrido] %d salas, %d frames cada una (%.0f min)\n", (int)gTour.size(), gTourFrames, gTour.size() * gTourFrames / 3600.0);
    }
    if (FILE* f = std::fopen(testPath(saveDir, "stress.txt").c_str(), "r")) {
        char room[128], ob[128];
        if (std::fscanf(f, "%127s %d %d %d", room, &gStress.every, &gStress.add, &gStress.max) == 4) {
            gStress.room = room;
            while (std::fscanf(f, "%127s", ob) == 1) { int o = objByName(g, ob); if (o >= 0) gStress.objs.push_back(o); else logf("[estres] objeto desconocido: %s\n", ob); }
            gStressOn = !gStress.objs.empty() && roomByName(g, room) >= 0;
            if (gStressOn) logf("[estres] en %s: %d enemigos cada %d frames hasta %d (%d tipos)\n", room, gStress.add, gStress.every, gStress.max, (int)gStress.objs.size());
        }
        std::fclose(f);
    }
}

void cheatsStep(Game& g);
void debugTourBeforeStep(Game& g) {
    bossRushStep(g);
    keysStep(g);
    if (gMonkey && !isMenuRoom(g.d.rooms[g.room].name)) monkeyStep(g);
    // unlockchars: el juego pone global.unlock_char = 1 al desbloquear el lore 63 (general_control Step) y lo vuelve a 0
    // en room_settings; aqui se mantiene a 1 para poder elegir a Gilda y Vladyn sin la partida completa
    if (gUnlockChars && gVarUnlockChar >= 0) g.globals[gVarUnlockChar] = Value(1.0);
    if (gGod && gObjPlayer >= 0 && gVarHp >= 0)
        for (Instance* p : g.instancesOf(gObjPlayer)) if (p->alive) {
            p->vars[gVarHp] = Value(999999.0);   // el jugador no tiene hp_max (usa otros nombres): hp directo
        }
    cheatsStep(g);
    const std::string& rn = g.d.rooms[g.room].name;
    if (gTourOn && g.pendingRoom < 0) {
        bool start = gTourIdx < 0 && !isMenuRoom(rn) && g.frame > 60;
        bool next = gTourIdx >= 0 && g.frame - gTourAt >= gTourFrames + kEntryFrames;
        if (start || next) {
            if (gTourIdx >= 0) logStats("[recorrido]", std::to_string(gTourIdx + 1) + "/" + std::to_string(gTour.size()) + " " + gTour[gTourIdx], gTourSt);
            gTourIdx++;
            if (gTourIdx >= (int)gTour.size()) { logf("[recorrido] terminado\n"); gTourOn = false; }
            else { g.pendingRoom = roomByName(g, gTour[gTourIdx]); gTourAt = g.frame + 1; gTourSt = Stats{}; }
        }
    }
    // estres: si se arranca en otra sala de juego (o una puerta lo saca), vuelve solo a la suya (como el Boss Rush)
    if (gStressOn && !gTourOn && rn != gStress.room && !isMenuRoom(rn) && g.frame > 60 && g.pendingRoom < 0)
        g.pendingRoom = roomByName(g, gStress.room);
    if (gStressOn && rn == gStress.room && !gTourOn) {
        if (gStressAt < 0) { gStressAt = g.frame; gStressNext = g.frame + gStress.every; gStressSt = Stats{}; logf("[estres] empieza en %s\n", rn.c_str()); }
        if (g.frame >= gStressNext && gStressCount < gStress.max) {
            logStats("[estres]", "enemigos " + std::to_string(gStressCount), gStressSt);
            Instance* pl = gObjPlayer >= 0 ? g.firstOf(gObjPlayer) : nullptr;
            double px = pl ? pl->x : g.roomW / 2.0, py = pl ? pl->y : g.roomH / 2.0;
            for (int k = 0; k < gStress.add && gStressCount < gStress.max; k++) {
                gRand = gRand * 1103515245u + 12345u;
                double dx = ((int)((gRand >> 16) % 500) - 250);
                if (std::abs(dx) < 60) dx = dx < 0 ? -60 : 60;
                int obj = gStress.objs[gStressCount % gStress.objs.size()];
                g.createInstance(px + dx, py - 40, obj, true);
                gStressCount++;
            }
            gStressNext = g.frame + gStress.every; gStressAt = g.frame; gStressSt = Stats{};
        } else if (g.frame >= gStressNext && gStressCount >= gStress.max) {
            logStats("[estres]", "enemigos " + std::to_string(gStressCount) + " (final)", gStressSt);
            gStressOn = false;
        }
    }
}

void debugTourAfterFrame(Game& g, double stepMs, double drawMs, double frameMs) {
    if (std::getenv("VS_VARSTATS") && g.frame == std::atoi(std::getenv("VS_VARSTATS"))) {   // TEMPORAL: variables por instancia
        std::vector<std::pair<int,int>> per(g.d.objects.size(), {0, 0}); long tot = 0; int n = 0;
        for (auto& p : g.insts) { per[p->obj].first++; per[p->obj].second += p->vars.size(); tot += p->vars.size(); n++; }
        logf("[vars] %d instancias, media %.1f variables\n", n, n ? (double)tot / n : 0.0);
        for (size_t o = 0; o < per.size(); o++) if (per[o].first) logf("[vars] %-22s x%3d  %5.1f vars\n", g.d.objects[o].name.c_str(), per[o].first, (double)per[o].second / per[o].first);
    }
    if (const char* e = std::getenv("VS_VARDUMP")) if (g.frame == std::atoi(e)) {   // TEMPORAL: valores numericos de todo
        for (auto& p : g.insts) if (p->alive) p->vars.forEach([&](int k, const Value& v) {
            if (v.isReal()) logf("[vardump] %s.%s = %.17g\n", g.d.objects[p->obj].name.c_str(), g.d.varNames[k].c_str(), v.r);
            else logf("[vardump] %s.%s = <tipo %d> %s\n", g.d.objects[p->obj].name.c_str(), g.d.varNames[k].c_str(), (int)v.t, v.isStr() ? v.toString().c_str() : ""); });
        for (size_t k = 0; k < g.d.varNames.size(); k++) if (const Value* gv = g.globals.get((int)k)) {   // todas (IntMap: size() no es el rango)
            if (gv->isReal()) logf("[vardump] global.%s = %.17g\n", g.d.varNames[k].c_str(), gv->r);
            else logf("[vardump] global.%s = <tipo %d> %s\n", g.d.varNames[k].c_str(), (int)gv->t, gv->isStr() ? gv->toString().c_str() : "");
        }
        for (auto& p : g.insts) if (p->alive) logf("[vardump] %s xy %.17g %.17g id %d img %.17g spd %.17g depth %.17g spr %d alarmas %d %d %d %d %d %d\n", g.d.objects[p->obj].name.c_str(), p->x, p->y,
            (int)p->id, p->image_index, p->image_speed, p->depth, p->sprite, p->alarm[0], p->alarm[1], p->alarm[2], p->alarm[3], p->alarm[4], p->alarm[5]);
    }
    gLastStep = gLastStep * 0.9 + stepMs * 0.1; gLastDraw = gLastDraw * 0.9 + drawMs * 0.1;
    int inst = (int)g.insts.size();
    if (gTourOn && gTourIdx >= 0) gTourSt.add(stepMs, drawMs, frameMs, inst, g.frame - gTourAt < kEntryFrames);
    if (gBROn && gBR.idx >= 0 && gBR.at > 0) gBR.st.add(stepMs, drawMs, frameMs, inst, g.frame - gBR.at < kEntryFrames);
    if (gStressOn && gStressAt >= 0) gStressSt.add(stepMs, drawMs, frameMs, inst, g.frame - gStressAt < kEntryFrames);
}

// ================================================================ menu de TRUCOS
// Se abre con SELECT (libre en los controles del juego). Mientras esta abierto el juego queda en pausa (no avanza la
// logica; los eventos Draw siguen dibujando la escena). Las opciones de encendido/apagado se guardan como archivos en
// save/ (los mismos de las herramientas de prueba: godmode, unlockchars...), asi que siguen activas al volver a entrar.
extern void (*gOverlayHook)(Game&);
extern int gHudPercent;   // vm.cpp: tamano del HUD (MODS)
int logrosTotal(); int logrosHechos(); bool logroInfo(int k, std::string& name);   // logros.cpp
extern void (*gLogroToast)(const std::string&);
bool logrosOverlay(Game& g);
extern void (*gPreDrawHook)(Game&);
void overlayBegin(Game& g);
void overlaySize(int& w, int& h);
void overlayRect(float x1, float y1, float x2, float y2, uint32_t bgr, float alpha);
void overlayText(Game& g, double x, double y, const std::string& s, double scale, uint32_t bgr, double alpha);

ModsCfg gMods;
int rearTouchZone(int x, int y) {
    if (gMods.touch == 0 || x < 0 || x >= 1920 || y < 0 || y >= 1088) return 0;
    if (gMods.touch == 2) return x < 960 ? 1 : 2;
    // Como Shotgun King Vita: dos zonas anchas solo en la parte de arriba del panel, con margen exterior y un gran
    // hueco central, para que las palmas que sujetan la consola no pulsen L1/R1 sin querer.
    if (x >= 120 && x < 820 && y >= 40 && y < 420) return 1;
    if (x >= 1100 && x < 1800 && y >= 40 && y < 420) return 2;
    return 0;
}

namespace {
bool gInfMana = false, gShowPerf = false;
int gMenu = 0;   // 0 ninguno, 1 TRUCOS, 2 MODS
bool gMenuOpen = false, gBlockInput = false; uint32_t gPrevBtn = 0; int gSel = 0, gSelMods = 0, gRepeat = 0;
bool gSelectHeld = false, gSelectUsed = false, gSelectLR = false;
std::vector<int> gWarpRooms; int gWarpSel = 0; int gWarpTarget = -1;
int gVarMp = -1, gVarSetMp = -1, gVarXp = -1, gVarXpMax = -1, gVarPStat = -1, gVarPSkill = -1;
std::string gMsg; int gMsgFrames = 0;

enum { C_GOD, C_MANA, C_LEVEL, C_POINTS, C_UNLOCK, C_WARP, C_PERF, C_CLOSE, C_COUNT };
enum { M_LANG, M_FONT, M_TSIZE, M_ICONS, M_TOUCH, M_HUD, M_LOGROS, M_CLOSE, M_COUNT };
bool gLogroList = false; int gLogroTop = 0;   // lista de logros (MODS -> Logros)
const char* kFontNames[] = {"Original", "Sistema negrita", "Sistema normal", "Sistema estrecha", "Sistema con remates"};
const int kTsizeSteps[] = {100, 90, 80, 70};

const int kHudSteps[] = {100, 85, 70, 60, 50};

void modsLoad() {
    gHudPercent = gMods.hud;   // sin mods.ini: el valor por defecto
    FILE* f = std::fopen((gSaveDir + "/mods.ini").c_str(), "r");
    if (!f) return;
    char k[64]; int v;
    while (std::fscanf(f, " %63[^=]=%d", k, &v) == 2) {
        std::string s = k;
        if (s == "lang") gMods.lang = std::max(0, std::min(1, v));
        else if (s == "font") gMods.font = std::max(0, std::min(4, v));
        else if (s == "tsize") gMods.tsize = std::max(70, std::min(100, v));
        else if (s == "icons") gMods.icons = std::max(0, std::min(1, v));
        else if (s == "touch") gMods.touch = std::max(0, std::min(2, v));
        else if (s == "hud") gMods.hud = std::max(50, std::min(100, v));
    }
    std::fclose(f);
    gHudPercent = gMods.hud;
}
void modsSave() {
    if (FILE* f = std::fopen((gSaveDir + "/mods.ini").c_str(), "w")) {
        std::fprintf(f, "lang=%d\nfont=%d\ntsize=%d\nicons=%d\ntouch=%d\nhud=%d\n", gMods.lang, gMods.font, gMods.tsize, gMods.icons, gMods.touch, gMods.hud);
        std::fclose(f);
    }
    logf("[mods] lang=%d font=%d tsize=%d icons=%d touch=%d hud=%d\n", gMods.lang, gMods.font, gMods.tsize, gMods.icons, gMods.touch, gMods.hud);
}

void setFlagFile(const char* name, bool on) {
    std::string p = gSaveDir + "/" + name;
    if (on) { if (FILE* f = std::fopen(p.c_str(), "w")) { std::fputs("1", f); std::fclose(f); } }
    else {   // tambien las variantes con sufijo que subio una prueba (godmode_0815...)
        for (int k = 0; k < 16; k++) { std::string q = testPath(gSaveDir, name); if (q.empty()) break; std::remove(q.c_str()); gSaveListDir.clear(); }
    }
    gSaveListDir.clear();
}
Instance* player(Game& g) { return gObjPlayer >= 0 ? g.firstOf(gObjPlayer) : nullptr; }
bool inGameplay(Game& g) { const std::string& n = g.d.rooms[g.room].name; return !isMenuRoom(n) && n != "quit_room" && player(g); }
void say(const std::string& m) { gMsg = m; gMsgFrames = 150; logf("[trucos] %s\n", m.c_str()); }

std::string label(Game& g, int k) {
    auto onoff = [](bool b) { return std::string(b ? "SÍ" : "NO"); };
    switch (k) {
        case C_GOD: return "Modo dios (no mueres): " + onoff(gGod);
        case C_MANA: return "Maná infinito: " + onoff(gInfMana);
        case C_LEVEL: return "Subir un nivel";
        case C_POINTS: return "+5 puntos de atributo y de habilidad";
        case C_UNLOCK: return "Personajes y Boss Rush desbloqueados: " + onoff(gUnlockChars);
        case C_WARP: {   // la fuente del juego dibuja '_' como una flecha: se muestra con espacios
            std::string n = gWarpRooms.empty() ? std::string("-") : g.d.rooms[gWarpRooms[gWarpSel]].name;
            std::replace(n.begin(), n.end(), '_', ' ');
            char cnt[32]; std::snprintf(cnt, sizeof cnt, "  (%d/%d)", gWarpSel + 1, (int)gWarpRooms.size());
            return "Ir a sala: < " + n + " >" + cnt;
        }
        case C_PERF: return "Mostrar rendimiento: " + onoff(gShowPerf);
        default: return "Cerrar";
    }
}

// Iconos de botones de la Vita: el juego guarda el nombre de cada boton del mando en variables joy_*_name de
// general_control (joy_name_script: "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start", "Select") y de ahi lo copian los
// tutoriales, la intro, los creditos y el menu de controles. Se cambian por codigos 0x11..0x1A que drawText dibuja como
// iconos (ver fn_draw.cpp). Mapeo del runner en la Vita: A = X (cruz), B = circulo, X = cuadrado, Y = triangulo,
// LB/RB = panel trasero izq/der, LT/RT = gatillos L/R.
const char* kJoyNames[10] = {"A", "B", "X", "Y", "LT", "RT", "LB", "RB", "Start", "Select"};
std::vector<int> gJoyVars; int gObjGeneral = -1;
void joyIconsInit(Game& g) {
    gObjGeneral = objByName(g, "general_control");
    for (size_t v = 0; v < g.d.varNames.size(); v++) {
        const std::string& n = g.d.varNames[v];
        if (n.size() > 9 && n.compare(0, 4, "joy_") == 0 && n.compare(n.size() - 5, 5, "_name") == 0) gJoyVars.push_back((int)v);
    }
}
void joyIconsApply(Game& g) {
    if (gObjGeneral < 0) return;
    for (Instance* in : g.instancesOf(gObjGeneral)) {
        if (!in->alive) continue;
        for (int v : gJoyVars) {
            Value* p = in->vars.get(v);
            if (!p || !p->isStr()) continue;
            const std::string& s = p->sref();
            if (gMods.icons) {
                for (int k = 0; k < 10; k++) if (s == kJoyNames[k]) { *p = Value::str(std::string(1, (char)(0x11 + k))); break; }
            } else if (s.size() == 1 && s[0] >= 0x11 && s[0] <= 0x1A) {
                *p = Value::str(kJoyNames[s[0] - 0x11]);
            }
        }
    }
}

// MODS: etiquetas bilingues (el menu tiene que entenderse antes de elegir el idioma)
std::string modLabel(int k) {
    switch (k) {
        case M_LANG: return std::string("Idioma / Language: < ") + (gMods.lang ? "Español" : "English") + " >";
        case M_FONT: return std::string("Fuente / Font: < ") + kFontNames[gMods.font] + " >";
        case M_TSIZE: return std::string("Tamaño del texto / Text size: < ") + std::to_string(gMods.tsize) + " % >";
        case M_ICONS: return std::string("Botones / Buttons: < ") + (gMods.icons ? "PS Vita" : "Original (Xbox)") + " >";
        case M_TOUCH: {
            const char* t[3] = {"Apagado / Off", "Solo arriba / Top only", "Entero / Full"};
            return std::string("Panel trasero / Rear touch: < ") + t[gMods.touch] + " >";
        }
        case M_LOGROS: return "Logros: " + std::to_string(logrosHechos()) + " de " + std::to_string(logrosTotal()) + "  (X: ver la lista)";
        case M_HUD: return std::string("HUD: < ") + std::to_string(gMods.hud) + " % >" + (gMods.hud == 100 ? " (original)" : "");
        default: return "Cerrar / Close";
    }
}
void modActivate(Game& g, int k, int dir) {
    int d = dir == 0 ? 1 : dir;
    switch (k) {
        case M_LANG: gMods.lang ^= 1; say(gMods.lang ? "Español: se aplica al volver al menú principal (Salir) o al abrir el juego" : "English: applies when you return to the main menu (Quit) or restart"); break;
        case M_FONT: gMods.font = (gMods.font + d + 5) % 5; break;
        case M_TSIZE: { int i = 0; while (i < 3 && kTsizeSteps[i] != gMods.tsize) i++; i = (i + d + 4) % 4; gMods.tsize = kTsizeSteps[i]; break; }
        case M_ICONS: gMods.icons ^= 1; break;
        case M_TOUCH: gMods.touch = (gMods.touch + d + 3) % 3; break;
        case M_HUD: { int i = 0; while (i < 4 && kHudSteps[i] != gMods.hud) i++; i = (i + d + 5) % 5; gMods.hud = kHudSteps[i]; gHudPercent = gMods.hud; break; }
        case M_LOGROS: if (dir == 0) { gLogroList = true; gLogroTop = 0; } return;
        case M_CLOSE: if (dir == 0) { gMenuOpen = false; gBlockInput = true; } return;
    }
    (void)g; modsSave();
}

void activate(Game& g, int k, int dir) {   // dir 0 = X; -1/+1 = izquierda/derecha
    switch (k) {
        case C_GOD: gGod = !gGod; setFlagFile("godmode", gGod); say(gGod ? "Modo dios activado" : "Modo dios desactivado"); break;
        case C_MANA: gInfMana = !gInfMana; setFlagFile("infmana", gInfMana); say(gInfMana ? "Maná infinito activado" : "Maná infinito desactivado"); break;
        case C_UNLOCK:
            gUnlockChars = !gUnlockChars; setFlagFile("unlockchars", gUnlockChars);
            say(gUnlockChars ? "Personajes desbloqueados y Boss Rush en el menú principal" : "Personajes y Boss Rush: como en la partida");
            break;
        case C_PERF: gShowPerf = !gShowPerf; setFlagFile("showperf", gShowPerf); break;
        case C_LEVEL:
            if (dir != 0) break;
            if (!inGameplay(g) || gVarXp < 0 || gVarXpMax < 0) { say("Solo dentro de la partida"); break; }
            g.globals[gVarXp] = Value(g.globals[gVarXpMax].num() + 1);   // el propio juego sube el nivel en el Step del jugador
            say("Subes un nivel (máximo 20)");
            break;
        case C_POINTS:
            if (dir != 0) break;
            if (!inGameplay(g) || gVarPStat < 0 || gVarPSkill < 0) { say("Solo dentro de la partida"); break; }
            g.globals[gVarPStat] = Value(g.globals[gVarPStat].num() + 5);
            g.globals[gVarPSkill] = Value(g.globals[gVarPSkill].num() + 5);
            say("+5 puntos de atributo y +5 de habilidad: gástalos en el menú de pausa");
            break;
        case C_WARP:
            if (gWarpRooms.empty()) break;
            if (dir != 0) { int n = (int)gWarpRooms.size(); gWarpSel = ((gWarpSel + dir) % n + n) % n; break; }
            if (!inGameplay(g)) { say("Solo dentro de la partida"); break; }
            gWarpTarget = gWarpRooms[gWarpSel]; g.pendingRoom = gWarpTarget; gMenuOpen = false; gBlockInput = true;
            { std::string n = g.d.rooms[gWarpTarget].name; std::replace(n.begin(), n.end(), '_', ' ');
              say("Viajando a " + n + " (si apareces en una pared, elige otra sala)"); }
            break;
        case C_CLOSE: if (dir == 0) { gMenuOpen = false; gBlockInput = true; } break;
    }
}

void drawOverlay(Game& g) {
    static const char* textTest = std::getenv("VS_TEXTTEST");   // prueba de texto (acentos, iconos) en PC
    bossRushModeOverlay(g);
    logrosOverlay(g);   // aviso de logro (9.10)
    if (!gMenuOpen && !gShowPerf && gMsgFrames <= 0 && !textTest) return;
    overlayBegin(g);
    int W, H; overlaySize(W, H);
    if (textTest) { overlayRect(0, 60, (float)W, 200, 0, 0.7f); overlayText(g, 20, 80, textTest, 1.0, 0xFFFFFF, 1); overlayText(g, 20, 150, textTest, 0.5, 0x00E0FF, 1); }
    if (gShowPerf) {
        char b[96]; std::snprintf(b, sizeof b, "%d fps | logica %.1f ms | dibujo %.1f ms", (int)(g.fps + 0.5), gLastStep, gLastDraw);
        overlayRect(W - 364.f, H - 28.f, W - 4.f, H - 4.f, 0, 0.6f);   // abajo a la derecha: arriba esta la vida
        overlayText(g, W - 358.0, H - 26.0, b, 0.4, 0xFFFFFF, 1);
    }
    if (gMenuOpen) {
        float x0 = W * 0.5f - 320, y0 = H * 0.5f - 200, x1 = W * 0.5f + 320, y1 = H * 0.5f + 200;
        overlayRect(0, 0, (float)W, (float)H, 0, 0.45f);
        overlayRect(x0, y0, x1, y1, 0x201010, 0.92f);
        overlayRect(x0, y0, x1, y0 + 3, 0x00CCFF, 1);
        bool mods = gMenu == 2;
        if (mods && gLogroList) {
            overlayText(g, x0 + 20, y0 + 12, "LOGROS: " + std::to_string(logrosHechos()) + " de " + std::to_string(logrosTotal()), 0.6, 0x00CCFF, 1);
            for (int k = 0; k < 14; k++) {
                int i = gLogroTop + k; std::string nm; if (i >= logrosTotal()) break;
                bool have = logroInfo(i, nm);
                overlayText(g, x0 + 24, y0 + 52 + k * 22, std::string(have ? "[X] " : "[  ] ") + nm, 0.38, have ? 0x40E080 : 0x909090, 1);
            }
            overlayText(g, x0 + 20, y1 - 32, "Arriba/Abajo mover - X u O volver", 0.35, 0xB0B0B0, 1);
            return;
        }
        overlayText(g, x0 + 20, y0 + 12, mods ? "MODS" : "TRUCOS", 0.6, 0x00CCFF, 1);
        int n = mods ? M_COUNT : C_COUNT, sel = mods ? gSelMods : gSel;
        for (int k = 0; k < n; k++) {
            float y = y0 + 60 + k * 36;
            if (k == sel) overlayRect(x0 + 10, y - 4, x1 - 10, y + 28, 0x604020, 0.95f);
            overlayText(g, x0 + 24, y, mods ? modLabel(k) : label(g, k), 0.45, k == sel ? 0x00E0FF : 0xFFFFFF, 1);
        }
        overlayText(g, x0 + 20, y1 - 32, mods ? "\x11 / Izq / Der cambiar - \x12 o SELECT cerrar   (SELECT + L + R: trucos)"
                                              : "\x11 elegir - Izq/Der cambiar (L/R de 10 en 10) - \x12 o SELECT cerrar", 0.35, 0xB0B0B0, 1);
    }
    if (gMsgFrames > 0) {
        float y = H - 46.f;
        overlayRect(0, y - 4, (float)W, y + 28, 0, 0.6f);
        overlayText(g, 16, y, gMsg, 0.42, 0xFFFFFF, 1);
    }
}
} // namespace

void cheatsInit(Game& g) {
    gInfMana = testExists(gSaveDir, "infmana");
    gShowPerf = testExists(gSaveDir, "showperf");
    modsLoad();
    gVarMp = varByName(g, "mp"); gVarSetMp = varByName(g, "set_mp");
    gVarXp = varByName(g, "xp"); gVarXpMax = varByName(g, "xp_max");
    gVarPStat = varByName(g, "point_stat"); gVarPSkill = varByName(g, "point_skill");
    gWarpRooms.clear();
    for (size_t r = 0; r < g.d.rooms.size(); r++) {
        const std::string& n = g.d.rooms[r].name;
        if (isMenuRoom(n) || n == "quit_room" || n == "intro_1" || n.compare(0, 5, "test_") == 0) continue;
        if (g.d.rooms[r].insts.empty() && g.d.rooms[r].tiles.empty()) continue;
        gWarpRooms.push_back((int)r);
    }
    gOverlayHook = drawOverlay;
    gLogroToast = say;   // aviso de logro desbloqueado
    joyIconsInit(g);
    gPreDrawHook = joyIconsApply;
    bossRushModeInit(g);
}

void cheatsStep(Game& g) {
    joyIconsApply(g);
    bossRushModeStep(g);
    {   // Correccion del port (9.2, 23-11): al bajar del tren el jugador se quedaba en depth 9 (detras de los edificios).
        // tram_travel_script pone depth 0 y pide el cambio de sala, pero en ese mismo frame el End Step del jugador ve
        // tram.go > 0 y lo vuelve a poner a 9. Solo el tren usa depth 9: si no hay un tren en marcha, se devuelve a 0.
        static int objTram = -2, varGo = -1;
        if (objTram == -2) { objTram = objByName(g, "tram"); varGo = varByName(g, "go"); }
        if (Instance* p = player(g)) if (p->depth == 9) {
            bool riding = false;
            if (objTram >= 0 && varGo >= 0) for (Instance* t : g.instancesOf(objTram)) { const Value* v = t->vars.get(varGo); if (v && v->num() > 0) riding = true; }
            if (!riding) p->depth = 0;
        }
    }
    if (gInfMana && gVarMp >= 0 && gVarSetMp >= 0)
        if (Instance* p = player(g)) p->vars[gVarMp] = g.globals[gVarSetMp];
    // tras saltar de sala: si el jugador queda fuera de los limites de la sala, se le pone dentro
    if (gWarpTarget >= 0 && g.room == gWarpTarget && g.pendingRoom < 0) {
        if (Instance* p = player(g)) {
            p->x = std::max(48.0, std::min(p->x, (double)g.roomW - 48));
            p->y = std::max(48.0, std::min(p->y, (double)g.roomH - 96));
        }
        gWarpTarget = -1;
    }
    if (gMsgFrames > 0) gMsgFrames--;
}

bool cheatsInput(Game& g, uint32_t b) {
    uint32_t pr = b & ~gPrevBtn; gPrevBtn = b;
    auto clearPad = [&]() { for (bool& x : g.in.gpBtn) x = false; for (float& a : g.in.gpAxis) a = 0; };
    if (!gMenuOpen && bossRushModeInput(g, b, false)) return true;
    if (!gMenuOpen) {
        if (gBlockInput) { if (b == 0) gBlockInput = false; else { clearPad(); return false; } }
        // SELECT: al soltarlo abre MODS; si mientras se mantiene se pulsan L y R, abre TRUCOS
        if (pr & 64) { gSelectHeld = true; gSelectUsed = false; gSelectLR = false; }
        if (gSelectHeld) {
            if ((b & 64) && (b & 128) && (b & 256) && !gSelectUsed) {
                gSelectUsed = true; gMenu = 1; gMenuOpen = true; gRepeat = 0; clearPad(); return true;
            }
            if (b & (128 | 256)) gSelectLR = true;   // tocar L o R con SELECT nunca abre MODS al soltar
            if (!(b & 64)) {
                gSelectHeld = false;
                if (!gSelectUsed && !gSelectLR) { gMenu = 2; gMenuOpen = true; gRepeat = 0; clearPad(); return true; }
            }
            clearPad();   // mientras se mantiene SELECT el juego no recibe el mando (SELECT no tiene uso en el juego)
            return false;
        }
        return false;
    }
    clearPad();
    if (gMsgFrames > 0) gMsgFrames--;
    uint32_t dirs = b & 15, act = pr;   // repeticion al mantener una direccion
    if (dirs) { if (++gRepeat > 24 && gRepeat % 4 == 0) act |= dirs; } else gRepeat = 0;
    if (gMenu == 2 && gLogroList) {   // lista de logros: arriba/abajo, X u O vuelve a MODS
        int n = logrosTotal();
        if (act & 1) gLogroTop = std::max(0, gLogroTop - 1);
        if (act & 2) gLogroTop = std::min(std::max(0, n - 14), gLogroTop + 1);
        if (pr & (16 | 32)) gLogroList = false;
        if (pr & 64) { gLogroList = false; gMenuOpen = false; gBlockInput = true; }
        return true;
    }
    if (gMenu == 2) {
        if (act & 1) gSelMods = (gSelMods + M_COUNT - 1) % M_COUNT;
        if (act & 2) gSelMods = (gSelMods + 1) % M_COUNT;
        if (pr & 4) modActivate(g, gSelMods, -1);
        if (pr & 8) modActivate(g, gSelMods, +1);
        if (gMenuOpen && (pr & 16)) modActivate(g, gSelMods, 0);
        if (gMenuOpen && (pr & (32 | 64))) { gMenuOpen = false; gBlockInput = true; }
        return true;
    }
    if (act & 1) gSel = (gSel + C_COUNT - 1) % C_COUNT;
    if (act & 2) gSel = (gSel + 1) % C_COUNT;
    if (act & 4) activate(g, gSel, -1);
    if (act & 8) activate(g, gSel, +1);
    if (gSel == C_WARP && !gWarpRooms.empty()) {
        int n = (int)gWarpRooms.size();
        if (pr & 128) gWarpSel = ((gWarpSel - 10) % n + n) % n;
        if (pr & 256) gWarpSel = (gWarpSel + 10) % n;
    }
    if (gMenuOpen && (pr & 16)) activate(g, gSel, 0);
    if (gMenuOpen && (pr & (32 | 64))) { gMenuOpen = false; gBlockInput = true; }
    return true;   // el frame en que se cierra tampoco avanza
}

} // namespace vs
