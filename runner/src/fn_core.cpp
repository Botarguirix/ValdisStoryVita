// Funciones builtin GML (no graficas) usadas por Valdis Story.
#include "runtime.h"
#include "gfx.h"
#include "bgio.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cstdio>

namespace vs {

extern int gWinW, gWinH;
static const double NOONE = -4;

// ------------------------------------------------------------------ matematicas / cadenas
VS_FN(ceil) { return Value(std::ceil(a[0].num())); }
VS_FN(floor) { return Value(std::floor(a[0].num())); }
VS_FN(round) { double v = a[0].num(); double r = std::nearbyint(v); return Value(r); } // banker's como GMS
VS_FN(random) { return Value(g.random(a[0].num())); }
VS_FN(choose) { if (n <= 0) return Value(); return a[(int)g.random(n) % n]; }
VS_FN(point_direction) {
    double dx = a[2].num() - a[0].num(), dy = a[3].num() - a[1].num();
    if (dx == 0 && dy == 0) return Value(0.0);
    double d = std::atan2(-dy, dx) * 180.0 / M_PI; if (d < 0) d += 360; return Value(d);
}
VS_FN(point_distance) { double dx = a[2].num() - a[0].num(), dy = a[3].num() - a[1].num(); return Value(std::sqrt(dx * dx + dy * dy)); }
VS_FN(make_color_rgb) { return Value((double)((a[0].ival() & 255) | ((a[1].ival() & 255) << 8) | ((a[2].ival() & 255) << 16))); }

VS_FN(string) { return Value::str(a[0].toString()); }
VS_FN(string_length) { return Value((double)a[0].toString().size()); }
VS_FN(string_copy) {
    std::string s = a[0].toString(); int idx = a[1].ival() - 1, cnt = a[2].ival();
    if (idx < 0) idx = 0;
    if (idx >= (int)s.size() || cnt <= 0) return Value::str("");
    return Value::str(s.substr(idx, cnt));
}
VS_FN(string_repeat) { std::string s = a[0].toString(), r; int k = a[1].ival(); for (int i = 0; i < k; i++) r += s; return Value::str(r); }
VS_FN(string_replace) {
    std::string s = a[0].toString(), f = a[1].toString(), r = a[2].toString();
    if (f.empty()) return Value::str(s);
    size_t p = s.find(f); if (p != std::string::npos) s.replace(p, f.size(), r);
    return Value::str(s);
}

// ------------------------------------------------------------------ scripts
VS_FN(script_execute) { return g.runScript(a[0].ival(), c, a + 1, n - 1); }

// ------------------------------------------------------------------ instancias
VS_FN(instance_create) {
    Instance* i = g.createInstance(a[0].num(), a[1].num(), a[2].ival(), true);
    return Value(i ? (double)i->id : NOONE);
}
VS_FN(instance_destroy) { g.destroyInstance(c.self); return Value(); }
VS_FN(instance_exists) {
    int t = a[0].ival();
    if (t >= 100000) return Value(g.findInstance(t) ? 1.0 : 0.0);
    if (t == -1) return Value(c.self && c.self->alive ? 1.0 : 0.0);
    if (t == -2) return Value(c.other && c.other->alive ? 1.0 : 0.0);
    return Value(g.firstOf(t) ? 1.0 : 0.0);
}
VS_FN(instance_number) {
    int cnt = 0; g.eachTarget(a[0].num(), c, [&](Instance*) { cnt++; return false; }); return Value((double)cnt);
}
VS_FN(instance_nearest) {
    std::vector<Instance*> v; g.collectTargets(a[2].num(), c, v);
    double best = 1e300; Instance* bi = nullptr; double x = a[0].num(), y = a[1].num();
    for (Instance* i : v) { double d = (i->x - x) * (i->x - x) + (i->y - y) * (i->y - y); if (d < best) { best = d; bi = i; } }
    return Value(bi ? (double)bi->id : NOONE);
}
VS_FN(instance_position) {
    double px = a[0].num(), py = a[1].num(); int found = -1;
    g.eachTarget(a[2].num(), c, [&](Instance* i) { if (g.pointInInstance(i, px, py)) { found = i->id; return true; } return false; });
    return found >= 0 ? Value((double)found) : Value(NOONE);
}
// ---- rejilla espacial para consultas de colision contra objetos con muchas instancias
// En las salas grandes (outer_slum_2/6: ~330 bloques) el Alarm 0 de cada bloque hace ~30 position_meeting /
// collision_rectangle contra los demas bloques: >1 millon de comprobaciones, 10 ms en PC y ~650 ms en la Vita.
// La rejilla devuelve los candidatos cuya caja (ampliada 2 px) toca el punto/rectangulo y se prueban EN EL MISMO
// ORDEN que eachTarget (indice en instancesOf), asi el resultado es identico. Se reconstruye si cambia la epoca de
// geometria (runtime.cpp) o la lista del objeto.
extern uint64_t gGeomEpoch;
namespace {
struct CollGrid {
    int obj = -1; uint64_t epoch = 0; const Instance* first = nullptr; size_t n = 0; uint64_t useTick = 0;
    double ox = 0, oy = 0; int gw = 0, gh = 0; static constexpr double kCell = 64;
    std::vector<std::vector<int>> cells;   // indices en instancesOf
    std::vector<int> always;               // sin mascara o enormes: se prueban siempre
};
CollGrid gGrids[4];
uint64_t gGridTick = 0;
std::vector<int> gCand;
constexpr size_t kGridMin = 32;

CollGrid* gridFor(Game& g, int obj) {
    const auto& list = g.instancesOf(obj);
    if (list.size() < kGridMin) return nullptr;
    CollGrid* best = &gGrids[0];
    for (auto& cg : gGrids) {
        if (cg.obj == obj) { best = &cg; break; }
        if (cg.useTick < best->useTick) best = &cg;
    }
    CollGrid& G = *best;
    G.useTick = ++gGridTick;
    if (G.obj == obj && G.epoch == gGeomEpoch && G.n == list.size() && G.first == list[0]) return &G;
    G.obj = obj; G.epoch = gGeomEpoch; G.n = list.size(); G.first = list[0];
    G.always.clear();
    std::vector<double> bb(list.size() * 4);
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    std::vector<char> ok(list.size(), 0);
    for (size_t k = 0; k < list.size(); k++) {
        Instance* i = list[k];
        int m = g.maskOf(i);
        if (m < 0 || m >= (int)g.sprites.size()) { G.always.push_back((int)k); continue; }
        double l, t, r, b; g.bbox(i, l, t, r, b);
        l -= 2; t -= 2; r += 2; b += 2;
        if (!(r >= l && b >= t) || r - l > 8 * CollGrid::kCell || b - t > 8 * CollGrid::kCell) { G.always.push_back((int)k); continue; }
        bb[k * 4] = l; bb[k * 4 + 1] = t; bb[k * 4 + 2] = r; bb[k * 4 + 3] = b; ok[k] = 1;
        minx = std::min(minx, l); miny = std::min(miny, t); maxx = std::max(maxx, r); maxy = std::max(maxy, b);
    }
    if (minx > maxx) { G.gw = G.gh = 0; G.cells.clear(); return &G; }
    G.ox = minx; G.oy = miny;
    G.gw = std::min(1024, (int)((maxx - minx) / CollGrid::kCell) + 1);
    G.gh = std::min(1024, (int)((maxy - miny) / CollGrid::kCell) + 1);
    G.cells.assign((size_t)G.gw * G.gh, {});
    for (size_t k = 0; k < list.size(); k++) {
        if (!ok[k]) continue;
        int x0 = (int)((bb[k * 4] - G.ox) / CollGrid::kCell), x1 = std::min(G.gw - 1, (int)((bb[k * 4 + 2] - G.ox) / CollGrid::kCell));
        int y0 = (int)((bb[k * 4 + 1] - G.oy) / CollGrid::kCell), y1 = std::min(G.gh - 1, (int)((bb[k * 4 + 3] - G.oy) / CollGrid::kCell));
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) G.cells[(size_t)y * G.gw + x].push_back((int)k);
    }
    return &G;
}

// candidatos (indices ordenados) cuyo rectangulo puede tocar [l,r]x[t,b]; false = usar el recorrido normal
bool gridCandidates(CollGrid& G, double l, double t, double r, double b) {
    gCand.assign(G.always.begin(), G.always.end());
    if (G.gw > 0) {
        if (r < l) std::swap(l, r);
        if (b < t) std::swap(t, b);
        int x0 = (int)std::floor((l - G.ox) / CollGrid::kCell), x1 = (int)std::floor((r - G.ox) / CollGrid::kCell);
        int y0 = (int)std::floor((t - G.oy) / CollGrid::kCell), y1 = (int)std::floor((b - G.oy) / CollGrid::kCell);
        x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, G.gw - 1); y1 = std::min(y1, G.gh - 1);
        if ((long long)(x1 - x0 + 1) * (y1 - y0 + 1) > 64) return false;   // consulta enorme: no compensa
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            const auto& c = G.cells[(size_t)y * G.gw + x];
            gCand.insert(gCand.end(), c.begin(), c.end());
        }
    }
    std::sort(gCand.begin(), gCand.end());
    gCand.erase(std::unique(gCand.begin(), gCand.end()), gCand.end());
    return true;
}

// recorre los destinos como eachTarget, usando la rejilla si el destino es un objeto con muchas instancias
template <class F> bool eachTargetNear(Game& g, double target, Ctx& c, double l, double t, double r, double b, F&& f) {
    int tg = (int)target;
    if (tg >= 0 && tg < 100000 && tg < (int)g.isA.size()) {
        if (CollGrid* G = gridFor(g, tg)) {
            if (gridCandidates(*G, l, t, r, b)) {
                const auto& list = g.instancesOf(tg);
                for (int k : gCand) { Instance* p = list[k]; if (p->alive && f(p)) return true; }
                return false;
            }
        }
    }
    return g.eachTarget(target, c, f);
}
} // namespace

VS_FN(position_meeting) {
    double px = a[0].num(), py = a[1].num();
    return Value(eachTargetNear(g, a[2].num(), c, px, py, px, py, [&](Instance* i) { return g.pointInInstance(i, px, py); }) ? 1.0 : 0.0);
}
VS_FN(collision_rectangle) {
    bool prec = a[5].truthy(), notme = a[6].truthy();
    double x1 = a[0].num(), y1 = a[1].num(), x2 = a[2].num(), y2 = a[3].num(); int found = -1;
    eachTargetNear(g, a[4].num(), c, x1, y1, x2, y2, [&](Instance* i) {
        if (notme && i == c.self) return false;
        if (g.rectInInstance(i, x1, y1, x2, y2, prec)) { found = i->id; return true; }
        return false;
    });
    return found >= 0 ? Value((double)found) : Value(NOONE);
}
static double rectDist(double l, double t, double r, double b, double l2, double t2, double r2, double b2) {
    double dx = std::max(0.0, std::max(l2 - r, l - r2)), dy = std::max(0.0, std::max(t2 - b, t - b2));
    return std::sqrt(dx * dx + dy * dy);
}
VS_FN(distance_to_object) {
    if (!c.self) return Value(0.0);
    std::vector<Instance*> v; g.collectTargets(a[0].num(), c, v);
    double l, t, r, b; g.bbox(c.self, l, t, r, b);
    double best = 1000000;
    for (Instance* i : v) { if (i == c.self) continue; double l2, t2, r2, b2; g.bbox(i, l2, t2, r2, b2); best = std::min(best, rectDist(l, t, r, b, l2, t2, r2, b2)); }
    return Value(best);
}
VS_FN(distance_to_point) {
    if (!c.self) return Value(0.0);
    double l, t, r, b; g.bbox(c.self, l, t, r, b);
    return Value(rectDist(l, t, r, b, a[0].num(), a[1].num(), a[0].num(), a[1].num()));
}
VS_FN(move_towards_point) {
    if (!c.self) return Value();
    double dx = a[0].num() - c.self->x, dy = a[1].num() - c.self->y;
    c.self->direction = std::atan2(-dy, dx) * 180.0 / M_PI; if (c.self->direction < 0) c.self->direction += 360;
    c.self->speed = a[2].num(); c.self->setSpeedDir();
    return Value();
}

// ------------------------------------------------------------------ rooms / juego
VS_FN(room_goto) {
    g.pendingRoom = a[0].ival();
    if (g.curCode >= 0) logf("[room_goto] %d desde %s\n", g.pendingRoom, g.d.codes[g.curCode].name.c_str());
    return Value();
}
VS_FN(room_set_persistent) { g.roomPersistOverride[a[0].ival()] = a[1].truthy(); if (a[0].ival() == g.room) g.roomPersistent = a[1].truthy(); return Value(); }
VS_FN(room_set_view) {
    int r = a[0].ival(), v = a[1].ival();
    if (r < 0 || r >= (int)g.d.rooms.size() || v < 0 || v > 7) return Value();
    auto& rm = g.d.rooms[r];
    if ((int)rm.views.size() <= v) rm.views.resize(v + 1, RoomView{});
    RoomView& rv = rm.views[v];
    rv.enabled = a[2].truthy(); rv.vx = a[3].ival(); rv.vy = a[4].ival(); rv.vw = a[5].ival(); rv.vh = a[6].ival();
    rv.px = a[7].ival(); rv.py = a[8].ival(); rv.pw = a[9].ival(); rv.ph = a[10].ival();
    rv.bx = a[11].ival(); rv.by = a[12].ival(); rv.hs = a[13].ival(); rv.vs = a[14].ival(); rv.obj = a[15].ival();
    rm.flags |= 1;
    return Value();
}
VS_FN(game_end) { g.endRequested = true; return Value(); }
VS_FN(game_restart) { g.restartRequested = true; return Value(); }
VS_FN(os_is_paused) { return Value(0.0); }
VS_FN(display_reset) { return Value(); }
VS_FN(window_get_fullscreen) { return Value(1.0); }
VS_FN(window_set_fullscreen) { return Value(); }
VS_FN(window_get_width) { return Value((double)gWinW); }
VS_FN(window_get_height) { return Value((double)gWinH); }
VS_FN(window_set_size) { gWinW = std::max(1, a[0].ival()); gWinH = std::max(1, a[1].ival()); return Value(); }
VS_FN(window_set_caption) { return Value(); }
VS_FN(highscore_add) { return Value(); }
VS_FN(highscore_value) { return Value(0.0); }

// ------------------------------------------------------------------ tiles
// Indices de tiles: antes tile_get_* y tile_layer_find recorrian todos los tiles de la sala en cada llamada (~33 us
// en la Vita; el Alarm 0 de cada bloque hace 2 tile_layer_find y 12 tile_get_*). Se invalidan con gTileEpoch (cambio
// de sala, tile_add, cambios de region/escala/posicion) y comprobando tamano y direccion del vector.
uint64_t gTileEpoch = 1;
namespace {
struct LayerGrid { double ox = 0, oy = 0; int gw = 0, gh = 0; std::vector<std::vector<int>> cells; std::vector<int> always; };
struct TileIndex {
    uint64_t epoch = 0; const Tile* data = nullptr; size_t n = 0;
    std::unordered_map<int, int> byId;
    std::unordered_map<int, LayerGrid> layers;   // se construyen al pedirlas
};
TileIndex gTI;
constexpr double kTileCell = 128;
void tileIndexCheck(Game& g) {
    if (gTI.epoch == gTileEpoch && gTI.data == g.tiles.data() && gTI.n == g.tiles.size()) return;
    gTI.epoch = gTileEpoch; gTI.data = g.tiles.data(); gTI.n = g.tiles.size();
    gTI.byId.clear(); gTI.layers.clear();
    for (size_t k = 0; k < g.tiles.size(); k++) gTI.byId.emplace(g.tiles[k].id, (int)k);   // emplace: gana el primero, como el recorrido lineal
}
bool tileHit(const Tile& t, int dep, double x, double y) {
    return t.depth == dep && x >= t.x && x < t.x + t.w * t.xscale && y >= t.y && y < t.y + t.h * t.yscale;
}
LayerGrid& layerGrid(Game& g, int dep) {
    auto it = gTI.layers.find(dep);
    if (it != gTI.layers.end()) return it->second;
    LayerGrid& L = gTI.layers[dep];
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    std::vector<int> ks;
    for (size_t k = 0; k < g.tiles.size(); k++) {
        const Tile& t = g.tiles[k];
        if (t.depth != dep) continue;
        double x1 = t.x + t.w * t.xscale, y1 = t.y + t.h * t.yscale;
        if (t.xscale <= 0 || t.yscale <= 0 || x1 - t.x > 8 * kTileCell || y1 - t.y > 8 * kTileCell) { L.always.push_back((int)k); continue; }
        ks.push_back((int)k);
        minx = std::min(minx, (double)t.x); miny = std::min(miny, (double)t.y); maxx = std::max(maxx, x1); maxy = std::max(maxy, y1);
    }
    if (ks.empty()) return L;
    L.ox = minx; L.oy = miny;
    L.gw = std::min(1024, (int)((maxx - minx) / kTileCell) + 1); L.gh = std::min(1024, (int)((maxy - miny) / kTileCell) + 1);
    L.cells.assign((size_t)L.gw * L.gh, {});
    for (int k : ks) {
        const Tile& t = g.tiles[k];
        int cx0 = (int)((t.x - L.ox) / kTileCell), cx1 = std::min(L.gw - 1, (int)((t.x + t.w * t.xscale - L.ox) / kTileCell));
        int cy0 = (int)((t.y - L.oy) / kTileCell), cy1 = std::min(L.gh - 1, (int)((t.y + t.h * t.yscale - L.oy) / kTileCell));
        for (int cy = cy0; cy <= cy1; cy++) for (int cx = cx0; cx <= cx1; cx++) L.cells[(size_t)cy * L.gw + cx].push_back(k);
    }
    return L;
}
} // namespace
static Tile* tileById(Game& g, int id) {
    tileIndexCheck(g);
    auto it = gTI.byId.find(id);
    if (it == gTI.byId.end()) return nullptr;
    Tile* t = &g.tiles[it->second];
    return t->id == id ? t : nullptr;
}
VS_FN(tile_add) {
    Tile t; t.id = g.nextTileId++; t.bg = a[0].ival(); t.sx = a[1].ival(); t.sy = a[2].ival(); t.w = a[3].ival(); t.h = a[4].ival();
    t.x = a[5].ival(); t.y = a[6].ival(); t.depth = a[7].ival();
    g.tiles.push_back(t); gTileEpoch++; return Value((double)t.id);
}
VS_FN(tile_exists) { return Value(tileById(g, a[0].ival()) ? 1.0 : 0.0); }
#define TILE_GET(fname, expr) VS_FN(fname) { Tile* t = tileById(g, a[0].ival()); return t ? Value((double)(expr)) : Value(-1.0); }
TILE_GET(tile_get_background, t->bg)
TILE_GET(tile_get_blend, t->blend)
TILE_GET(tile_get_height, t->h)
TILE_GET(tile_get_width, t->w)
TILE_GET(tile_get_left, t->sx)
TILE_GET(tile_get_top, t->sy)
TILE_GET(tile_get_xscale, t->xscale)
TILE_GET(tile_get_yscale, t->yscale)
TILE_GET(tile_get_y, t->y)
VS_FN(tile_set_blend) { if (Tile* t = tileById(g, a[0].ival())) t->blend = (uint32_t)a[1].num() & 0xFFFFFF; return Value(); }
VS_FN(tile_set_region) { if (Tile* t = tileById(g, a[0].ival())) { t->sx = a[1].ival(); t->sy = a[2].ival(); t->w = a[3].ival(); t->h = a[4].ival(); gTileEpoch++; } return Value(); }
VS_FN(tile_set_scale) { if (Tile* t = tileById(g, a[0].ival())) { t->xscale = a[1].num(); t->yscale = a[2].num(); gTileEpoch++; } return Value(); }
VS_FN(tile_layer_find) {
    int dep = a[0].ival(); double x = a[1].num(), y = a[2].num();
    tileIndexCheck(g);
    LayerGrid& L = layerGrid(g, dep);
    int best = -1;   // el primero en el orden del vector, como el recorrido lineal
    for (int k : L.always) if (tileHit(g.tiles[k], dep, x, y)) { best = k; break; }
    if (L.gw > 0) {
        int cx = (int)std::floor((x - L.ox) / kTileCell), cy = (int)std::floor((y - L.oy) / kTileCell);
        if (cx >= 0 && cy >= 0 && cx < L.gw && cy < L.gh)
            for (int k : L.cells[(size_t)cy * L.gw + cx]) { if (best >= 0 && k > best) break; if (tileHit(g.tiles[k], dep, x, y)) { best = k; break; } }
    }
    return best >= 0 ? Value((double)g.tiles[best].id) : Value(-1.0);
}
VS_FN(tile_layer_hide) { for (auto& t : g.tiles) if (t.depth == a[0].ival()) t.visible = false; return Value(); }
VS_FN(tile_layer_show) { for (auto& t : g.tiles) if (t.depth == a[0].ival()) t.visible = true; return Value(); }
VS_FN(tile_layer_shift) { for (auto& t : g.tiles) if (t.depth == a[0].ival()) { t.x += a[1].ival(); t.y += a[2].ival(); } gTileEpoch++; return Value(); }

// ------------------------------------------------------------------ sprites / backgrounds dinamicos
static std::string assetPath(Game& g, std::string f) {
    for (auto& ch : f) if (ch == '\\') ch = '/';
    return g.gameDir + "/" + f;
}
static std::unordered_map<std::string, std::pair<int, std::pair<int, int>>> gTexCache; // ruta -> handle,w,h

static int loadTexCached(Game& g, const std::string& path, int& w, int& h) {
    auto it = gTexCache.find(path);
    if (it != gTexCache.end()) { w = it->second.second.first; h = it->second.second.second; return it->second.first; }
    int hnd = gfx::loadDynamic(path, w, h);
    gTexCache[path] = {hnd, {w, h}};
    if (hnd == -1) return -1;
    (void)g;
    return hnd;
}

VS_FN(sprite_add) {
    std::string path = assetPath(g, a[0].toString());
    int frames = std::max(1, a[1].ival());
    RSprite s; s.name = a[0].toString();
    {   // tira mas ancha que el maximo de la GPU: un trozo de frames por textura (cacheado por ruta, como loadTexCached)
        struct Strip { std::vector<int> hnd, offs; int w, h, cw; };
        static std::unordered_map<std::string, Strip> strips;
        auto it = strips.find(path);
        if (it == strips.end()) {
            Strip st;
            if (!gfx::loadDynamicStrip(path, frames, st.hnd, st.offs, st.w, st.h, st.cw)) st.hnd.clear();   // vacio = no hace falta partirla
            it = strips.emplace(path, std::move(st)).first;
        }
        if (it != strips.end() && (int)it->second.hnd.size() == frames) {
            const Strip& st = it->second;
            s.w = st.w / frames; s.h = st.h; s.ox = a[4].ival(); s.oy = a[5].ival();
            s.bl = 0; s.bt = 0; s.br = s.w - 1; s.bb = s.h - 1; s.precise = false;
            for (int k = 0; k < frames; k++) {
                int tw, th; gfx::texSize(st.hnd[k], tw, th);
                SFrame f; f.tex = st.hnd[k]; f.texW = tw; f.texH = th; f.sx = st.offs[k]; f.sy = 0; f.sw = s.w; f.sh = s.h; f.tx = 0; f.ty = 0; f.bw = s.w; f.bh = s.h;
                s.frames.push_back(f);
            }
            g.sprites.push_back(std::move(s));
            return Value((double)(g.sprites.size() - 1));
        }
    }
    int w, h; int tex = loadTexCached(g, path, w, h);
    if (tex == -1) { g.warnOnce("sprite_add: falta " + path); return Value(-1.0); }
    s.w = w / frames; s.h = h; s.ox = a[4].ival(); s.oy = a[5].ival();
    s.bl = 0; s.bt = 0; s.br = s.w - 1; s.bb = s.h - 1; s.precise = false;
    for (int k = 0; k < frames; k++) {
        SFrame f; f.tex = tex; f.texW = w; f.texH = h; f.sx = k * s.w; f.sy = 0; f.sw = s.w; f.sh = s.h; f.tx = 0; f.ty = 0; f.bw = s.w; f.bh = s.h;
        s.frames.push_back(f);
    }
    g.sprites.push_back(std::move(s));
    return Value((double)(g.sprites.size() - 1));
}
VS_FN(sprite_assign) {
    int dst = a[0].ival(), src = a[1].ival();
    if (dst < 0 || src < 0 || dst >= (int)g.sprites.size() || src >= (int)g.sprites.size()) return Value();
    std::string name = g.sprites[dst].name;
    g.sprites[dst] = g.sprites[src]; g.sprites[dst].name = name;
    return Value();
}
VS_FN(sprite_delete) {
    int s = a[0].ival();
    if (s >= (int)g.d.sprites.size() && s < (int)g.sprites.size()) { g.sprites[s].valid = false; g.sprites[s].frames.clear(); }
    return Value();
}
VS_FN(sprite_collision_mask) {
    int s = a[0].ival(); if (s < 0 || s >= (int)g.sprites.size()) return Value();
    RSprite& r = g.sprites[s];
    int mode = a[2].ival();
    if (mode == 2) { r.bl = a[3].ival(); r.bt = a[4].ival(); r.br = a[5].ival(); r.bb = a[6].ival(); }
    else if (mode == 1) { r.bl = 0; r.bt = 0; r.br = r.w - 1; r.bb = r.h - 1; }
    r.kind = a[7].ival(); r.precise = r.kind == 0 && !r.masks.empty();
    return Value();
}
VS_FN(sprite_get_width) { int s = a[0].ival(); return Value(s >= 0 && s < (int)g.sprites.size() ? (double)g.sprites[s].w : 0.0); }
VS_FN(sprite_get_height) { int s = a[0].ival(); return Value(s >= 0 && s < (int)g.sprites.size() ? (double)g.sprites[s].h : 0.0); }
VS_FN(sprite_get_xoffset) { int s = a[0].ival(); return Value(s >= 0 && s < (int)g.sprites.size() ? (double)g.sprites[s].ox : 0.0); }
VS_FN(sprite_get_yoffset) { int s = a[0].ival(); return Value(s >= 0 && s < (int)g.sprites.size() ? (double)g.sprites[s].oy : 0.0); }

static bool loadBg(Game& g, const std::string& file, RBackground& b) {
    std::string path = assetPath(g, file);
    int w, h; int tex = loadTexCached(g, path, w, h);
    if (tex == -1) { g.warnOnce("background_add: falta " + path); return false; }
    b.name = file; b.w = w; b.h = h; b.valid = true;
    b.f = SFrame{}; b.f.tex = tex; b.f.texW = w; b.f.texH = h; b.f.sw = w; b.f.sh = h; b.f.bw = w; b.f.bh = h;
    return true;
}
VS_FN(background_add) {
    RBackground b; if (!loadBg(g, a[0].toString(), b)) return Value(-1.0);
    g.backgrounds.push_back(b); return Value((double)(g.backgrounds.size() - 1));
}
VS_FN(background_assign) {
    int dst = a[0].ival(), src = a[1].ival();
    if (dst < 0 || src < 0 || dst >= (int)g.backgrounds.size() || src >= (int)g.backgrounds.size()) return Value();
    std::string name = g.backgrounds[dst].name; g.backgrounds[dst] = g.backgrounds[src]; g.backgrounds[dst].name = name;
    return Value();
}
VS_FN(background_delete) {
    int b = a[0].ival(); if (b >= (int)g.d.backgrounds.size() && b < (int)g.backgrounds.size()) g.backgrounds[b].valid = false;
    return Value();
}
VS_FN(background_replace) {
    int b = a[0].ival(); if (b < 0 || b >= (int)g.backgrounds.size()) return Value();
    RBackground nb; if (loadBg(g, a[1].toString(), nb)) { nb.name = g.backgrounds[b].name; g.backgrounds[b] = nb; }
    return Value();
}

// ------------------------------------------------------------------ archivos / ini
std::string Game::savePath(const std::string& name) const {
    std::string n = name; for (auto& ch : n) if (ch == '\\') ch = '/';
    return saveDir + "/" + n;
}
static bool fileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

// Los archivos de guardado pasan por bgio (vista en memoria + escritura a disco en segundo plano).
VS_FN(file_exists) {
    std::string f = a[0].toString();
    return Value(bgio::exists(g.savePath(f)) || fileExists(assetPath(g, f)) ? 1.0 : 0.0);
}
VS_FN(file_delete) { return Value(bgio::remove(g.savePath(a[0].toString())) ? 1.0 : 0.0); }
void (*gRenameHook)(Game&, const std::string&, const std::string&) = nullptr;   // bossrush.cpp: graba el estado de cada jefe
VS_FN(file_rename) {
    std::string from = a[0].toString(), to = a[1].toString();
    bool ok = bgio::rename(g.savePath(from), g.savePath(to));
    if (ok && gRenameHook) gRenameHook(g, from, to);
    return Value(ok ? 1.0 : 0.0);
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static void iniFlush(Game& g) {
    if (!g.iniOpen || !g.iniDirty) return;
    std::string o; o.reserve(64 * 1024);
    for (auto& [sec, kv] : g.ini) {
        o += "["; o += sec; o += "]\r\n";
        for (auto& [k, v] : kv) { o += k; o += "=\""; o += v; o += "\"\r\n"; }
        o += "\r\n";
    }
    bgio::write(g.iniPath, std::move(o));
    g.iniDirty = false;
}
VS_FN(ini_open) {
    iniFlush(g);
    g.ini.clear(); g.iniPath = g.savePath(a[0].toString()); g.iniOpen = true; g.iniDirty = false;
    std::string text; bgio::read(g.iniPath, text);
    std::istringstream f(text);
    std::string line, sec;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') { size_t e = line.find(']'); sec = line.substr(1, e == std::string::npos ? std::string::npos : e - 1); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        g.ini[sec][k] = v;
    }
    return Value();
}
VS_FN(ini_close) { iniFlush(g); g.iniOpen = false; g.ini.clear(); return Value::str(""); }
static const std::string* iniGet(Game& g, const Value& s, const Value& k) {
    auto it = g.ini.find(s.toString()); if (it == g.ini.end()) return nullptr;
    auto jt = it->second.find(k.toString()); if (jt == it->second.end()) return nullptr;
    return &jt->second;
}
VS_FN(ini_read_real) { const std::string* v = iniGet(g, a[0], a[1]); return v ? Value(std::atof(v->c_str())) : a[2]; }
void langTranslateRead(std::string& s);   // lang.cpp: textos guardados en la partida, al idioma activo
VS_FN(ini_read_string) { const std::string* v = iniGet(g, a[0], a[1]); std::string r = v ? *v : a[2].toString(); langTranslateRead(r); return Value::str(r); }
VS_FN(ini_write_real) {
    char b[64]; std::snprintf(b, sizeof b, "%.6f", a[2].num());
    g.ini[a[0].toString()][a[1].toString()] = b; g.iniDirty = true; return Value();
}
VS_FN(ini_write_string) { g.ini[a[0].toString()][a[1].toString()] = a[2].toString(); g.iniDirty = true; return Value(); }

// ------------------------------------------------------------------ teclado
static int key(const Value& v) { int k = v.ival(); return k < 0 ? 0 : (k > 255 ? 255 : k); }
VS_FN(keyboard_check) { return Value(g.in.down[key(a[0])] ? 1.0 : 0.0); }
VS_FN(keyboard_check_pressed) { return Value(g.in.pressed[key(a[0])] ? 1.0 : 0.0); }
VS_FN(keyboard_check_released) { return Value(g.in.released[key(a[0])] ? 1.0 : 0.0); }
VS_FN(keyboard_key_press) {
    int k = key(a[0]);
    if (!g.in.down[k]) { g.in.pressed[k] = true; g.in.lastKey = k; }
    g.in.virt[k] = true; g.in.down[k] = true;
    return Value();
}
VS_FN(keyboard_key_release) {
    int k = key(a[0]);
    if (g.in.down[k] && !g.in.phys[k]) g.in.released[k] = true;
    g.in.virt[k] = false; if (!g.in.phys[k]) g.in.down[k] = false;
    return Value();
}

// ------------------------------------------------------------------ mando (slot 0)
static int gpBtn(const Value& v) { int b = v.ival() - 32769; return (b >= 0 && b < 16) ? b : -1; }
VS_FN(gamepad_get_device_count) { return Value(4.0); }
VS_FN(gamepad_is_connected) { return Value(a[0].ival() == 0 && g.in.gpConnected ? 1.0 : 0.0); }
VS_FN(gamepad_button_check) { int b = gpBtn(a[1]); return Value(a[0].ival() == 0 && b >= 0 && g.in.gpBtn[b] ? 1.0 : 0.0); }
VS_FN(gamepad_button_value) { int b = gpBtn(a[1]); return Value(a[0].ival() == 0 && b >= 0 && g.in.gpBtn[b] ? 1.0 : 0.0); }
VS_FN(gamepad_axis_value) {
    int ax = a[1].ival() - 32785; if (a[0].ival() != 0 || ax < 0 || ax > 3) return Value(0.0);
    return Value((double)g.in.gpAxis[ax]);
}
VS_FN(gamepad_get_button_threshold) { return Value((double)g.in.gpThreshold); }
VS_FN(gamepad_set_button_threshold) { g.in.gpThreshold = (float)a[1].num(); return Value(); }
VS_FN(joystick_exists) { return Value(0.0); }
VS_FN(joystick_check_button) { return Value(0.0); }
VS_FN(joystick_pov) { return Value(-1.0); }
VS_FN(joystick_xpos) { return Value(0.0); }
VS_FN(joystick_ypos) { return Value(0.0); }

// ------------------------------------------------------------------ ds / json
VS_FN(ds_list_destroy) { int i = a[0].ival(); if (i >= 0 && i < (int)g.dsLists.size()) g.dsLists[i].reset(); return Value(); }
VS_FN(ds_list_size) { int i = a[0].ival(); return Value(i >= 0 && i < (int)g.dsLists.size() && g.dsLists[i] ? (double)g.dsLists[i]->size() : 0.0); }
VS_FN(ds_list_find_value) {
    int i = a[0].ival(), k = a[1].ival();
    if (i < 0 || i >= (int)g.dsLists.size() || !g.dsLists[i] || k < 0 || k >= (int)g.dsLists[i]->size()) return Value::undef();
    return (*g.dsLists[i])[k];
}
VS_FN(ds_map_destroy) { int i = a[0].ival(); if (i >= 0 && i < (int)g.dsMaps.size()) g.dsMaps[i].reset(); return Value(); }
VS_FN(ds_map_exists) {
    int i = a[0].ival(); if (i < 0 || i >= (int)g.dsMaps.size() || !g.dsMaps[i]) return Value(0.0);
    return Value(g.dsMaps[i]->count(a[1].toString()) ? 1.0 : 0.0);
}
VS_FN(ds_map_find_value) {
    int i = a[0].ival(); if (i < 0 || i >= (int)g.dsMaps.size() || !g.dsMaps[i]) return Value::undef();
    auto it = g.dsMaps[i]->find(a[1].toString()); return it == g.dsMaps[i]->end() ? Value::undef() : it->second;
}
VS_FN(json_decode) {
    // Solo se usa con respuestas de Steam (leaderboards): sin Steam no llega nada. Devuelve un mapa vacio.
    g.dsMaps.push_back(std::make_unique<std::map<std::string, Value>>());
    return Value((double)(g.dsMaps.size() - 1));
}

// ------------------------------------------------------------------ Steam (sin Steam en Vita)
VS_FN(steam_is_cloud_enabled_for_account) { return Value(0.0); }
VS_FN(steam_is_cloud_enabled_for_app) { return Value(0.0); }
VS_FN(steam_is_screenshot_requested) { return Value(0.0); }
VS_FN(steam_get_quota_total) { return Value(0.0); }
VS_FN(steam_file_persisted) { return Value(0.0); }
VS_FN(steam_file_share) { return Value(-1.0); }
VS_FN(steam_file_write_file) { return Value(0.0); }
VS_FN(steam_send_screenshot) { return Value(); }
VS_FN(steam_upload_score) { return Value(-1.0); }
VS_FN(steam_download_scores) { return Value(-1.0); }
VS_FN(steam_download_friends_scores) { return Value(-1.0); }

// Para la fase de colisiones de Game::step (runtime.cpp): indices (en instancesOf(obj), ordenados) de las instancias
// cuya caja puede tocar [l,r]x[t,b]. false = no aplica (pocas instancias o consulta enorme): recorrer todas.
bool collCandidatesFor(Game& g, int obj, double l, double t, double r, double b, std::vector<int>& out) {
    CollGrid* G = gridFor(g, obj);
    if (!G || !gridCandidates(*G, l, t, r, b)) return false;
    out.assign(gCand.begin(), gCand.end());
    return true;
}

} // namespace vs
