// Modo Boss Rush (8.5). Se desbloquea como los modos extra del juego: al terminar la historia con cualquier personaje
// (global.unlock_char = 1, lore 63; el truco "personajes desbloqueados" tambien lo activa). Aparece como "-Boss Rush-"
// debajo de "-Salir-" en el menu principal.
//
// Fidelidad: justo antes de cada pelea de la historia el juego guarda una copia completa del estado (quick_save_script ->
// boss_save.ini -> vvvd.dat, el archivo de "Reintentar"). El runner copia esa instantanea en save/bossrush/pC_bN.ini
// (C = personaje, N = numero del jefe en global.boss_N) cada vez que se llega a un jefe jugando normal. El Boss Rush
// "fiel" carga cada instantanea con los mismos scripts que "Reintentar": nivel, atributos, armas, hechizos y objetos
// son los que se tenian en ese encuentro. Si un jefe aun no esta grabado, se parte de la partida guardada mas avanzada
// de ese personaje con TODO el equipo (armas, armaduras, accesorios, almas y remates).
// Modo aleatorio: igual, pero el equipo se baraja (lo equipado se conserva para no romper la partida).
//
// Durante el Boss Rush los guardados van a un modo aislado de bgio: el juego cree que guarda, pero nada llega a la
// tarjeta, y al salir se olvida (las partidas del usuario no se tocan).
#include "runtime.h"
#include "bgio.h"
#include "gfx.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace vs {
extern std::string gSaveDir;
void geomTouchObj(int obj); extern uint64_t gGeomEpoch;
extern void (*gRenameHook)(Game&, const std::string&, const std::string&);
extern void (*gViewDrawHook)(Game&);
void overlayBegin(Game& g);
void overlaySize(int& w, int& h);
void overlayRect(float x1, float y1, float x2, float y2, uint32_t bgr, float alpha);
void overlayText(Game& g, double x, double y, const std::string& s, double scale, uint32_t bgr, double alpha);

namespace {
struct BossDef { int n; const char* name; const char* room; const char* obj; const char* pre; char ent = 'O'; };
// ent (solo sin instantanea): 'O' junto al jefe o arriba al centro; 'L' / 'R' puerta izquierda / derecha de la sala;
// 'X' solo con estado grabado (su escena de historia necesita mas que unas banderas: sin grabar se salta).
// Probado en PC (20-11, volviendo al titulo entre jefes): 18 de 20 funcionan sin grabar; Ruckjahl y Dhistahn poseido
// solo con su grabacion.
// Orden aproximado de la historia. pre: banderas para llegar al jefe sin instantanea ("var=valor,...")
const BossDef kBosses[] = {
    {1, "Azudor", "outer_slum_7", "azudor", "boss_10=1"},
    {10, "Yeti", "outer_slum_7", "yeti", "boss_1=0,prison_lost=0"},
    {2, "Tagu", "grave_9", "tagu", ""},
    {3, "Ruckjahl", "grave_18", "ruckjahl_head", "", 'X'},
    {4, "Shu", "garden_7", "shu", "scene_check_32=1"},
    {5, "Dhistahn", "sewer_13", "dhistahn", "", 'R'},
    {18, "Lozzo", "sewer_4", "lozzo", ""},
    {15, "El Cuervo", "lost_4", "raven", "", 'R'},
    {14, "Magus", "west_5", "magus", "boss_8=1"},
    {6, "Jahzracht", "temple_16", "jahzracht", "scene_check_59=1"},
    {7, "Dhistahn poseído", "mines_9", "evil_dhistahn", "", 'X'},
    {8, "Treu e Itahn", "palace_7", "treu", "scene_check_76=1"},
    {16, "El Manawar", "west_5", "manawar", "boss_8=0,boss_14=0"},
    {9, "Gernot", "zigg_8", "gernot", "", 'R'},
    {19, "Abbigail", "hallow_5", "abbigail", "", 'L'},
    {12, "Ojo de Alagath", "hallow_2", "alagath", "", 'L'},
    {11, "Ojo de Myrgato", "pit_3", "myrgato", ""},
    {13, "Anemone", "sanct_6", "anemone", ""},
    {17, "Anemone verdadera", "sanct_12", "true_anemone", "", 'L'},
    {20, "Ragano", "lab_6", "ragano", ""},
};
const int kNumBosses = sizeof(kBosses) / sizeof(kBosses[0]);
const char* kChars[] = {"", "Wyatt", "Reina", "Vladyn", "Gilda"};

// Restart: entre jefes se vuelve al titulo por dentro (game_restart) y se carga desde alli, como "Reintentar":
// cargar en mitad de la escena de victoria del jefe anterior arrastraba estado que la partida no guarda.
enum class St { Off, Menu, Restart, Load, Fight, Won, Dead, Done };
int gTitleAt = -1;
St gSt = St::Off;
bool gSel = false;                 // cursor en "-Boss Rush-" del menu principal
int gMenuRow = 0, gChar = 1, gModeRandom = 0;
int gIdx = 0, gFightAt = 0, gWonAt = 0, gRepeat = 0; bool gRatingSeen = false; uint32_t gPrev = 0; bool gBlock = false;
bool gPlaced = true;               // sin instantanea: falta poner al jugador en una entrada de la sala
struct Result { int boss; int frames; bool won; bool snap; };
std::vector<Result> gResults;
std::vector<int> gRun;             // indices de kBosses que se juegan
std::vector<bool> gRunSnap;
uint32_t gRnd = 1;
int gTries = 0;
// pendiente del grabador: instantanea recien escrita (se nombra en el frame siguiente, cuando el juego ya puso boss_N=0)
std::string gPendSnap; int gPendFrame = -1;

std::map<std::string, int> gVar;
int var(Game& g, const std::string& n) {
    if (gVar.empty()) for (size_t k = 0; k < g.d.varNames.size(); k++) gVar.emplace(g.d.varNames[k], (int)k);
    auto it = gVar.find(n); return it == gVar.end() ? -1 : it->second;
}
double gnum(Game& g, const std::string& n) { int k = var(g, n); return k >= 0 && g.globals.count(k) ? g.globals[k].num() : 0; }
void gset(Game& g, const std::string& n, double v) { int k = var(g, n); if (k >= 0) g.globals[k] = Value(v); }
int objIdx(Game& g, const char* n) { auto it = g.d.objByName.find(n); return it == g.d.objByName.end() ? -1 : it->second; }
int roomIdx(Game& g, const char* n) { auto it = g.d.roomByName.find(n); return it == g.d.roomByName.end() ? -1 : it->second; }
Instance* gc(Game& g) { int o = objIdx(g, "general_control"); return o >= 0 ? g.firstOf(o) : nullptr; }
double ivar(Game& g, Instance* i, const char* n) { int k = var(g, n); const Value* v = (i && k >= 0) ? i->vars.get(k) : nullptr; return v ? v->num() : 0; }
void iset(Game& g, Instance* i, const char* n, double v) { int k = var(g, n); if (i && k >= 0) i->vars[k] = Value(v); }
const std::string& roomName(Game& g) { static std::string none; return g.room >= 0 ? g.d.rooms[g.room].name : none; }
char entMode(int bi) { if (const char* e = std::getenv("VS_BRENT")) return e[0]; return kBosses[bi].ent; }
uint32_t rnd() { gRnd = gRnd * 1103515245u + 12345u; return gRnd >> 16; }

bool callFn(Game& g, const char* fn, std::vector<Value> args) {
    auto& reg = builtinRegistry(); auto it = reg.find(fn); if (it == reg.end()) return false;
    Instance* self = gc(g); Ctx c; c.self = self; c.other = self;
    it->second(g, c, args.data(), (int)args.size()); return true;
}
bool runScript(Game& g, const char* name) {
    auto it = g.d.scriptByName.find(name); if (it == g.d.scriptByName.end()) { logf("[bossrush] falta el script %s\n", name); return false; }
    Instance* self = gc(g); Ctx c; c.self = self; c.other = self;
    g.runScript(it->second, c, nullptr, 0); return true;
}

// ---- archivos
std::string snapDir() { return gSaveDir + "/bossrush"; }
std::string snapPath(int ch, int n) { return snapDir() + "/p" + std::to_string(ch) + "_b" + std::to_string(n) + ".ini"; }
bool readFile(const std::string& p, std::string& out) {
    FILE* f = std::fopen(p.c_str(), "rb"); if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0); size_t rd = n > 0 ? std::fread(&out[0], 1, (size_t)n, f) : 0; std::fclose(f);
    out.resize(rd); return true;
}
bool fileThere(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
void makeDir(const std::string& p) {
#ifdef _WIN32
    mkdir(p.c_str());
#else
    mkdir(p.c_str(), 0777);
#endif
}
// valor de una clave del ini guardado (global.xxx="1.000000"), sin importar la seccion
bool iniVal(const std::string& text, const std::string& key, double& out) {
    std::string pat = "\n" + key + "=\"";
    size_t p = text.find(pat);
    if (p == std::string::npos) { if (text.compare(0, key.size() + 2, key + "=\"") != 0) return false; p = 0; } else p += 1;
    p += key.size() + 2; out = std::atof(text.c_str() + p); return true;
}

// ---- grabador (partida normal)
void onRename(Game& g, const std::string& from, const std::string& to) {
    if (std::getenv("VS_BRDEBUG")) logf("[brdbg] rename %s -> %s (aislado %d, estado %d)\n", from.c_str(), to.c_str(), (int)bgio::sandbox(), (int)gSt);
    if (bgio::sandbox() || gSt != St::Off) return;
    if (from != "boss_save.ini") return;
    std::string data; if (!bgio::read(g.savePath(to), data)) return;
    gPendSnap = std::move(data); gPendFrame = g.frame;
}
void recorderStep(Game& g) {
    if (gPendFrame < 0 || g.frame == gPendFrame) return;
    std::string data; data.swap(gPendSnap); gPendFrame = -1;
    double ch = 0; if (!iniVal(data, "global.player_current", ch)) { logf("[bossrush] instantanea sin player_current (%d bytes)\n", (int)data.size()); return; }
    if (std::getenv("VS_BRDEBUG")) { double b1 = -1; iniVal(data, "global.boss_1", b1); logf("[brdbg] instantanea: personaje %.0f, boss_1 guardado %.0f ahora %.0f, sala %s\n", ch, b1, gnum(g, "boss_1"), roomName(g).c_str()); }
    for (auto& b : kBosses) {   // el jefe de esta instantanea: boss_N estaba a 1 al guardar y el juego lo acaba de poner a 0
        double was = 0;
        if (!iniVal(data, "global.boss_" + std::to_string(b.n), was) || was <= 0) continue;
        if (gnum(g, "boss_" + std::to_string(b.n)) != 0) continue;
        if (roomName(g) != b.room) continue;
        makeDir(snapDir());
        std::string p = snapPath((int)ch, b.n);
        if (FILE* f = std::fopen(p.c_str(), "wb")) { std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); }
        logf("[bossrush] grabado el estado antes de %s (%s): %s\n", b.name, kChars[(int)ch >= 1 && ch <= 4 ? (int)ch : 0], p.c_str());
        return;
    }
}

// ---- carga de un jefe
bool unlocked(Game& g) { return gnum(g, "unlock_char") == 1; }
bool charAvailable(Game& g, int c) { return c <= 2 || unlocked(g); }

// partida guardada mas avanzada de un personaje (o de cualquiera si no tiene): base para los jefes sin grabar
bool bestSlot(Game& g, int ch, std::string& out) {
    int best = -1; std::string bestData; bool bestSame = false;
    for (int s = 1; s <= 16; s++) {
        int k = var(g, "savename"); if (k < 0) break;
        // global.savename[s]: arreglo de cadenas; se lee con el nombre de archivo conocido del juego
        static const char* names[17] = {"", "vvvo.dat", "vvvr.dat", "vvva.dat", "vvvz.dat", "vvvx.dat", "vvvc.dat", "vvvv.dat", "vvvf.dat",
                                        "vvvg.dat", "vvvh.dat", "vvvj.dat", "vvvn.dat", "vvvm.dat", "vvvk.dat", "vvvw.dat", "vvve.dat"};
        std::string data; if (!bgio::read(g.savePath(names[s]), data)) continue;
        double pc = 0; if (!iniVal(data, "global.player_current", pc)) continue;
        int dead = 0; for (auto& b : kBosses) { double v = 1; if (iniVal(data, "global.boss_" + std::to_string(b.n), v) && v == 0) dead++; }
        bool same = (int)pc == ch;
        int score = dead + (same ? 1000 : 0);
        if (score > best) { best = score; bestData.swap(data); bestSame = same; }
    }
    (void)bestSame;
    if (best < 0) return false;
    out.swap(bestData); return true;
}

void giveAll(Game& g) {
    for (int k = 1; k <= 16; k++) { gset(g, "wep_" + std::to_string(k), 1); gset(g, "armor_" + std::to_string(k), 1); gset(g, "acc_" + std::to_string(k), 1); }
    for (const char* s : {"soul_angel", "soul_demon", "soul_frost", "soul_burn", "soul_shock", "soul_void"}) gset(g, s, 1);
    for (const char* f : {"fin_ally", "fin_cleave", "fin_divide", "fin_hound", "fin_hurricane", "fin_life", "fin_lion", "fin_luck", "fin_phantom", "fin_shadow", "fin_wolf"}) gset(g, f, 1);
}
void randomize(Game& g) {
    int eqA = (int)gnum(g, "eqp_armor"), eqC = (int)gnum(g, "eqp_accessory");
    for (int k = 1; k <= 16; k++) {
        if (rnd() % 2) gset(g, "wep_" + std::to_string(k), 1);   // las armas solo se suman: la equipada no se conoce por nombre
        if (k != eqA) gset(g, "armor_" + std::to_string(k), rnd() % 2);
        if (k != eqC) gset(g, "acc_" + std::to_string(k), rnd() % 2);
    }
    for (const char* s : {"soul_angel", "soul_demon", "soul_frost", "soul_burn", "soul_shock", "soul_void"}) gset(g, s, rnd() % 2);
    for (const char* f : {"fin_ally", "fin_cleave", "fin_divide", "fin_hound", "fin_hurricane", "fin_life", "fin_lion", "fin_luck", "fin_phantom", "fin_shadow", "fin_wolf"}) gset(g, f, rnd() % 2);
}

bool loadBoss(Game& g, int bi) {
    const BossDef& b = kBosses[bi];
    std::string data; bool snap = readFile(snapPath(gChar, b.n), data);
    if (!snap && b.ent == 'X' && !std::getenv("VS_BRNOX")) { logf("[bossrush] %s: sin estado grabado (se salta)\n", b.name); return false; }
    if (!snap && !bestSlot(g, gChar, data)) { logf("[bossrush] sin partida base para %s\n", b.name); return false; }
    int r = roomIdx(g, b.room); if (r < 0) return false;
    runScript(g, "retry_script");
    bgio::write(g.savePath("vsbr_tmp.ini"), data);   // modo aislado: solo en memoria
    callFn(g, "ini_open", {Value::str("vsbr_tmp.ini")});
    runScript(g, "load_player_script"); runScript(g, "load_map_script"); runScript(g, "load_item_script"); runScript(g, "load_scene_script");
    callFn(g, "ini_close", {});
    if (!snap) {   // sin instantanea: llevar al jugador a la sala del jefe con todo el equipo
        gset(g, "player_current", gChar);
        gset(g, "boss_" + std::to_string(b.n), 1);
        std::string pre = b.pre;
        for (size_t p = 0; p < pre.size();) {
            size_t e = pre.find(',', p); std::string kv = pre.substr(p, e == std::string::npos ? std::string::npos : e - p);
            size_t q = kv.find('='); if (q != std::string::npos) gset(g, kv.substr(0, q), std::atof(kv.c_str() + q + 1));
            if (e == std::string::npos) break; p = e + 1;
        }
        gset(g, "room_loc", r);
        double lx = g.d.rooms[r].w * 0.5, ly = 64;
        int o = objIdx(g, b.obj);   // junto al jefe si esta colocado en la sala
        for (auto& in : g.d.rooms[r].insts) if (in.obj == o) { lx = in.x + (in.x > g.d.rooms[r].w * 0.5 ? -260 : 260); ly = in.y - 48; break; }
        gset(g, "load_x", lx); gset(g, "load_y", ly);
        giveAll(g);
    }
    if (gModeRandom) randomize(g);
    gset(g, "goibniu_mode", 0); gset(g, "arena_mode", 0);
    if (std::getenv("VS_BRDEBUG")) logf("[brdbg] antes de load_finish: room_loc %.0f (sala %d) load %.0f,%.0f pending %d\n", gnum(g, "room_loc"), r, gnum(g, "load_x"), gnum(g, "load_y"), g.pendingRoom);
    runScript(g, "load_finish_script");
    if (std::getenv("VS_BRDEBUG")) logf("[brdbg] despues: pending %d\n", g.pendingRoom);
    gRunSnap[gIdx] = snap;
    logf("[bossrush] %d/%d %s (%s, %s)\n", gIdx + 1, (int)gRun.size(), b.name, snap ? "estado grabado" : "todo el equipo", gModeRandom ? "aleatorio" : "fiel");
    return true;
}

void finish(Game& g, bool toTitle) {
    gSt = St::Off; gSel = false;
    bgio::setSandbox(false);
    if (toTitle) g.restartRequested = true;
}
void startRun(Game& g) {
    gRun.clear(); gResults.clear();
    const char* only = std::getenv("VS_BRONLY");   // pruebas: numeros de jefe separados por comas
    std::string onlyFile;                           // en la Vita: save/bossrush_only.txt con los mismos numeros
    if (!only && readFile(gSaveDir + "/bossrush_only.txt", onlyFile)) {
        while (!onlyFile.empty() && (onlyFile.back() == '\n' || onlyFile.back() == '\r' || onlyFile.back() == ' ')) onlyFile.pop_back();
        if (!onlyFile.empty()) { only = onlyFile.c_str(); logf("[bossrush] solo los jefes %s (save/bossrush_only.txt)\n", only); }
    }
    for (int k = 0; k < kNumBosses; k++) {
        if (only) { std::string o = std::string(",") + only + ","; if (o.find("," + std::to_string(kBosses[k].n) + ",") == std::string::npos) continue; }
        gRun.push_back(k);
    }
    gRunSnap.assign(gRun.size(), false);
    gRnd = (uint32_t)std::time(nullptr) | 1; gIdx = 0; gTries = 0;
    bgio::setSandbox(true);
    gSt = St::Load;
    logf("[bossrush] empieza: %s, modo %s\n", kChars[gChar], gModeRandom ? "aleatorio" : "fiel");
}
int fmtTime(int frames, char* b, size_t n) { int s = frames / 60; return std::snprintf(b, n, "%d:%02d", s / 60, s % 60); }

// ---- menu principal: "-Boss Rush-" debajo de "-Salir-"
bool onTitlePage0(Game& g) {
    Instance* c = gc(g);
    return c && roomName(g) == "start_game" && ivar(g, c, "intro_page") == 0 && ivar(g, c, "intro_menu") == 1 && ivar(g, c, "intro_menu_fade") > 0.5;
}
void viewDraw(Game& g) {
    if (gSt != St::Off || !onTitlePage0(g) || !unlocked(g)) return;
    Instance* c = gc(g);
    double mx = ivar(g, c, "menu_x"), my = ivar(g, c, "menu_y"), sx = ivar(g, c, "splash_x"), vert = ivar(g, c, "vert");
    double tr = ivar(g, c, "intro_trans_5"), fade = ivar(g, c, "intro_menu_fade");
    auto sit = g.d.spriteByName.find("ui_start");
    if (sit != g.d.spriteByName.end()) g.drawSpriteFrame(sit->second, 2, sx + 92 + tr, vert + 472 - 12, 1, 1, 0, 0xFFFFFF, fade);
    int ha = g.drawHalign; g.drawHalign = 1;
    if (gSel) g.drawText(mx + 200 + tr, my + 482, "-Boss Rush-", 0.75, 0.75, 0, 0xFFB15F, 0xFFB15F, 0x00CCFF, 0x00CCFF, fade, -1, -1);
    else g.drawText(mx + 200 + tr, my + 482, "-Boss Rush-", 0.75, 0.75, 0, 0, 0, 0x404040, 0x404040, fade, -1, -1);
    g.drawHalign = ha;
}

int countSnaps(int ch) { int n = 0; for (auto& b : kBosses) if (fileThere(snapPath(ch, b.n))) n++; return n; }
} // namespace

void bossRushModeInit(Game& g) {
    gRenameHook = onRename;
    gViewDrawHook = viewDraw;
    (void)g;
}

// Entrada del mando (antes del paso del juego). Devuelve true si el juego queda en pausa este frame.
bool bossRushModeInput(Game& g, uint32_t b, bool menuOpen) {
    uint32_t pr = b & ~gPrev; gPrev = b;
    auto clearPad = [&]() { for (bool& x : g.in.gpBtn) x = false; for (float& a : g.in.gpAxis) a = 0; };
    if (gBlock) { if (b == 0) gBlock = false; else { clearPad(); return gSt == St::Menu || gSt == St::Dead || gSt == St::Done; } }
    if (menuOpen) return false;
    if (gSt == St::Off) {
        if (!onTitlePage0(g) || !unlocked(g)) { gSel = false; return false; }
        Instance* c = gc(g);
        if (!gSel) {
            if ((pr & 2) && ivar(g, c, "menu_level") == 5 && ivar(g, c, "menu_delay") == 0) {
                gSel = true; iset(g, c, "menu_level", 0); iset(g, c, "cursor_y", 502); clearPad(); gBlock = true;
                callFn(g, "audio_play_sound", {Value((double)(g.d.soundByName.count("sfx_cursor") ? g.d.soundByName["sfx_cursor"] : -1)), Value(1.0), Value(0.0)});
                return true;
            }
            return false;
        }
        clearPad();
        iset(g, c, "menu_level", 0); iset(g, c, "cursor_y", 502);
        if (pr & 1) { gSel = false; iset(g, c, "menu_level", 5); iset(g, c, "cursor_y", 448); gBlock = true; }
        else if (pr & 2) { gSel = false; bool retry = ivar(g, c, "menu_level_max") == 6; iset(g, c, "menu_level", retry ? 6 : 1); iset(g, c, "cursor_y", retry ? 226 : 272); gBlock = true; }
        else if (pr & 16) { gSt = St::Menu; gMenuRow = 0; gBlock = true; if (!charAvailable(g, gChar)) gChar = 1; }
        return true;
    }
    if (gSt == St::Menu) {
        clearPad();
        uint32_t act = pr, dirs = b & 15;
        if (dirs) { if (++gRepeat > 24 && gRepeat % 4 == 0) act |= dirs; } else gRepeat = 0;
        if (act & 1) gMenuRow = (gMenuRow + 3) % 4;
        if (act & 2) gMenuRow = (gMenuRow + 1) % 4;
        int dir = (act & 4) ? -1 : (act & 8) ? 1 : 0;
        if (dir && gMenuRow == 0) { int c = gChar; do { c = (c + 3 + dir) % 4 + 1; } while (!charAvailable(g, c)); gChar = c; }
        if (dir && gMenuRow == 1) gModeRandom = !gModeRandom;
        if (pr & 16) {
            if (gMenuRow == 2) { startRun(g); gBlock = true; return false; }
            if (gMenuRow == 3) { gSt = St::Off; gBlock = true; }
        }
        if (pr & 32) { gSt = St::Off; gBlock = true; }
        return true;
    }
    if (gSt == St::Dead) {
        clearPad();
        if (pr & (1 | 2)) gMenuRow = 1 - gMenuRow;
        if (pr & 16) {
            if (gMenuRow == 0) { gTries++; gSt = St::Restart; gTitleAt = -1; g.restartRequested = true; gBlock = true; return false; }
            gSt = St::Done; gBlock = true;
        }
        return true;
    }
    if (gSt == St::Done) {
        clearPad();
        if (pr & (16 | 32)) { finish(g, true); gBlock = true; return false; }
        return true;
    }
    return false;
}

// Cada paso de la logica (antes del paso del juego)
bool testFileExists(const std::string& saveDir, const std::string& base);   // debugtour.cpp
void bossRushModeStep(Game& g) {
    recorderStep(g);
    {   // pruebas en la Vita: save/bossrush_auto arranca el Boss Rush solo en el titulo (Wyatt, modo fiel)
        static int autoState = -1;
        if (autoState < 0) autoState = testFileExists(gSaveDir, "bossrush_auto") ? 1 : 0;
        if (autoState == 1 && gSt == St::Off && roomName(g) == "start_game" && gc(g) && g.frame > 300) {
            autoState = 2; gChar = 1; gModeRandom = 0; logf("[bossrush] arranque automatico (save/bossrush_auto)\n"); startRun(g);
        }
    }
    if (gSt == St::Off || gSt == St::Menu || gSt == St::Done) return;
    const std::string& rn = roomName(g);
    if (gSt == St::Restart) {
        if (rn != "start_game" || g.pendingRoom >= 0) return;
        if (gTitleAt < 0) gTitleAt = g.frame;
        if (g.frame - gTitleAt >= 30) gSt = St::Load;
        return;
    }
    if (gSt != St::Load && rn == "start_game") { logf("[bossrush] vuelta al titulo: se abandona\n"); finish(g, false); return; }
    if (gSt == St::Load) {
        if (g.pendingRoom >= 0) return;
        while (gIdx < (int)gRun.size() && !loadBoss(g, gRun[gIdx])) { gResults.push_back({gRun[gIdx], 0, false, false}); gIdx++; }
        if (gIdx >= (int)gRun.size()) { gSt = St::Done; return; }
        gSt = St::Fight; gFightAt = g.frame; gRatingSeen = false; gPlaced = gRunSnap[gIdx] || entMode(gRun[gIdx]) == 'O';
        return;
    }
    int rating = objIdx(g, "boss_rating");
    static int killAfter = std::getenv("VS_BRKILL") ? std::atoi(std::getenv("VS_BRKILL")) : -1;   // pruebas: el jefe cae a los N frames
    if (killAfter > 0 && gSt == St::Fight && g.frame - gFightAt == killAfter) {
        std::vector<std::string> objs = {kBosses[gRun[gIdx]].obj};
        if (kBosses[gRun[gIdx]].n == 3) objs = {"ruckjahl_head", "ruckjahl_core", "ruckjahl_chest", "ruckjahl_back", "ruckjahl_shoulder"};
        if (kBosses[gRun[gIdx]].n == 7) objs = {"evil_dhistahn", "shin_azudor"};
        for (auto& on : objs) { int o = objIdx(g, on.c_str()); if (o >= 0) for (Instance* i : g.instancesOf(o)) iset(g, i, "hp", 0); }
        logf("[bossrush] prueba: vida de %s a 0\n", kBosses[gRun[gIdx]].name);
    }
    if (gSt == St::Fight && !gPlaced && rn == kBosses[gRun[gIdx]].room) {
        // como al cruzar una puerta (player_wyatt Step, trans 2 / 1): entrada izquierda o derecha que define la sala
        int po = objIdx(g, "player_wyatt");
        Instance* p = po >= 0 ? g.firstOf(po) : nullptr;
        if (p) {
            double yl = gnum(g, "start_left_top"), yr = gnum(g, "start_right_top");
            bool right = entMode(gRun[gIdx]) == 'R' ? yr > 0 : yl <= 0;
            if (right) { p->x = g.roomW - 80; p->y = yr; } else { p->x = 80; p->y = yl; }
            iset(g, p, "safe_x", p->x); iset(g, p, "safe_y", p->y);
            p->hspeed = 0; p->vspeed = 0; geomTouchObj(p->obj); gGeomEpoch++;
            gPlaced = true;
            logf("[bossrush] entrada en %s: %.0f, %.0f\n", rn.c_str(), p->x, p->y);
        }
    }
    if (gSt == St::Fight) {
        if (rn == "ending_room" && !gRatingSeen) {   // Anemone verdadera: al caer empieza el final del juego
            gRatingSeen = true; gResults.push_back({gRun[gIdx], g.frame - gFightAt, true, (bool)gRunSnap[gIdx]});
            logf("[bossrush] victoria contra %s (final del juego)\n", kBosses[gRun[gIdx]].name); gSt = St::Won; gWonAt = g.frame; return;
        }
        if (rn == "death_room") { gSt = St::Dead; gMenuRow = 0; logf("[bossrush] derrota contra %s\n", kBosses[gRun[gIdx]].name); return; }
        bool r = rating >= 0 && !g.instancesOf(rating).empty();
        if (r && !gRatingSeen) {
            gRatingSeen = true;
            gResults.push_back({gRun[gIdx], g.frame - gFightAt, true, (bool)gRunSnap[gIdx]});
            logf("[bossrush] victoria contra %s en %d s\n", kBosses[gRun[gIdx]].name, (g.frame - gFightAt) / 60);
        }
        if (gRatingSeen && !r) { gSt = St::Won; gWonAt = g.frame; }
        static int skipAfter = std::getenv("VS_BRSKIP") ? std::atoi(std::getenv("VS_BRSKIP")) : -1;   // pruebas: limite por jefe
        if (skipAfter > 0 && !gRatingSeen && g.frame - gFightAt > skipAfter) {
            logf("[bossrush] prueba: %s no termina en %d frames (sala %s)\n", kBosses[gRun[gIdx]].name, skipAfter, rn.c_str());
            gResults.push_back({gRun[gIdx], 0, false, (bool)gRunSnap[gIdx]}); gSt = St::Won; gWonAt = g.frame;
        }
        return;
    }
    if (gSt == St::Won && g.frame - gWonAt > 90) {
        gIdx++; gTries = 0;
        if (gIdx >= (int)gRun.size()) { gSt = St::Done; logf("[bossrush] completado\n"); }
        else { gSt = St::Restart; gTitleAt = -1; g.restartRequested = true; }
    }
}

// Capa del runner (pantalla): submenu, marcador y resultados. Devuelve true si dibujo algo.
bool bossRushModeOverlay(Game& g) {
    if (gSt == St::Off) return false;
    overlayBegin(g);
    int W, H; overlaySize(W, H);
    char b[160];
    if (gSt == St::Menu) {
        float x0 = W * 0.5f - 320, y0 = H * 0.5f - 190, x1 = W * 0.5f + 320, y1 = H * 0.5f + 190;
        overlayRect(0, 0, (float)W, (float)H, 0, 0.5f);
        overlayRect(x0, y0, x1, y1, 0x201010, 0.94f);
        overlayRect(x0, y0, x1, y0 + 3, 0x00CCFF, 1);
        overlayText(g, x0 + 20, y0 + 12, "BOSS RUSH", 0.6, 0x00CCFF, 1);
        std::string rows[4] = {std::string("Personaje: < ") + kChars[gChar] + " >",
                               std::string("Modo: < ") + (gModeRandom ? "Aleatorio" : "Fiel a cada encuentro") + " >",
                               "Empezar", "Volver"};
        for (int k = 0; k < 4; k++) {
            float y = y0 + 64 + k * 38;
            if (k == gMenuRow) overlayRect(x0 + 10, y - 4, x1 - 10, y + 28, 0x604020, 0.95f);
            overlayText(g, x0 + 24, y, rows[k], 0.45, k == gMenuRow ? 0x00E0FF : 0xFFFFFF, 1);
        }
        int n = countSnaps(gChar);
        std::snprintf(b, sizeof b, "%d jefes seguidos. Con el estado grabado de tu partida: %d de %d.", kNumBosses, n, kNumBosses);
        overlayText(g, x0 + 20, y0 + 230, b, 0.36, 0xD0D0D0, 1);
        overlayText(g, x0 + 20, y0 + 256, "Los demás: tu partida más avanzada, con todo el equipo.", 0.36, 0xD0D0D0, 1);
        overlayText(g, x0 + 20, y0 + 282, gModeRandom ? "Aleatorio: armaduras, accesorios, almas y remates al azar (lo equipado se queda)."
                                                      : "Cada jefe se graba solo al llegar a él jugando la historia.", 0.36, 0xD0D0D0, 1);
        overlayText(g, x0 + 20, y1 - 32, "\x11 elegir - Izq/Der cambiar - \x12 volver. Los guardados no se tocan.", 0.35, 0xB0B0B0, 1);
        return true;
    }
    if (gSt == St::Fight || gSt == St::Won || gSt == St::Load || gSt == St::Restart) {
        if (gIdx < (int)gRun.size()) {
            char t[16]; fmtTime(gSt == St::Fight ? g.frame - gFightAt : 0, t, sizeof t);
            std::snprintf(b, sizeof b, "BOSS RUSH %d/%d  %s  %s", gIdx + 1, (int)gRun.size(), kBosses[gRun[gIdx]].name, t);
            overlayRect(W * 0.5f - 230, H - 30.f, W * 0.5f + 230, H - 6.f, 0, 0.55f);
            overlayText(g, W * 0.5 - 220, H - 28.0, b, 0.38, 0x00CCFF, 1);
        }
        return true;
    }
    if (gSt == St::Dead) {
        float x0 = W * 0.5f - 260, y0 = H * 0.5f - 90, x1 = W * 0.5f + 260, y1 = H * 0.5f + 90;
        overlayRect(x0, y0, x1, y1, 0x101020, 0.94f);
        std::snprintf(b, sizeof b, "Derrota contra %s", kBosses[gRun[gIdx]].name);
        overlayText(g, x0 + 20, y0 + 14, b, 0.5, 0x4060FF, 1);
        const char* rows[2] = {"Reintentar este jefe", "Terminar el Boss Rush"};
        for (int k = 0; k < 2; k++) {
            float y = y0 + 70 + k * 38;
            if (k == gMenuRow) overlayRect(x0 + 10, y - 4, x1 - 10, y + 28, 0x604020, 0.95f);
            overlayText(g, x0 + 24, y, rows[k], 0.45, k == gMenuRow ? 0x00E0FF : 0xFFFFFF, 1);
        }
        return true;
    }
    if (gSt == St::Done) {
        float x0 = W * 0.5f - 330, y0 = 20, x1 = W * 0.5f + 330, y1 = H - 20.f;
        overlayRect(0, 0, (float)W, (float)H, 0, 0.6f);
        overlayRect(x0, y0, x1, y1, 0x201010, 0.95f);
        int won = 0, total = 0; for (auto& r : gResults) if (r.won) { won++; total += r.frames; }
        char t[16]; fmtTime(total, t, sizeof t);
        std::snprintf(b, sizeof b, "BOSS RUSH - %s: %d de %d jefes, %s", kChars[gChar], won, (int)gRun.size(), t);
        overlayText(g, x0 + 16, y0 + 10, b, 0.48, 0x00CCFF, 1);
        float y = y0 + 50;
        for (auto& r : gResults) {
            fmtTime(r.frames, t, sizeof t);
            std::snprintf(b, sizeof b, "%-20s %s %s", kBosses[r.boss].name, r.won ? t : "-", r.won ? (r.snap ? "" : "(todo el equipo)") : (kBosses[r.boss].ent == 'X' ? "(sin grabar: llega a él en la historia)" : "(no terminado)"));
            overlayText(g, x0 + 24, y, b, 0.34, r.won ? 0xFFFFFF : 0x9090A0, 1);
            y += 22; if (y > y1 - 50) break;
        }
        overlayText(g, x0 + 16, y1 - 30, "\x11 volver al título", 0.36, 0xB0B0B0, 1);
        return true;
    }
    return false;
}

} // namespace vs
