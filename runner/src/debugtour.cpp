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

std::string gSaveDir; double gLastStep = 0, gLastDraw = 0;
void cheatsInit(Game& g);
void debugTourInit(Game& g, const std::string& saveDir) {
    gSaveDir = saveDir;
    gGod = fileExists(saveDir + "/godmode");
    gMonkey = fileExists(saveDir + "/monkey");
    bossRushInit(g, saveDir);
    { extern int gDrawDebug; if (fileExists(saveDir + "/drawdebug")) gDrawDebug = 400; }
    if (fileExists(saveDir + "/drawdebug")) gfx::gUploadDebug = true;
    gUnlockChars = fileExists(saveDir + "/unlockchars"); gVarUnlockChar = varByName(g, "unlock_char");
    if (gUnlockChars) logf("[prueba] personajes desbloqueados (global.unlock_char = 1, variable %d)\n", gVarUnlockChar);
    gVarHp = varByName(g, "hp"); gVarHpMax = varByName(g, "hp_max"); gVarDeath = varByName(g, "death"); gObjPlayer = objByName(g, "player_wyatt");
    cheatsInit(g);
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

void cheatsStep(Game& g);
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
void overlayBegin(Game& g);
void overlaySize(int& w, int& h);
void overlayRect(float x1, float y1, float x2, float y2, uint32_t bgr, float alpha);
void overlayText(Game& g, double x, double y, const std::string& s, double scale, uint32_t bgr, double alpha);

namespace {
bool gInfMana = false, gShowPerf = false;
bool gMenuOpen = false, gBlockInput = false; uint32_t gPrevBtn = 0; int gSel = 0, gRepeat = 0;
std::vector<int> gWarpRooms; int gWarpSel = 0; int gWarpTarget = -1;
int gVarMp = -1, gVarSetMp = -1, gVarXp = -1, gVarXpMax = -1, gVarPStat = -1, gVarPSkill = -1;
std::string gMsg; int gMsgFrames = 0;

enum { C_GOD, C_MANA, C_LEVEL, C_POINTS, C_UNLOCK, C_WARP, C_PERF, C_CLOSE, C_COUNT };

void setFlagFile(const char* name, bool on) {
    std::string p = gSaveDir + "/" + name;
    if (on) { if (FILE* f = std::fopen(p.c_str(), "w")) { std::fputs("1", f); std::fclose(f); } }
    else std::remove(p.c_str());
}
Instance* player(Game& g) { return gObjPlayer >= 0 ? g.firstOf(gObjPlayer) : nullptr; }
bool inGameplay(Game& g) { const std::string& n = g.d.rooms[g.room].name; return !isMenuRoom(n) && n != "quit_room" && player(g); }
void say(const std::string& m) { gMsg = m; gMsgFrames = 150; logf("[trucos] %s\n", m.c_str()); }

std::string label(Game& g, int k) {
    auto onoff = [](bool b) { return std::string(b ? "SI" : "NO"); };
    switch (k) {
        case C_GOD: return "Modo dios (no mueres): " + onoff(gGod);
        case C_MANA: return "Mana infinito: " + onoff(gInfMana);
        case C_LEVEL: return "Subir un nivel";
        case C_POINTS: return "+5 puntos de atributo y de habilidad";
        case C_UNLOCK: return "Personajes desbloqueados: " + onoff(gUnlockChars);
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

void activate(Game& g, int k, int dir) {   // dir 0 = X; -1/+1 = izquierda/derecha
    switch (k) {
        case C_GOD: gGod = !gGod; setFlagFile("godmode", gGod); say(gGod ? "Modo dios activado" : "Modo dios desactivado"); break;
        case C_MANA: gInfMana = !gInfMana; setFlagFile("infmana", gInfMana); say(gInfMana ? "Mana infinito activado" : "Mana infinito desactivado"); break;
        case C_UNLOCK:
            gUnlockChars = !gUnlockChars; setFlagFile("unlockchars", gUnlockChars);
            say(gUnlockChars ? "Personajes desbloqueados en la seleccion de personaje" : "Personajes: como en la partida");
            break;
        case C_PERF: gShowPerf = !gShowPerf; setFlagFile("showperf", gShowPerf); break;
        case C_LEVEL:
            if (dir != 0) break;
            if (!inGameplay(g) || gVarXp < 0 || gVarXpMax < 0) { say("Solo dentro de la partida"); break; }
            g.globals[gVarXp] = Value(g.globals[gVarXpMax].num() + 1);   // el propio juego sube el nivel en el Step del jugador
            say("Subes un nivel (maximo 20)");
            break;
        case C_POINTS:
            if (dir != 0) break;
            if (!inGameplay(g) || gVarPStat < 0 || gVarPSkill < 0) { say("Solo dentro de la partida"); break; }
            g.globals[gVarPStat] = Value(g.globals[gVarPStat].num() + 5);
            g.globals[gVarPSkill] = Value(g.globals[gVarPSkill].num() + 5);
            say("+5 puntos de atributo y +5 de habilidad: gastalos en el menu de pausa");
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
    if (!gMenuOpen && !gShowPerf && gMsgFrames <= 0) return;
    overlayBegin(g);
    int W, H; overlaySize(W, H);
    if (gShowPerf) {
        char b[96]; std::snprintf(b, sizeof b, "%d fps | logica %.1f ms | dibujo %.1f ms", g.fps, gLastStep, gLastDraw);
        overlayRect(W - 364.f, H - 28.f, W - 4.f, H - 4.f, 0, 0.6f);   // abajo a la derecha: arriba esta la vida
        overlayText(g, W - 358.0, H - 26.0, b, 0.4, 0xFFFFFF, 1);
    }
    if (gMenuOpen) {
        float x0 = W * 0.5f - 320, y0 = H * 0.5f - 200, x1 = W * 0.5f + 320, y1 = H * 0.5f + 200;
        overlayRect(0, 0, (float)W, (float)H, 0, 0.45f);
        overlayRect(x0, y0, x1, y1, 0x201010, 0.92f);
        overlayRect(x0, y0, x1, y0 + 3, 0x00CCFF, 1);
        overlayText(g, x0 + 20, y0 + 12, "TRUCOS", 0.6, 0x00CCFF, 1);
        for (int k = 0; k < C_COUNT; k++) {
            float y = y0 + 60 + k * 36;
            if (k == gSel) overlayRect(x0 + 10, y - 4, x1 - 10, y + 28, 0x604020, 0.95f);
            overlayText(g, x0 + 24, y, label(g, k), 0.45, k == gSel ? 0x00E0FF : 0xFFFFFF, 1);
        }
        overlayText(g, x0 + 20, y1 - 32, "X elegir - Izq/Der cambiar (L/R de 10 en 10) - O o SELECT cerrar", 0.35, 0xB0B0B0, 1);
    }
    if (gMsgFrames > 0) {
        float y = H - 46.f;
        overlayRect(0, y - 4, (float)W, y + 28, 0, 0.6f);
        overlayText(g, 16, y, gMsg, 0.42, 0xFFFFFF, 1);
    }
}
} // namespace

void cheatsInit(Game& g) {
    gInfMana = fileExists(gSaveDir + "/infmana");
    gShowPerf = fileExists(gSaveDir + "/showperf");
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
}

void cheatsStep(Game& g) {
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
    if (!gMenuOpen) {
        if (gBlockInput) { if (b == 0) gBlockInput = false; else { clearPad(); return false; } }
        if (pr & 64) { gMenuOpen = true; gRepeat = 0; clearPad(); return true; }
        return false;
    }
    clearPad();
    if (gMsgFrames > 0) gMsgFrames--;
    uint32_t dirs = b & 15, act = pr;   // repeticion al mantener una direccion
    if (dirs) { if (++gRepeat > 24 && gRepeat % 4 == 0) act |= dirs; } else gRepeat = 0;
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
