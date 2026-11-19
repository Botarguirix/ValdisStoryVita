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
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdlib>

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

// ---- Boss Rush de prueba
struct BossRush { std::string room; int frames = 1800; std::vector<int> objs; int idx = -1, at = 0, room_ = -1; long warn0 = 0; Stats st; } gBR;
bool gBROn = false;
void bossRushInit(Game& g, const std::string& saveDir) {
    FILE* f = std::fopen((saveDir + "/bossrush.txt").c_str(), "r");
    if (!f) return;
    char room[128], ob[128];
    if (std::fscanf(f, "%127s %d", room, &gBR.frames) == 2) {
        gBR.room = room; gBR.room_ = roomByName(g, room);
        while (std::fscanf(f, "%127s", ob) == 1) { int o = objByName(g, ob); if (o >= 0) gBR.objs.push_back(o); else logf("[jefe] objeto desconocido: %s\n", ob); }
    }
    std::fclose(f);
    gBROn = gBR.room_ >= 0 && !gBR.objs.empty();
    if (gBROn) logf("[jefe] Boss Rush de prueba en %s: %d jefes, %d frames cada uno\n", room, (int)gBR.objs.size(), gBR.frames);
}
void bossRushStep(Game& g) {
    if (!gBROn || g.pendingRoom >= 0) return;
    const std::string& rn = g.d.rooms[g.room].name;
    if (gBR.idx < 0) {   // primero a la sala del combate
        if (isMenuRoom(rn) || g.frame < 60) return;
        if (g.room != gBR.room_) { g.pendingRoom = gBR.room_; return; }
        gBR.idx = 0; gBR.at = 0;
    }
    if (g.room != gBR.room_) { g.pendingRoom = gBR.room_; return; }   // si una puerta o una muerte lo saca, vuelve
    if (gBR.at == 0 || g.frame - gBR.at >= gBR.frames) {
        if (gBR.at != 0) {   // cerrar el jefe actual
            int o = gBR.objs[gBR.idx], alive = 0;
            for (Instance* i : g.instancesOf(o)) if (i->alive) alive++;

            logStats("[jefe]", std::to_string(gBR.idx + 1) + "/" + std::to_string(gBR.objs.size()) + " " + g.d.objects[o].name +
                     (alive ? " (sigue vivo)" : " (derrotado)") + " avisos nuevos " + std::to_string(gWarnCount - gBR.warn0), gBR.st);
            for (Instance* i : g.instancesOf(o)) if (i->alive) g.destroyInstance(i, false);
            gBR.idx++;
            if (gBR.idx >= (int)gBR.objs.size()) { logf("[jefe] Boss Rush terminado\n"); gBROn = false; return; }
        }
        Instance* pl = gObjPlayer >= 0 ? g.firstOf(gObjPlayer) : nullptr;
        double px = pl ? pl->x : g.roomW / 2.0, py = pl ? pl->y : g.roomH / 2.0;
        int o = gBR.objs[gBR.idx];
        gBR.warn0 = gWarnCount;
        logf("[jefe] sale %s\n", g.d.objects[o].name.c_str());
        g.createInstance(px + 220, py - 60, o, true);
        gBR.at = g.frame; gBR.st = Stats{};
    }
}
} // namespace

void debugTourInit(Game& g, const std::string& saveDir) {
    gGod = fileExists(saveDir + "/godmode");
    gMonkey = fileExists(saveDir + "/monkey");
    bossRushInit(g, saveDir);
    { extern int gDrawDebug; if (fileExists(saveDir + "/drawdebug")) gDrawDebug = 400; }
    gUnlockChars = fileExists(saveDir + "/unlockchars"); gVarUnlockChar = varByName(g, "unlock_char");
    if (gUnlockChars) logf("[prueba] personajes desbloqueados (global.unlock_char = 1, variable %d)\n", gVarUnlockChar);
    gVarHp = varByName(g, "hp"); gVarHpMax = varByName(g, "hp_max"); gVarDeath = varByName(g, "death"); gObjPlayer = objByName(g, "player_wyatt");
    if (gGod) logf("[prueba] modo sin morir activo (hp=%d hp_max=%d jugador=%d)\n", gVarHp, gVarHpMax, gObjPlayer);
    if (FILE* f = std::fopen((saveDir + "/tour.txt").c_str(), "r")) {
        char w[128];
        while (std::fscanf(f, "%127s", w) == 1) {
            if (!std::strcmp(w, "FRAMES")) { int n; if (std::fscanf(f, "%d", &n) == 1) gTourFrames = std::max(60, n); continue; }
            if (roomByName(g, w) >= 0) gTour.push_back(w); else logf("[recorrido] sala desconocida: %s\n", w);
        }
        std::fclose(f);
        gTourOn = !gTour.empty();
        if (gTourOn) logf("[recorrido] %d salas, %d frames cada una (%.0f min)\n", (int)gTour.size(), gTourFrames, gTour.size() * gTourFrames / 3600.0);
    }
    if (FILE* f = std::fopen((saveDir + "/stress.txt").c_str(), "r")) {
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

void debugTourBeforeStep(Game& g) {
    bossRushStep(g);
    if (gMonkey && !isMenuRoom(g.d.rooms[g.room].name)) monkeyStep(g);
    // unlockchars: el juego pone global.unlock_char = 1 al desbloquear el lore 63 (general_control Step) y lo vuelve a 0
    // en room_settings; aqui se mantiene a 1 para poder elegir a Gilda y Vladyn sin la partida completa
    if (gUnlockChars && gVarUnlockChar >= 0) g.globals[gVarUnlockChar] = Value(1.0);
    if (gGod && gObjPlayer >= 0 && gVarHp >= 0)
        for (Instance* p : g.instancesOf(gObjPlayer)) if (p->alive) {
            p->vars[gVarHp] = Value(999999.0);   // el jugador no tiene hp_max (usa otros nombres): hp directo
        }
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
    int inst = (int)g.insts.size();
    if (gTourOn && gTourIdx >= 0) gTourSt.add(stepMs, drawMs, frameMs, inst, g.frame - gTourAt < kEntryFrames);
    if (gBROn && gBR.idx >= 0 && gBR.at > 0) gBR.st.add(stepMs, drawMs, frameMs, inst, g.frame - gBR.at < kEntryFrames);
    if (gStressOn && gStressAt >= 0) gStressSt.add(stepMs, drawMs, frameMs, inst, g.frame - gStressAt < kEntryFrames);
}

} // namespace vs
