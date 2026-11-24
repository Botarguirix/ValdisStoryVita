// Logros locales (9.10). El juego solo da logros si Steam esta activo (steam_start_script: steam_initialised() y
// steam_stats_ready() -> global.steam_api = 1) y entonces llama a steam_get/set_achievement y steam_get/set_stat_int.
// Aqui esas funciones guardan en save/logros.ini, en la propia consola. La nube, las tablas de puntuacion y las
// capturas siguen desactivadas (las otras funciones steam_* de fn_core.cpp devuelven "sin datos").
// Se guarda con bgio: durante el Boss Rush (modo aislado) un logro se ve pero no se escribe en la tarjeta.
#include "runtime.h"
#include "bgio.h"
#include "gfx.h"
#include "debugtour.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <vector>
#include <string>

namespace vs {
extern std::string gSaveDir;
void (*gLogroToast)(const std::string&) = nullptr;   // debugtour.cpp: aviso en pantalla

namespace {
struct LogroDef { const char* id; const char* nombre; const char* desc; const char* icon = nullptr; };
#include "logros_tabla.inc"
const int kNumLogros = sizeof(kLogros) / sizeof(kLogros[0]);
std::map<std::string, int> gHave, gStats;
bool gLoaded = false;
std::string path() { return gSaveDir + "/logros.ini"; }
void load() {
    if (gLoaded) return;
    gLoaded = true;
    std::string text; if (!bgio::read(path(), text)) return;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p); std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? text.size() : e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t q = line.find('='); if (q == std::string::npos) continue;
        std::string k = line.substr(0, q); int v = std::atoi(line.c_str() + q + 1);
        if (k.compare(0, 5, "stat.") == 0) gStats[k.substr(5)] = v; else gHave[k] = v;
    }
}
void save() {
    std::string o = "; Logros locales de Valdis Story PSVITA (9.10)\n";
    for (auto& [k, v] : gHave) if (v) o += k + "=1\n";
    for (auto& [k, v] : gStats) o += "stat." + k + "=" + std::to_string(v) + "\n";
    bgio::write(path(), o);
}
const char* nombre(const std::string& id) { for (auto& l : kLogros) if (id == l.id) return l.nombre; return nullptr; }
const char* icono(const std::string& id) { for (auto& l : kLogros) if (id == l.id) return l.icon; return nullptr; }
} // namespace

// Aviso "¡Logro desbloqueado!" arriba al centro durante 4 s (como en DDVita); varios seguidos hacen cola
void overlayBegin(Game& g);
void overlaySize(int& w, int& h);
void overlayRect(float x1, float y1, float x2, float y2, uint32_t bgr, float alpha);
void overlayText(Game& g, double x, double y, const std::string& s, double scale, uint32_t bgr, double alpha);
struct Toast { std::string name; const char* icon; std::string id; };
static std::vector<Toast> gToastQueue; static int gToastFrames = 0;
void logrosToastPush(const std::string& name, const char* icon, const std::string& id = "") { gToastQueue.push_back({name, icon, id}); }
// Icono de Steam del logro (tools/logros_iconos.py): <vsdata>/logros/<id>.png o <id>_gris.png. -1 si no esta.
int logroIconTex(Game& g, const std::string& id, bool have) {
    static std::map<std::string, int> cache;
    if (id.empty()) return -1;
    std::string key = have ? id : id + "_gris";
    auto it = cache.find(key); if (it != cache.end()) return it->second;
    int w, h; int t = gfx::loadDynamic(g.dataDir + "/logros/" + key + ".png", w, h);
    cache[key] = t; return t;
}
// Dibuja el icono del logro en el cuadro (x, y, lado); false si no hay imagen de Steam
bool logroIconDraw(Game& g, const std::string& id, bool have, float x, float y, float s, float a) {
    int t = logroIconTex(g, id, have); int w, h;
    if (t == -1 || !gfx::texSize(t, w, h)) return false;
    float xy[8] = {x, y, x + s, y, x + s, y + s, x, y + s};
    float uv[8] = {0, 0, (float)w, 0, (float)w, (float)h, 0, (float)h};
    uint32_t cc[4] = {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF};
    gfx::quad(t, xy, uv, cc, a);
    return true;
}
// sprite del icono: anim_<nombre>_idle, <nombre>_idle o el sprite del objeto; -1 = trofeo
static int iconSprite(Game& g, const char* icon) {
    if (!icon) return -1;
    for (std::string n : {std::string("anim_") + icon + "_idle", std::string(icon) + "_idle", std::string(icon)}) {
        auto it = g.d.spriteByName.find(n); if (it != g.d.spriteByName.end()) return it->second;
    }
    auto ot = g.d.objByName.find(icon);
    if (ot != g.d.objByName.end() && g.d.objects[ot->second].sprite >= 0) return g.d.objects[ot->second].sprite;
    return -1;
}
bool logrosOverlay(Game& g) {
    static bool testDone = false;   // pruebas: VS_LOGROTEST=id1,id2,... encola esos avisos (sin desbloquear nada)
    if (!testDone) { testDone = true; if (const char* e = std::getenv("VS_LOGROTEST")) {
        std::string ids = e; size_t p = 0;
        while (p <= ids.size()) { size_t c = ids.find(',', p); std::string id = ids.substr(p, c == std::string::npos ? std::string::npos : c - p);
            if (!id.empty()) logrosToastPush(nombre(id) ? nombre(id) : id, icono(id), id); if (c == std::string::npos) break; p = c + 1; } } }
    if (gToastFrames <= 0) { if (gToastQueue.empty()) return false; gToastFrames = 240; }
    gToastFrames--;
    const Toast t = gToastQueue.front(); const std::string& name = t.name;
    if (gToastFrames == 0) gToastQueue.erase(gToastQueue.begin());
    overlayBegin(g);
    int W, H; overlaySize(W, H);
    float a = gToastFrames > 220 ? (240 - gToastFrames) / 20.f : gToastFrames < 20 ? gToastFrames / 20.f : 1.f;
    float x0 = W * 0.5f - 230, x1 = W * 0.5f + 230, y0 = 10, y1 = 66;
    overlayRect(x0, y0, x1, y1, 0x201010, 0.9f * a);
    overlayRect(x0, y0, x1, y0 + 3, 0x00CCFF, a);
    // icono a la izquierda (cuadro de 48 px): sprite del jefe o personaje, o un trofeo dibujado
    float ix = x0 + 8, iy = y0 + 6, is = 46;
    overlayRect(ix, iy, ix + is, iy + is, 0x403020, 0.9f * a);
    int spr = iconSprite(g, t.icon);
    if (logroIconDraw(g, t.id, true, ix + 1, iy + 1, is - 2, a)) {
    } else if (spr >= 0 && spr < (int)g.sprites.size() && g.sprites[spr].w > 0 && g.sprites[spr].h > 0) {
        const RSprite& rs = g.sprites[spr];
        double sc = std::min((is - 4) / (double)rs.w, (is - 4) / (double)rs.h);
        double cx = ix + is * 0.5, cy = iy + is * 0.5;
        g.drawSpriteFrame(spr, 0, cx - (rs.w * 0.5 - rs.ox) * sc, cy - (rs.h * 0.5 - rs.oy) * sc, sc, sc, 0, 0xFFFFFF, a);
    } else {   // trofeo: copa, asas, pie y base en dorado
        uint32_t gold = 0x20B8F0;
        overlayRect(ix + 13, iy + 8, ix + 33, iy + 24, gold, a);
        overlayRect(ix + 8, iy + 10, ix + 13, iy + 18, gold, a); overlayRect(ix + 33, iy + 10, ix + 38, iy + 18, gold, a);
        overlayRect(ix + 20, iy + 24, ix + 26, iy + 32, gold, a);
        overlayRect(ix + 14, iy + 32, ix + 32, iy + 37, gold, a);
    }
    overlayText(g, x0 + 66, y0 + 8, "¡Logro desbloqueado!", 0.4, 0x00CCFF, a);
    overlayText(g, x0 + 66, y0 + 30, name, 0.45, 0xFFFFFF, a);
    return true;
}

int logrosTotal() { return kNumLogros; }
int logrosHechos() { load(); int n = 0; for (auto& l : kLogros) if (gHave.count(l.id) && gHave[l.id]) n++; return n; }
bool logroInfo(int k, std::string& name) { load(); if (k < 0 || k >= kNumLogros) return false; name = kLogros[k].nombre; return gHave.count(kLogros[k].id) && gHave[kLogros[k].id]; }

// ---- Pantalla de logros dentro del menu de opciones de la pausa (sustituye a "Full Screen", que en la Vita no hace
// nada). El juego sigue igual: al aceptar en esa fila cambia global.opt_screen; aqui se detecta el cambio, se deja
// como estaba y se abre la pantalla. Se dibuja despues de draw_option_menu_script (vm.cpp), en la zona del mapa, con
// la fuente del menu; mientras esta abierta el runner se queda con el mando (el juego espera, como en la pausa).
namespace {
bool gScreen = false; int gScrSel = 0, gScrTop = 0, gScrPulse = 0;
const int kCols = 8, kRows = 4;
int gVarOptScreen = -2, gVarMenuX = -1, gVarMenuY = -1, gVarShowMap = -1;
int varId(Game& g, const char* n) { for (size_t v = 0; v < g.d.varNames.size(); v++) if (g.d.varNames[v] == n) return (int)v; return -1; }
void initVars(Game& g) {
    if (gVarOptScreen != -2) return;
    gVarOptScreen = varId(g, "opt_screen"); gVarMenuX = varId(g, "menu_x"); gVarMenuY = varId(g, "menu_y");
    gVarShowMap = varId(g, "show_map");
}
double instNum(Instance* p, int v) { if (!p || v < 0) return 0; const Value* x = p->vars.get(v); return x ? x->num() : 0; }
void sfx(Game& g, const char* name) {
    auto s = g.d.soundByName.find(name); if (s == g.d.soundByName.end()) return;
    auto& reg = builtinRegistry(); auto it = reg.find("audio_play_sound"); if (it == reg.end()) return;
    Value a[3] = {Value((double)s->second), Value(1.0), Value(0.0)}; Ctx c; it->second(g, c, a, 3);
}
// Textos en ingles (idioma original): <vsdata>/logros/nombres_en.txt, "id<TAB>nombre<TAB>descripcion", lo escribe
// tools/logros_iconos.py en la maquina del usuario (no va en el repositorio). Sin el archivo, en espanol.
std::map<std::string, std::pair<std::string, std::string>> gEn; bool gEnRead = false;
void readEn(Game& g) {
    if (gEnRead) return; gEnRead = true;
    std::string t; if (!bgio::read(g.dataDir + "/logros/nombres_en.txt", t)) return;
    size_t p = 0;
    while (p < t.size()) {
        size_t e = t.find('\n', p); if (e == std::string::npos) e = t.size();
        std::string l = t.substr(p, e - p); p = e + 1;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        size_t a = l.find('\t'), b = a == std::string::npos ? a : l.find('\t', a + 1);
        if (b != std::string::npos) gEn[l.substr(0, a)] = {l.substr(a + 1, b - a - 1), l.substr(b + 1)};
    }
}
void texts(Game& g, int k, std::string& nm, std::string& desc) {
    nm = kLogros[k].nombre; desc = kLogros[k].desc;
    if (gMods.lang == 1) return;
    readEn(g); auto it = gEn.find(kLogros[k].id); if (it != gEn.end()) { nm = it->second.first; desc = it->second.second; }
}
bool have(int k) { auto it = gHave.find(kLogros[k].id); return it != gHave.end() && it->second; }
void rect(float x1, float y1, float x2, float y2, uint32_t c, float a) {
    float xy[8] = {x1, y1, x2, y1, x2, y2, x1, y2}; float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0}; uint32_t cc[4] = {c, c, c, c};
    gfx::quad(-1, xy, uv, cc, a);
}
void frame(float x1, float y1, float x2, float y2, uint32_t c, float a, float t) {
    rect(x1, y1, x2, y1 + t, c, a); rect(x1, y2 - t, x2, y2, c, a); rect(x1, y1, x1 + t, y2, c, a); rect(x2 - t, y1, x2, y2, c, a);
}
}  // namespace
// Icono de un logro en el cuadro (x, y, lado): imagen de Steam, si no el sprite del juego, si no un trofeo
static void iconAny(Game& g, int k, bool on, float x, float y, float s, float a) {
    if (logroIconDraw(g, kLogros[k].id, on, x, y, s, a)) return;
    int spr = iconSprite(g, kLogros[k].icon);
    uint32_t col = on ? 0xFFFFFF : 0x505050;
    if (spr >= 0 && spr < (int)g.sprites.size() && g.sprites[spr].w > 0 && g.sprites[spr].h > 0) {
        const RSprite& rs = g.sprites[spr];
        double sc = std::min((s - 4) / (double)rs.w, (s - 4) / (double)rs.h);
        double cx = x + s * 0.5, cy = y + s * 0.5;
        g.drawSpriteFrame(spr, 0, cx - (rs.w * 0.5 - rs.ox) * sc, cy - (rs.h * 0.5 - rs.oy) * sc, sc, sc, 0, col, a);
        return;
    }
    uint32_t gold = on ? 0x20B8F0 : 0x505050; float u = s / 46.f;
    rect(x + 13 * u, y + 8 * u, x + 33 * u, y + 24 * u, gold, a);
    rect(x + 8 * u, y + 10 * u, x + 13 * u, y + 18 * u, gold, a); rect(x + 33 * u, y + 10 * u, x + 38 * u, y + 18 * u, gold, a);
    rect(x + 20 * u, y + 24 * u, x + 26 * u, y + 32 * u, gold, a);
    rect(x + 14 * u, y + 32 * u, x + 32 * u, y + 37 * u, gold, a);
}

bool logroScreenOpen() { return gScreen; }
// Juego en ingles (sin lang/es.txt): la fila "Full Screen" de las opciones y sus textos de ayuda pasan a ser los logros
const std::string* logroMenuAlias(const std::string& s) {
    if (gMods.lang == 1 || s.size() < 11 || (s[0] != 'F' && s[0] != 'T' && s[0] != 'A')) return nullptr;
    static const std::string fs = "Full Screen", tg = "Toggle fullscreen On/Off.", ad = "Adjust Controls/Sound/Fullscreen or quit game.#View Map of current Zone.";
    static const std::string fs2 = "Achievements", tg2 = "View the achievements you have and the ones left.",
                             ad2 = "Adjust Controls/Sound, view Achievements or quit game.#View Map of current Zone.";
    if (s == fs) return &fs2; if (s == tg) return &tg2; if (s == ad) return &ad2;
    return nullptr;
}
void logroScreenStep(Game& g) {
    initVars(g);
    static int last = -1000; static int lastRoom = -1;
    if (gVarOptScreen < 0) return;
    int v = g.globals.count(gVarOptScreen) ? (int)g.globals[gVarOptScreen].num() : 0;
    static const std::string none;
    const std::string& rn = g.room >= 0 ? g.d.rooms[g.room].name : none;
    bool menu = rn == "status_room" || rn == "start_game";
    if (gScreen && !menu) gScreen = false;
    if (last != -1000 && v != last && g.room == lastRoom && menu) {
        g.globals[gVarOptScreen] = Value((double)last); v = last;   // la opcion de pantalla completa no cambia
        load(); gScreen = true; gScrSel = 0; gScrTop = 0;
        logf("[logros] pantalla de logros (%d de %d)\n", logrosHechos(), kNumLogros);
    }
    if (const char* e = std::getenv("VS_LOGROSCREEN")) {   // pruebas: abre la pantalla al llegar a la pagina de opciones
        static bool done = false; static int enterFrame = -1;
        if (rn != "status_room") enterFrame = -1; else if (enterFrame < 0) enterFrame = g.frame;
        if (!done && enterFrame >= 0 && g.frame - enterFrame > 90) {
            done = true; load(); gScreen = std::atoi(e) >= 0; gScrSel = std::max(0, std::atoi(e));   // -1: solo ir a la pagina
            gScrTop = std::max(0, gScrSel / kCols - kRows + 1);
            auto o = g.d.objByName.find("general_control"); int vp = varId(g, "menu_page");
            if (o != g.d.objByName.end() && vp >= 0) for (Instance* p : g.instancesOf(o->second)) p->vars[vp] = Value(5.0);
        }
    }
    last = v; lastRoom = g.room;
}
bool logroScreenInput(Game& g, uint32_t b, uint32_t pr) {
    if (!gScreen) return false;
    static int rep = 0; uint32_t dirs = b & 15, act = pr & 15;
    if (dirs) { if (++rep > 20 && rep % 5 == 0) act |= dirs; } else rep = 0;
    int s = gScrSel;
    if (act & 1) s -= kCols;
    if (act & 2) s += kCols;
    if (act & 4) s -= 1;
    if (act & 8) s += 1;
    if (pr & 128) s -= kCols * kRows;
    if (pr & 256) s += kCols * kRows;
    s = std::max(0, std::min(kNumLogros - 1, s));
    if (s != gScrSel) { gScrSel = s; sfx(g, "sfx_cursor"); }
    int row = gScrSel / kCols;
    if (row < gScrTop) gScrTop = row;
    if (row >= gScrTop + kRows) gScrTop = row - kRows + 1;
    if (pr & (16 | 32 | 64)) { gScreen = false; sfx(g, "sfx_cancel"); }
    return true;
}
// Dibujo en coordenadas del menu (self = general_control, ya con la camara de la vista)
void logroScreenBeforeMenu(Game& g, Instance* self) {
    initVars(g);
    if (gScreen && self && gVarShowMap >= 0) self->vars[gVarShowMap] = Value(0.0);   // sin mapa debajo
}
void logroScreenDraw(Game& g, Instance* self) {
    if (!gScreen || !self) return;
    gScrPulse++;
    float mx = (float)instNum(self, gVarMenuX), my = (float)instNum(self, gVarMenuY);
    int ha = g.drawHalign, va = g.drawValign; g.drawHalign = 0; g.drawValign = 0;
    const uint32_t navy = 0x1C1008, blue = 0xFF9A3C, dblue = 0x80481C, yel = 0x00CCFF, wht = 0xFFFFFF, gry = 0x909090, grn = 0x40E080;
    // en la pausa ocupa el sitio del mapa (las opciones siguen a la derecha); en el menu principal (menu_options 1,
    // opciones corridas a la izquierda) tapa todo el menu
    static int vOpt = varId(g, "menu_options");
    bool tmenu = instNum(self, vOpt) == 1;
    float x1 = mx + (tmenu ? 60 : 34), y1 = my + 74, x2 = mx + (tmenu ? 740 : 562), y2 = my + 512;
    rect(x1, y1, x2, y2, navy, 1);
    frame(x1, y1, x2, y2, blue, 1, 2); frame(x1 + 5, y1 + 5, x2 - 5, y2 - 5, dblue, 1, 1);
    int n = logrosHechos();
    std::string title = gMods.lang == 1 ? "Logros" : "Achievements";
    g.drawText(x1 + 22, y1 + 14, title, 0.6, 0.6, 0, yel, yel, wht, wht, 1, -1, -1);
    g.drawHalign = 2;
    g.drawText(x2 - 22, y1 + 20, std::to_string(n) + " / " + std::to_string(kNumLogros), 0.45, 0.45, 0, yel, yel, wht, wht, 1, -1, -1);
    g.drawHalign = 0;
    rect(x1 + 18, y1 + 48, x2 - 18, y1 + 50, dblue, 1);
    // cuadricula de iconos
    const float cell = 60, isz = 52, gx = x1 + (x2 - x1 - kCols * cell - 4) * 0.5f, gy = my + 136;
    for (int r = 0; r < kRows; r++) for (int c = 0; c < kCols; c++) {
        int k = (gScrTop + r) * kCols + c; if (k >= kNumLogros) break;
        float x = gx + c * cell, y = gy + r * cell; bool on = have(k);
        rect(x - 2, y - 2, x + isz + 2, y + isz + 2, on ? 0x2A6AA0 : 0x302018, 1);
        iconAny(g, k, on, x, y, isz, on ? 1.f : 0.85f);
        if (k == gScrSel) { float p = 0.6f + 0.4f * (float)std::abs(((gScrPulse / 2) % 40) - 20) / 20.f; frame(x - 5, y - 5, x + isz + 5, y + isz + 5, yel, p, 3); }
    }
    int rows = (kNumLogros + kCols - 1) / kCols;   // barra de desplazamiento
    float sbx = gx + kCols * cell + 4, sbh = kRows * cell - 8;
    rect(sbx, gy, sbx + 4, gy + sbh, dblue, 1);
    rect(sbx, gy + sbh * gScrTop / rows, sbx + 4, gy + sbh * (gScrTop + kRows) / rows, blue, 1);
    // detalle del elegido
    float dy = gy + kRows * cell + 10;
    rect(x1 + 18, dy - 4, x2 - 18, dy - 2, dblue, 1);
    bool on = have(gScrSel); std::string nm, desc; texts(g, gScrSel, nm, desc);
    rect(gx - 2, dy + 8, gx + 66, dy + 76, on ? 0x2A6AA0 : 0x302018, 1);
    iconAny(g, gScrSel, on, gx, dy + 10, 64, 1);
    float tx = gx + 82, tw = x2 - 24 - tx;
    uint32_t nc = on ? yel : wht;
    g.drawText(tx, dy + 6, nm, 0.45, 0.45, 0, nc, nc, wht, wht, 1, -1, tw / 0.45);
    g.drawText(tx, dy + 34, desc, 0.36, 0.36, 0, wht, wht, wht, wht, 0.9, -1, tw / 0.36);
    std::string est = gMods.lang == 1 ? (on ? "Desbloqueado" : "Bloqueado") : (on ? "Unlocked" : "Locked");
    uint32_t ec = on ? grn : gry;
    g.drawText(tx, dy + 84, est, 0.36, 0.36, 0, ec, ec, ec, ec, 1, -1, -1);
    g.drawHalign = 2;
    g.drawText(x2 - 22, dy + 84, gMods.lang == 1 ? "L/R: página   O: volver" : "L/R: page   O: back", 0.32, 0.32, 0, gry, gry, gry, gry, 1, -1, -1);
    g.drawHalign = ha; g.drawValign = va;
}

VS_FN(steam_initialised) { static const bool off = std::getenv("VS_NOSTEAM") != nullptr; return Value(off ? 0.0 : 1.0); }   // VS_NOSTEAM: pruebas
VS_FN(steam_stats_ready) { return Value(1.0); }
VS_FN(steam_get_achievement) { load(); auto it = gHave.find(a[0].toString()); return Value(it != gHave.end() && it->second ? 1.0 : 0.0); }
VS_FN(steam_set_achievement) {
    load();
    std::string id = a[0].toString();
    if (gHave[id]) return Value();
    gHave[id] = 1; save();
    const char* nm = nombre(id);
    std::string msg = std::string("Logro desbloqueado: ") + (nm ? nm : id.c_str());
    logf("[logro] %s (%s)\n", msg.c_str(), id.c_str());
    logrosToastPush(nm ? nm : id, icono(id), id);
    return Value();
}
VS_FN(steam_get_stat_int) { load(); auto it = gStats.find(a[0].toString()); return Value(it != gStats.end() ? (double)it->second : 0.0); }
VS_FN(steam_set_stat_int) {
    load();
    std::string k = a[0].toString(); int v = a[1].ival();
    if (gStats[k] != v) { gStats[k] = v; save(); }
    return Value();
}
} // namespace vs
