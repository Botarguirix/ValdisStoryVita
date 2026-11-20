#include "runtime.h"
#include "debugtour.h"
#include "gfx.h"
#include "bgio.h"
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstring>

namespace vs {
void langInit(Game& g); void langApply(Game& g, int lang);
extern int gSlowFnWatch; void watchDump(Game& g); extern bool gProfPhaseOnly;   // vm.cpp / mas abajo
void geomTouchObj(int obj);   // mas abajo: la geometria de las instancias de obj cambio
void stepGridsReset();        // mas abajo: olvida las rejillas de colision (cambio de sala)
#ifdef VS_HAVE_AOT
extern const uint64_t kAotHash; extern const unsigned kAotCount;
void aotFill(Game::AotFn* tab);
#endif

Game* gGame = nullptr;

void logf(const char* fmt, ...) {
    static std::string logPath;
    char buf[2048];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
#ifndef __vita__
    std::fputs(buf, stdout); std::fflush(stdout);
#endif
    if (gGame) {
        if (logPath.empty() && !gGame->saveDir.empty()) logPath = gGame->saveDir + "/vsrunner.log";
        if (!logPath.empty()) bgio::appendLog(logPath, buf); // en segundo plano: escribir en la tarjeta no bloquea el frame
    }
}

std::unordered_map<std::string, BuiltinFn>& builtinRegistry() {
    static std::unordered_map<std::string, BuiltinFn> r; return r;
}

static uint64_t nowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------- perfilado
double Game::profNow() { return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static const char* kPhase[] = {"begin step", "alarmas", "teclado", "step", "movimiento", "fuera room", "colisiones",
                               "end step", "animacion", "particulas+vistas+audio", "purga+cambio room", "draw"};

void Game::profDump() {
    if (!prof || profFrames == 0) return;
    double n = profFrames;
    logf("[prof] ===== %d frames, room %s (us por frame) =====\n", profFrames, d.rooms[room].name.c_str());
    for (int i = 0; i < 12; i++) logf("[prof] fase %-26s %8.0f\n", kPhase[i], profPhase[i] / n);
    std::vector<std::pair<double, int>> v;
    for (size_t i = 0; i < profCode.size(); i++) if (profCode[i] > 0) v.push_back({profCode[i], (int)i});
    std::sort(v.rbegin(), v.rend());
    for (size_t k = 0; k < v.size() && k < 30; k++)
        logf("[prof] code %8.0f us  %7.1f llamadas  %s\n", v[k].first / n, profCodeN[v[k].second] / n, d.codes[v[k].second].name.c_str());
    v.clear();
    for (size_t i = 0; i < profFn.size(); i++) if (profFn[i] > 0) v.push_back({profFn[i], (int)i});
    std::sort(v.rbegin(), v.rend());
    for (size_t k = 0; k < v.size() && k < 25; k++)
        logf("[prof] func %8.0f us  %7.1f llamadas  %s\n", v[k].first / n, profFnN[v[k].second] / n, d.functions[v[k].second].c_str());
    if (gProfPhaseOnly) watchDump(*this);   // acumulado de 600 frames (ms totales, no por frame)
    std::fill(profFn.begin(), profFn.end(), 0); std::fill(profCode.begin(), profCode.end(), 0);
    std::fill(profFnN.begin(), profFnN.end(), 0); std::fill(profCodeN.begin(), profCodeN.end(), 0);
    for (double& p : profPhase) p = 0;
    profFrames = 0;
}
void Game::perfFrame(double stepMs, double drawMs) {
    static int n = 0, lastRoom = -1; static double sSum = 0, dSum = 0, sMax = 0, dMax = 0; static uint64_t ins0 = 0, t0 = 0;
    if (!t0) { t0 = nowMs(); ins0 = insCount; }
    n++; sSum += stepMs; dSum += drawMs; sMax = std::max(sMax, stepMs); dMax = std::max(dMax, drawMs);
    gfx::Perf& p = gfx::perf();
    static double aSum = 0;
    aSum += audioMs;
    if (stepMs + drawMs > 200) {
        // causa del frame lento (etiqueta entre corchetes, segun las reglas del port)
        const char* tag; char cause[160];
        if (p.loadMs > (stepMs + drawMs) * 0.5) { tag = "[bandwidth-bound]"; std::snprintf(cause, sizeof cause, "lectura de %d texturas de la tarjeta en el hilo principal", p.loads); }
        else if (audioMs > (stepMs + drawMs) * 0.5) { tag = "[bandwidth-bound]"; std::snprintf(cause, sizeof cause, "apertura de %d sonidos en el hilo principal", audioCalls); }
        else { tag = "[CPU-bound]"; std::snprintf(cause, sizeof cause, "bloqueo del hilo principal (logica GML de un solo nucleo)"); }
        logf("[perf] %s frame lento %d room %s: %s | logica %.0f ms, render %.0f ms, texturas cargadas %d (%.0f ms), audio %d (%.0f ms), instancias %u\n",
             tag, frame, d.rooms[room].name.c_str(), cause, stepMs, drawMs, p.loads, p.loadMs, audioCalls, audioMs, (unsigned)insts.size());
    }
    audioMs = 0; audioCalls = 0;
    uint64_t now = nowMs();
    if (now - t0 >= 1000 || room != lastRoom) {
        double sec = (now - t0) / 1000.0, fps = n / std::max(sec, 0.001);
        double lg = sSum / n, rd = dSum / n, sw = p.swapMs / n, au = aSum / n;
        bool bad = fps < 58 || p.loads > 0 || p.uploadFails > 0;
        // que limita este segundo (presupuesto de 60 fps = 16,7 ms por frame)
        const char* tag = "[OK]"; char cause[200] = "dentro del presupuesto de 16,7 ms";
        int gfree = gfx::gpuFreeMB();
        if (p.uploadFails > 0 || (gfree >= 0 && gfree < 24)) { tag = "[VMEM-bound]"; std::snprintf(cause, sizeof cause, "memoria de GPU agotada: %d texturas no cupieron, %d expulsadas, %d MB libres", p.uploadFails, p.evictions, gfree); }
        else if (p.loadMs > 100 || p.loadMs > 0.3 * (sSum + dSum)) { tag = "[bandwidth-bound]"; std::snprintf(cause, sizeof cause, "lectura de texturas de la tarjeta (%.0f ms este segundo)", p.loadMs); }
        else if (lg + rd > 16.7) { tag = "[CPU-bound]"; std::snprintf(cause, sizeof cause, "hilo principal en un solo nucleo: logica GML %.1f ms + dibujo %.1f ms > 16,7 ms", lg, rd); }
        else if (fps < 58 && sw > 16.7 - lg - rd + 2) { tag = "[GPU-bound]"; std::snprintf(cause, sizeof cause, "espera a la GPU %.1f ms/frame (relleno de pixeles o capas de transparencia)", sw); }
        if (au > 1.0) std::snprintf(cause + std::strlen(cause), sizeof cause - std::strlen(cause), "; audio %.1f ms/frame en el hilo principal (objetivo <1 ms)", au);
        if (perfVerbose || bad)
            logf("[perf] %s %s fps %.0f | %s | logica %.1f/%.0f ms | render %.1f/%.0f ms | espera gpu %.1f ms | %llu ins/frame | draws %d | tex cargadas %d (%d en 2o plano; hilo ppal %.0f ms, subida %.0f; hilo cargador lectura %.0f, descompr. %.0f) sin listar a tiempo %d, espera %.0f ms, expulsadas %d fallos %d | gpu libre %d MB\n",
                 tag, d.rooms[room].name.c_str(), fps, cause, lg, sMax, rd, dMax, sw,
                 (unsigned long long)((insCount - ins0) / n), p.draws / n, p.loads, p.asyncLoads, p.loadMs, p.uploadMs, p.readMs, p.inflateMs, p.missing, p.waitMs, p.evictions,
                 p.uploadFails, gfree);
        // diagnostico del jugador (sprites invisibles en la Vita)
        {
            static int objPlayer = -2;
            if (objPlayer == -2) { objPlayer = -1; for (size_t o = 0; o < d.objects.size(); o++) if (d.objects[o].name == "player_wyatt") objPlayer = (int)o; }
            if (objPlayer >= 0) if (Instance* pl = firstOf(objPlayer)) {
                int spr = pl->sprite, page = -1, fr = 0;
                if (spr >= 0 && spr < (int)sprites.size() && !sprites[spr].frames.empty()) {
                    fr = ((int)pl->image_index % (int)sprites[spr].frames.size() + (int)sprites[spr].frames.size()) % (int)sprites[spr].frames.size();
                    page = sprites[spr].frames[fr].tex;
                }
                logf("[jugador] spr %d (%s) frame %d pagina %d estado %d | x %.0f y %.0f vista %.0f,%.0f | alfa %.2f visible %d blend %06x depth %.0f escala %.2f,%.2f\n",
                     spr, spr >= 0 && spr < (int)sprites.size() ? sprites[spr].name.c_str() : "-", fr, page, gfx::pageState(page),
                     pl->x, pl->y, views[0].xview, views[0].yview, pl->alpha, (int)pl->visible, pl->blend & 0xFFFFFF, pl->depth, pl->xscale, pl->yscale);
            }
        }
        n = 0; sSum = dSum = sMax = dMax = 0; aSum = 0; ins0 = insCount; t0 = now; lastRoom = room;
        p = gfx::Perf{};
    }
}

// ---------------------------------------------------------------- init
static const std::pair<const char*, int> kBuiltinVars[] = {
    {"x", BV_X}, {"y", BV_Y}, {"xprevious", BV_XPREV}, {"yprevious", BV_YPREV}, {"xstart", BV_XSTART}, {"ystart", BV_YSTART},
    {"hspeed", BV_HSPEED}, {"vspeed", BV_VSPEED}, {"speed", BV_SPEED}, {"direction", BV_DIRECTION},
    {"image_index", BV_IMAGE_INDEX}, {"image_speed", BV_IMAGE_SPEED}, {"image_xscale", BV_XSCALE}, {"image_yscale", BV_YSCALE},
    {"image_angle", BV_ANGLE}, {"image_alpha", BV_ALPHA}, {"image_blend", BV_BLEND}, {"image_number", BV_IMAGE_NUMBER},
    {"sprite_index", BV_SPRITE_INDEX}, {"mask_index", BV_MASK_INDEX}, {"depth", BV_DEPTH}, {"visible", BV_VISIBLE},
    {"persistent", BV_PERSISTENT}, {"object_index", BV_OBJECT_INDEX}, {"id", BV_ID}, {"alarm", BV_ALARM}, {"solid", BV_SOLID},
    {"bbox_left", BV_BBOX_LEFT}, {"bbox_right", BV_BBOX_RIGHT}, {"bbox_top", BV_BBOX_TOP}, {"bbox_bottom", BV_BBOX_BOTTOM},
    {"sprite_width", BV_SPRITE_WIDTH}, {"sprite_height", BV_SPRITE_HEIGHT},
    {"room", BV_ROOM}, {"room_width", BV_ROOM_WIDTH}, {"room_height", BV_ROOM_HEIGHT}, {"room_speed", BV_ROOM_SPEED},
    {"room_persistent", BV_ROOM_PERSISTENT}, {"view_visible", BV_VIEW_VISIBLE}, {"view_enabled", BV_VIEW_ENABLED},
    {"view_xview", BV_VIEW_XVIEW}, {"view_yview", BV_VIEW_YVIEW}, {"view_wview", BV_VIEW_WVIEW}, {"view_hview", BV_VIEW_HVIEW},
    {"view_xport", BV_VIEW_XPORT}, {"view_yport", BV_VIEW_YPORT}, {"view_wport", BV_VIEW_WPORT}, {"view_hport", BV_VIEW_HPORT},
    {"view_hborder", BV_VIEW_HBORDER}, {"view_vborder", BV_VIEW_VBORDER}, {"view_hspeed", BV_VIEW_HSPEED},
    {"view_vspeed", BV_VIEW_VSPEED}, {"view_object", BV_VIEW_OBJECT}, {"view_current", BV_VIEW_CURRENT},
    {"background_x", BV_BACKGROUND_X}, {"background_y", BV_BACKGROUND_Y}, {"background_color", BV_BACKGROUND_COLOR},
    {"background_visible", BV_BACKGROUND_VISIBLE}, {"background_index", BV_BACKGROUND_INDEX},
    {"keyboard_lastkey", BV_KEYBOARD_LASTKEY}, {"keyboard_lastchar", BV_KEYBOARD_LASTCHAR}, {"os_type", BV_OS_TYPE},
    {"async_load", BV_ASYNC_LOAD}, {"fps", BV_FPS}, {"current_time", BV_CURRENT_TIME},
    {"argument", BV_ARGUMENT}, {"argument_count", BV_ARGUMENT_COUNT}, {"instance_count", BV_INSTANCE_COUNT}, {"instance_id", BV_INSTANCE_ID},
    {"working_directory", BV_WORKING_DIRECTORY},
};

static SFrame frameFromTpag(const GameData& d, int tp) {
    SFrame f;
    if (tp < 0 || tp >= (int)d.tpages.size()) return f;
    const TPage& t = d.tpages[tp];
    f.tex = t.tex; f.texW = d.textures[t.tex].w; f.texH = d.textures[t.tex].h;
    f.sx = t.sx; f.sy = t.sy; f.sw = t.sw; f.sh = t.sh; f.tx = t.tx; f.ty = t.ty; f.bw = t.bw; f.bh = t.bh;
    return f;
}

bool Game::init(const std::string& dd, const std::string& gd, const std::string& sd, std::string& err) {
    gGame = this;
    gfx::gLogHook = [](const char* s) { logf("%s", s); };
    dataDir = dd; gameDir = gd; saveDir = sd;
    gfx::flushLog();   // despues de saber la carpeta de partidas (antes fijaba la ruta del registro a "/vsrunner.log")
    if (!d.load(dataDir + "/game.vsd", err)) return false;
    langInit(*this);   // idioma del menu de MODS (lang.cpp), antes de ejecutar nada
    rng = new std::mt19937(det ? detSeed : (uint32_t)nowMs());
    startMs = nowMs();

    // variables builtin
    varBuiltin.assign(d.varNames.size(), 0);
    std::unordered_map<std::string, int> bvByName;
    for (auto& p : kBuiltinVars) bvByName[p.first] = p.second;
    for (int i = 0; i < 16; i++) bvByName["argument" + std::to_string(i)] = BV_ARG0 + i;
    for (size_t i = 0; i < d.varNames.size(); i++) {
        auto it = bvByName.find(d.varNames[i]);
        if (it != bvByName.end()) varBuiltin[i] = it->second;
    }

    // funciones
    auto& reg = builtinRegistry();
    funcs.assign(d.functions.size(), nullptr);
    funcScript.assign(d.functions.size(), -1);
    int missing = 0;
    for (size_t i = 0; i < d.functions.size(); i++) {
        auto it = reg.find(d.functions[i]);
        if (it != reg.end()) funcs[i] = it->second;
        else {
            auto si = d.scriptByName.find(d.functions[i]);
            if (si != d.scriptByName.end()) funcScript[i] = d.scripts[si->second].code;
            else { missing++; logf("[falta] %s\n", d.functions[i].c_str()); }
        }
    }
    logf("funciones: %u, sin implementar: %d\n", (unsigned)d.functions.size(), missing);

    // sprites / backgrounds runtime
    sprites.resize(d.sprites.size());
    for (size_t i = 0; i < d.sprites.size(); i++) {
        const Sprite& s = d.sprites[i]; RSprite& r = sprites[i];
        r.name = s.name; r.w = s.w; r.h = s.h; r.ox = s.ox; r.oy = s.oy;
        r.bl = s.ml; r.br = s.mr; r.bt = s.mt; r.bb = s.mb;
        { float dx = (float)std::max(std::abs(s.ml - (int)s.ox), std::abs(s.mr + 1 - (int)s.ox)), dy = (float)std::max(std::abs(s.mt - (int)s.oy), std::abs(s.mb + 1 - (int)s.oy)); r.ext = std::sqrt(dx * dx + dy * dy) + 2; }
        r.precise = s.sepmasks == 1; r.kind = r.precise ? 0 : 1;
        for (int tp : s.frames) r.frames.push_back(frameFromTpag(d, tp));
        r.masks = s.masks;
    }
    backgrounds.resize(d.backgrounds.size());
    for (size_t i = 0; i < d.backgrounds.size(); i++) {
        RBackground& b = backgrounds[i]; b.name = d.backgrounds[i].name; b.f = frameFromTpag(d, d.backgrounds[i].tpag);
        b.w = b.f.bw; b.h = b.f.bh;
    }

    // herencia y tabla de eventos
    size_t no = d.objects.size();
    isA.assign(no, std::vector<bool>(no, false));
    evTable.assign(no, {}); evList.assign(no, {});
    collisionPairs.assign(no, {});
    for (size_t o = 0; o < no; o++) {
        int cur = (int)o; int guard = 0;
        while (cur >= 0 && guard++ < 64) {
            isA[o][cur] = true;
            for (auto& e : d.objects[cur].events) {
                uint64_t k = evKey(e.type, e.sub);
                if (!evTable[o].count(k)) evTable[o][k] = EvRef{e.code, cur};
            }
            cur = d.objects[cur].parent;
        }
        for (auto& [k, ev] : evTable[o]) if ((k >> 32) == EV_COLLISION) collisionPairs[o].push_back((int)(k & 0xffffffff));
        evList[o].assign(evTable[o].begin(), evTable[o].end());
        std::sort(evList[o].begin(), evList[o].end(), [](const std::pair<uint64_t, EvRef>& a, const std::pair<uint64_t, EvRef>& b) { return a.first < b.first; });
    }
    fastEv.assign(no, {}); hasKeyEv.assign(no, 0);
    for (size_t o = 0; o < no; o++) {
        for (auto& [k, ev] : evList[o]) {
            int t = (int)(k >> 32); uint32_t sub = (uint32_t)(k & 0xffffffff);
            if (t == EV_STEP && sub <= 2) fastEv[o][sub] = ev;
            else if (t == EV_DRAW && sub == 0) fastEv[o][3] = ev;
            else if ((t == EV_KEYBOARD || t == EV_KEYPRESS || t == EV_KEYRELEASE) && sub <= 255) hasKeyEv[o] = 1;
        }
    }

    // codigo nativo: solo si se genero a partir de este mismo game.vsd (si no, todo se interpreta)
    aot.assign(d.codes.size(), nullptr); aotEnabled = false;
#ifdef VS_HAVE_AOT
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } };
        uint64_t n = d.codes.size(); mix(&n, 8);
        for (auto& c : d.codes) { uint64_t k = c.ins.size(); mix(&k, 8); if (k) mix(c.ins.data(), k * sizeof(Ins)); }
        n = d.doubles.size(); mix(&n, 8); if (n) mix(d.doubles.data(), n * 8);
        n = d.varNames.size(); mix(&n, 8);
        n = d.functions.size(); mix(&n, 8);
        n = d.strings.size(); mix(&n, 8);
        if (h == kAotHash && d.codes.size() == kAotCount) {
            aotFill(aot.data());
            size_t k = 0; for (auto f : aot) k += f != nullptr;
            aotEnabled = true;
            logf("[aot] codigo nativo activo: %u de %u scripts\n", (unsigned)k, (unsigned)aot.size());
        } else logf("[aot] game.vsd distinto del usado al compilar: se interpreta todo\n");
    }
#endif
    audioInit();
    gotoRoom(d.roomOrder[0], true);
    return true;
}

EvRef Game::findEvent(int obj, int type, uint32_t sub) const {
    if (obj < 0) return {};
    if (type == EV_STEP && sub <= 2 && (size_t)obj < fastEv.size()) return fastEv[obj][sub];
    if (type == EV_DRAW && sub == 0 && (size_t)obj < fastEv.size()) return fastEv[obj][3];
    const uint64_t k = evKey(type, sub);
    for (auto& e : evList[obj]) if (e.first == k) return e.second;
    return EvRef{};
}

bool Game::fireEvent(Instance* inst, int type, uint32_t sub, Instance* other) {
    if (!inst || !inst->alive) return false;
    EvRef e = findEvent(inst->obj, type, sub);
    if (e.code < 0) return false;
    evStack.push_back({e.owner, type, sub});
    Ctx c{inst, other ? other : inst};
    call(e.code, c, nullptr, 0);
    evStack.pop_back();
    return true;
}

// ---------------------------------------------------------------- instancias
// paginas (sin repetir) de un sprite
static std::vector<std::vector<int>> sprPages, objPages; static std::vector<char> sprPagesDone, objPagesDone;
static const std::vector<int>& spritePagesOf(Game& g, int s) {
    auto& sprites = g.sprites;
    static const std::vector<int> none;
    if (s < 0 || s >= (int)sprites.size()) return none;
    if (sprPages.size() < sprites.size()) { sprPages.resize(sprites.size()); sprPagesDone.resize(sprites.size(), 0); }
    if (!sprPagesDone[s]) {
        sprPagesDone[s] = 1;
        for (auto& f : sprites[s].frames)
            if (f.tex >= 0 && std::find(sprPages[s].begin(), sprPages[s].end(), f.tex) == sprPages[s].end()) sprPages[s].push_back(f.tex);
    }
    return sprPages[s];
}

// paginas que un objeto va a usar: su sprite, su mascara y los anim_<objeto>_* (ataques, heridas... que si no se
// cargarian en mitad del combate). Se calcula una vez por objeto.
static const std::vector<int>& objectPagesOf(Game& g, int obj) {
    auto& d = g.d; auto& sprites = g.sprites;
    if (objPages.size() < d.objects.size()) { objPages.resize(d.objects.size()); objPagesDone.resize(d.objects.size(), 0); }
    if (!objPagesDone[obj]) {
        objPagesDone[obj] = 1;
        auto& v = objPages[obj];
        auto add = [&](int s) { for (int tx : spritePagesOf(g, s)) if (std::find(v.begin(), v.end(), tx) == v.end()) v.push_back(tx); };
        add(d.objects[obj].sprite); add(d.objects[obj].mask);
        const std::string& on = d.objects[obj].name;
        if (on.compare(0, 3, "fx_") != 0 && on.compare(0, 6, "block_") != 0) {
            // nombres con los que buscar: el del objeto y sin prefijos/sufijos de variante (assassin_fire -> assassin,
            // npc_summon_banshee -> banshee). Familia de un sprite = token tras anim_ (anim_dhist_slash -> dhist): vale si
            // el nombre empieza por ella (dhistahn, ruckjahl_core, shin_azudor, evil_dhistahn, siren_ice, manawar...).
            std::vector<std::string> names{on};
            for (const char* p : {"npc_summon_", "npc_", "atk_", "player_"})
                if (on.compare(0, std::strlen(p), p) == 0) { names.push_back(on.substr(std::strlen(p))); break; }
            for (size_t k = 0, n0 = names.size(); k < n0; k++)
                for (const char* sfx : {"_poison", "_fire", "_ice", "_void", "_thunder", "_shadow"}) {
                    size_t L = std::strlen(sfx); const std::string& nm = names[k];
                    if (nm.size() > L && nm.compare(nm.size() - L, L, sfx) == 0) names.push_back(nm.substr(0, nm.size() - L));
                }
            for (size_t s = 0; s < sprites.size(); s++) {
                const std::string& sn = sprites[s].name;
                if (sn.compare(0, 5, "anim_") != 0) continue;
                size_t e = sn.find('_', 5); if (e == std::string::npos) continue;
                std::string fam = sn.substr(5, e - 5);
                for (auto& nm : names)
                    if (sn.compare(5, nm.size(), nm) == 0 && sn.size() > 5 + nm.size() && sn[5 + nm.size()] == '_') { add((int)s); break; }
                    else if (fam.size() >= 3 && nm.compare(0, fam.size(), fam) == 0) { add((int)s); break; }
            }
        }
    }
    return objPages[obj];
}

// pide al hilo cargador las paginas que se veran pronto (no bloquea; si ya estan cargadas no hace nada)
static void prefetchObject(Game& g, int obj) {
    if (obj < 0 || obj >= (int)g.d.objects.size()) return;
    for (int tx : objectPagesOf(g, obj)) gfx::preload(tx);
}
static void prefetchSprite(Game& g, int s) { for (int tx : spritePagesOf(g, s)) gfx::preload(tx); }

// Al cambiar sprite_index: ademas del sprite nuevo, todas las paginas de su familia anim_<fam>_* (el jugador es
// player_wyatt pero usa anim_reina_*, anim_vlad_*...; los ataques se ven por primera vez en mitad del combate y su
// pagina tardaba ~8 frames en llegar: el ataque no se dibujaba). Las familias ocupan 1-5 paginas.
static std::vector<std::vector<int>> famPages; static std::vector<char> famPagesDone;
static void prefetchSpriteFamily(Game& g, int s) {
    auto& sprites = g.sprites;
    if (s < 0 || s >= (int)sprites.size()) return;
    if (famPages.size() < sprites.size()) { famPages.resize(sprites.size()); famPagesDone.resize(sprites.size(), 0); }
    if (!famPagesDone[s]) {
        famPagesDone[s] = 1;
        auto& v = famPages[s];
        const std::string& sn = sprites[s].name;
        size_t e = sn.compare(0, 5, "anim_") == 0 ? sn.find('_', 5) : std::string::npos;
        if (e != std::string::npos) {
            std::string pre = sn.substr(0, e + 1);
            for (size_t k = 0; k < sprites.size(); k++)
                if (sprites[k].name.compare(0, pre.size(), pre) == 0)
                    for (int tx : spritePagesOf(g, (int)k)) if (std::find(v.begin(), v.end(), tx) == v.end()) v.push_back(tx);
        }
    }
    for (int tx : famPages[s]) gfx::preload(tx);
}

// Salas vecinas (id +-1, +-2: las de cada zona son correlativas, outer_slum_1..21, grave_1..22): sus fondos y los
// sprites de sus objetos se piden al hilo cargador un rato despues de entrar en una sala, para que al cruzar la puerta
// ya esten en la GPU (entrada en frio = ~120 ms por pagina leida de la tarjeta). gfx::preload no expulsa nada: se
// detiene si la memoria de GPU o el presupuesto van justos.
static int gNeighbourAt = 0;
static void prefetchNeighbours(Game& g) {
    std::vector<char> seen(512, 0);   // cada pagina una vez (antes se pedia por cada tile: miles de llamadas)
    auto bgPage = [&](int bg) {
        if (bg < 0 || bg >= (int)g.backgrounds.size()) return;
        int tx = g.backgrounds[bg].f.tex;
        if (tx >= 0 && tx < (int)seen.size()) { if (seen[tx]) return; seen[tx] = 1; }
        gfx::preload(tx);
    };
    const int order[] = {1, -1, 2, -2};
    for (int pass = 0; pass < 2; pass++)
        for (int dlt : order) {
            int r = g.room + dlt;
            if (r < 0 || r >= (int)g.d.rooms.size()) continue;
            const vs::Room& rm = g.d.rooms[r];
            if (rm.insts.empty() && rm.tiles.empty()) continue;   // salas de menu: las dibuja otro objeto
            if (pass == 0) {   // primero lo que se ve nada mas entrar: fondos y tiles
                for (auto& b : rm.bgs) if (b.enabled) bgPage(b.bg);
                for (auto& t : rm.tiles) bgPage(t.bg);
            } else {
                for (auto& in : rm.insts) if (in.obj >= 0 && in.obj < (int)g.d.objects.size()) prefetchSprite(g, g.d.objects[in.obj].sprite);
            }
        }
}

// Epoca de geometria: cambia cada vez que puede cambiar la caja de colision de alguna instancia (posicion, escala,
// angulo, sprite/mascara, altas). La rejilla de colisiones de fn_core.cpp se reconstruye cuando cambia.
uint64_t gGeomEpoch = 1;
// perfil ligero (save/profphase en la Vita): fases del frame + tiempo por script/funcion SIN desactivar el codigo nativo
bool gProfPhaseOnly = false;
bool gProfLight = false;   // save/proffases en la Vita: solo el tiempo de cada fase (sin cronometrar cada llamada)
Instance* Game::createInstance(double x, double y, int obj, bool runCreate, int forceId) {
    gGeomEpoch++; geomTouchObj(obj);
    if (obj < 0 || obj >= (int)d.objects.size()) { warnOnce("instance_create con objeto invalido"); return nullptr; }
    auto p = std::make_unique<Instance>();
    Instance* i = p.get();
    i->id = forceId >= 0 ? forceId : ++nextId;
    if (i->id > nextId) nextId = i->id;
    i->obj = obj; i->x = i->xprev = i->xstart = x; i->y = i->yprev = i->ystart = y;
    i->vars.bind(obj);
    const Object& o = d.objects[obj];
    i->sprite = o.sprite; i->mask = o.mask; i->depth = o.depth; i->visible = o.visible; i->solid = o.solid; i->persistent = o.persistent;
    prefetchObject(*this, obj);
    i->createSerial = ++serial;
    byId[i->id] = i;
    insts.push_back(std::move(p));
    byObjDirty = true;
    if (runCreate) fireEvent(i, EV_CREATE, 0);
    return i;
}

void Game::destroyInstance(Instance* i, bool runEvent) {
    if (!i || !i->alive) return;
    if (runEvent) fireEvent(i, EV_DESTROY, 0);
    i->alive = false;
}

void Game::purgeDead() {
    size_t w = 0;
    for (size_t r = 0; r < insts.size(); r++) {
        if (insts[r]->alive) { if (w != r) insts[w] = std::move(insts[r]); w++; }
        else { byId.erase(insts[r]->id); geomTouchObj(insts[r]->obj); }
    }
    if (w != insts.size()) { byObjDirty = true; gGeomEpoch++; }
    insts.resize(w);
}

const std::vector<Instance*>& Game::instancesOf(int obj) const {
    if (byObjDirty) {
        byObj.resize(d.objects.size());
        for (auto& v : byObj) v.clear();
        for (auto& p : insts) {
            if (!p || !p->alive) continue;
            for (int cur = p->obj, guard = 0; cur >= 0 && guard < 64; cur = d.objects[cur].parent, guard++) byObj[cur].push_back(p.get());
        }
        byObjDirty = false;
    }
    return byObj[obj];
}

Instance* Game::firstOf(int obj) const {
    if (obj < 0 || obj >= (int)isA.size()) return nullptr;
    for (Instance* p : instancesOf(obj)) if (p->alive) return p;
    return nullptr;
}

void Game::collectTargets(double target, Ctx& ctx, std::vector<Instance*>& out) {
    int t = (int)target;
    if (t == -1) { if (ctx.self && ctx.self->alive) out.push_back(ctx.self); return; }
    if (t == -2) { if (ctx.other && ctx.other->alive) out.push_back(ctx.other); return; }
    if (t == -3) { for (auto& p : insts) if (p->alive) out.push_back(p.get()); return; }
    if (t < 0) return;
    if (t >= 100000) { if (Instance* i = findInstance(t)) out.push_back(i); return; }
    if (t >= (int)isA.size()) return;
    for (Instance* p : instancesOf(t)) if (p->alive) out.push_back(p);
}

// huella del estado del juego (instancias, variables, globales): para comparar el codigo nativo con el interprete
static uint64_t hmix(uint64_t h, uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); return h * 0x100000001B3ull; }
static uint64_t hval(uint64_t h, const Value& v) {
    h = hmix(h, (uint64_t)v.t);
    if (v.t == VT::Real) { uint64_t b; double r = v.r; std::memcpy(&b, &r, 8); return hmix(h, b); }
    // VS_HASHNOSTR: las cadenas solo cuentan por su tipo (para comparar la logica con y sin traduccion)
    static const bool noStr = std::getenv("VS_HASHNOSTR") != nullptr;
    if (v.t == VT::Str) { if (!noStr) for (unsigned char c : v.s->s) h = hmix(h, c); return h; }
    if (v.t == VT::Arr) { for (auto& row : v.a->rows) { h = hmix(h, row.size()); for (auto& e : row) h = hval(h, e); } }
    return h;
}
static uint64_t hvars(uint64_t h, const IntMap<Value>& m) {
    std::vector<std::pair<int, const Value*>> kv;
    for (auto& e : const_cast<IntMap<Value>&>(m)) kv.push_back({e.first, &e.second});
    std::sort(kv.begin(), kv.end(), [](const std::pair<int, const Value*>& a, const std::pair<int, const Value*>& b) { return a.first < b.first; });
    for (auto& [k, v] : kv) { h = hmix(h, (uint64_t)k); h = hval(h, *v); }
    return h;
}
static uint64_t hvars(uint64_t h, const VarStore& m) {
    std::vector<std::pair<int, const Value*>> kv;
    m.forEach([&](int k, const Value& v) { kv.push_back({k, &v}); });
    std::sort(kv.begin(), kv.end(), [](const std::pair<int, const Value*>& a, const std::pair<int, const Value*>& b) { return a.first < b.first; });
    for (auto& [k, v] : kv) { h = hmix(h, (uint64_t)k); h = hval(h, *v); }
    return h;
}
uint64_t Game::stateHash() const {
    uint64_t h = hmix(1469598103934665603ull, (uint64_t)room);
    for (auto& p : insts) {
        if (!p->alive) continue;
        const Instance& i = *p;
        h = hmix(h, (uint64_t)i.id); h = hmix(h, (uint64_t)i.obj);
        double nums[] = {i.x, i.y, i.hspeed, i.vspeed, i.image_index, i.image_speed, i.depth, (double)i.sprite};
        for (double d0 : nums) { uint64_t b; std::memcpy(&b, &d0, 8); h = hmix(h, b); }
        for (int a = 0; a < 12; a++) h = hmix(h, (uint64_t)(int64_t)i.alarm[a]);
        h = hvars(h, i.vars);
    }
    return hvars(h, globals);
}

double Game::random(double hi) { std::uniform_real_distribution<double> u(0.0, 1.0); return u(*rng) * hi; }

// ---------------------------------------------------------------- colision
static void bboxCompute(const RSprite& s, const Instance* i, double& l, double& t, double& r, double& b) {
    double x0 = (s.bl - s.ox) * i->xscale, x1 = (s.br + 1 - s.ox) * i->xscale;
    double y0 = (s.bt - s.oy) * i->yscale, y1 = (s.bb + 1 - s.oy) * i->yscale;
    if (i->angle == 0) {
        l = i->x + std::min(x0, x1); r = i->x + std::max(x0, x1) - 1;
        t = i->y + std::min(y0, y1); b = i->y + std::max(y0, y1) - 1;
    } else {
        double a = -i->angle * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
        double xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
        l = t = 1e18; r = b = -1e18;
        for (int k = 0; k < 4; k++) {
            double px = i->x + xs[k] * ca - ys[k] * sa, py = i->y + xs[k] * sa + ys[k] * ca;
            l = std::min(l, px); r = std::max(r, px); t = std::min(t, py); b = std::max(b, py);
        }
        r -= 1; b -= 1;
    }
    // std::round es una llamada a libm; en la Cortex-A9 sin vrint cuesta mucho. Truncar tras sumar 0.5 es igual (mitad lejos de 0)
    auto rd = [](double v) { return (double)(long long)(v + (v >= 0 ? 0.5 : -0.5)); };
    l = rd(l); t = rd(t); r = rd(r); b = rd(b);
}

// Cache de cajas: los scripts de enemigos leen bbox_left/top/right/bottom (4 calculos) antes de cada
// collision_rectangle (~1000 por frame en temple_5). Clave = todas las entradas del calculo, asi nunca queda obsoleta.
// La cache va en cada instancia (Instance::bbc).
void Game::bbox(const Instance* i, double& l, double& t, double& r, double& b) const {
    int m = maskOf(i);
    if (m < 0 || m >= (int)sprites.size()) { l = r = i->x; t = b = i->y; return; }
    Instance::BBoxCache& mm = i->bbc;
    if (mm.m == m && mm.x == i->x && mm.y == i->y && mm.xs == i->xscale && mm.ys == i->yscale && mm.a == i->angle) {
        l = mm.l; t = mm.t; r = mm.r; b = mm.b; return;
    }
    bboxCompute(sprites[m], i, l, t, r, b);
    mm.m = m; mm.x = i->x; mm.y = i->y; mm.xs = i->xscale; mm.ys = i->yscale; mm.a = i->angle; mm.l = l; mm.t = t; mm.r = r; mm.b = b;
}

bool Game::pointInInstance(const Instance* i, double px, double py) const {
    int m0 = maskOf(i);
    if (m0 >= 0 && m0 < (int)sprites.size()) {
        // descarte rapido: lejos del origen mas que la esquina mas lejana (con escala)
        double sc = std::max(std::abs(i->xscale), std::abs(i->yscale)), R = sprites[m0].ext * sc;
        if (std::abs(px - i->x) > R || std::abs(py - i->y) > R) return false;
    }
    double l, t, r, b; bbox(i, l, t, r, b);
    if (px < l || px > r || py < t || py > b) return false;
    int m = maskOf(i);
    if (m < 0) return false;
    const RSprite& s = sprites[m];
    if (!s.precise || s.masks.empty()) return true;
    // a espacio de sprite
    double dx = px - i->x, dy = py - i->y;
    if (i->angle != 0) { double a = i->angle * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a); double nx = dx * ca - dy * sa, ny = dx * sa + dy * ca; dx = nx; dy = ny; }
    if (i->xscale == 0 || i->yscale == 0) return false;
    int sx = (int)std::floor(dx / i->xscale + s.ox), sy = (int)std::floor(dy / i->yscale + s.oy);
    int fr = s.masks.size() == 1 ? 0 : ((int)std::floor(i->image_index) % (int)s.masks.size());
    if (fr < 0) fr += (int)s.masks.size();
    return s.masks[fr].get(sx, sy);
}

bool Game::instancesCollide(const Instance* a, const Instance* b) const {
    if (maskOf(a) < 0 || maskOf(b) < 0) return false;
    double al, at, ar, ab, bl, bt, br, bb;
    bbox(a, al, at, ar, ab); bbox(b, bl, bt, br, bb);
    if (al > br || bl > ar || at > bb || bt > ab) return false;
    const RSprite& sa = sprites[maskOf(a)]; const RSprite& sb = sprites[maskOf(b)];
    bool pa = sa.precise && !sa.masks.empty(), pb = sb.precise && !sb.masks.empty();
    if (!pa && !pb) return true;
    int l = (int)std::max(al, bl), r = (int)std::min(ar, br), t = (int)std::max(at, bt), bo = (int)std::min(ab, bb);
    for (int y = t; y <= bo; y++)
        for (int x = l; x <= r; x++)
            if (pointInInstance(a, x + 0.5, y + 0.5) && pointInInstance(b, x + 0.5, y + 0.5)) return true;
    return false;
}

bool Game::rectInInstance(const Instance* i, double l, double t, double r, double b, bool prec) const {
    if (maskOf(i) < 0) return false;
    if (l > r) std::swap(l, r);
    if (t > b) std::swap(t, b);
    double il, it, ir, ib; bbox(i, il, it, ir, ib);
    if (il > r || l > ir || it > b || t > ib) return false;
    const RSprite& s = sprites[maskOf(i)];
    if (!prec || !s.precise || s.masks.empty()) return true;
    int L = (int)std::max(il, l), R = (int)std::min(ir, r), T = (int)std::max(it, t), B = (int)std::min(ib, b);
    for (int y = T; y <= B; y++) for (int x = L; x <= R; x++) if (pointInInstance(i, x + 0.5, y + 0.5)) return true;
    return false;
}

// ---------------------------------------------------------------- variables builtin
static bool viewVar(Game& g, int bv, int idx, Value* out, const Value* in) {
    int v = idx < 0 ? 0 : idx;
    if (v > 7) return true;
    ViewState& s = g.views[v];
    double* dp = nullptr; int* ip = nullptr; bool* bp = nullptr;
    switch (bv) {
        case BV_VIEW_VISIBLE: bp = &s.visible; break;
        case BV_VIEW_XVIEW: dp = &s.xview; break; case BV_VIEW_YVIEW: dp = &s.yview; break;
        case BV_VIEW_WVIEW: dp = &s.wview; break; case BV_VIEW_HVIEW: dp = &s.hview; break;
        case BV_VIEW_XPORT: dp = &s.xport; break; case BV_VIEW_YPORT: dp = &s.yport; break;
        case BV_VIEW_WPORT: dp = &s.wport; break; case BV_VIEW_HPORT: dp = &s.hport; break;
        case BV_VIEW_HBORDER: ip = &s.hborder; break; case BV_VIEW_VBORDER: ip = &s.vborder; break;
        case BV_VIEW_HSPEED: ip = &s.hspeed; break; case BV_VIEW_VSPEED: ip = &s.vspeed; break;
        case BV_VIEW_OBJECT: ip = &s.obj; break;
        default: return false;
    }
    if (out) { if (dp) *out = Value(*dp); else if (ip) *out = Value((double)*ip); else *out = Value(*bp ? 1.0 : 0.0); }
    if (in) { if (dp) *dp = in->num(); else if (ip) *ip = in->ival(); else *bp = in->truthy(); }
    return true;
}

bool Game::getBuiltin(int bv, Instance* i, int idx, Value& out) {
    if (bv >= BV_ARG0 && bv < BV_ARG0 + 16) { int k = bv - BV_ARG0; out = (curArgs && k < curArgc) ? curArgs[k] : Value(); return true; }
    switch (bv) {
        case BV_ARGUMENT: { int k = idx < 0 ? 0 : idx; out = (curArgs && k < curArgc) ? curArgs[k] : Value(); return true; }
        case BV_ARGUMENT_COUNT: out = Value((double)curArgc); return true;
        case BV_ROOM: out = Value((double)room); return true;
        case BV_ROOM_WIDTH: out = Value((double)roomW); return true;
        case BV_ROOM_HEIGHT: out = Value((double)roomH); return true;
        case BV_ROOM_SPEED: out = Value((double)roomSpeed); return true;
        case BV_ROOM_PERSISTENT: out = Value(roomPersistent ? 1.0 : 0.0); return true;
        case BV_VIEW_ENABLED: out = Value(viewsEnabled ? 1.0 : 0.0); return true;
        case BV_VIEW_CURRENT: out = Value((double)viewCurrent); return true;
        case BV_BACKGROUND_COLOR: out = Value((double)bgColor); return true;
        case BV_BACKGROUND_X: out = Value(bgs[idx < 0 ? 0 : idx & 7].x); return true;
        case BV_BACKGROUND_Y: out = Value(bgs[idx < 0 ? 0 : idx & 7].y); return true;
        case BV_BACKGROUND_VISIBLE: out = Value(bgs[idx < 0 ? 0 : idx & 7].visible ? 1.0 : 0.0); return true;
        case BV_BACKGROUND_INDEX: out = Value((double)bgs[idx < 0 ? 0 : idx & 7].index); return true;
        case BV_KEYBOARD_LASTKEY: out = Value((double)in.lastKey); return true;
        case BV_KEYBOARD_LASTCHAR: out = Value::str(in.lastChar); return true;
        case BV_OS_TYPE: out = Value(0.0); return true; // os_windows: el juego asume PC
        case BV_ASYNC_LOAD: out = Value(-1.0); return true;
        case BV_FPS: out = Value(det ? 60.0 : fps); return true;
        case BV_CURRENT_TIME: out = Value(det ? frame * (1000.0 / 60.0) : (double)(nowMs() - startMs)); return true;
        case BV_INSTANCE_COUNT: { int c = 0; for (auto& p : insts) c += p->alive; out = Value((double)c); return true; }
        // instance_id[k] (GMS1, solo lectura): id de la k-esima instancia viva, en orden de creacion; sin indice = [0].
        // Los enemigos hacen owner = instance_id en su Create: antes no existia y daba 0 (con aviso de variable sin iniciar).
        case BV_INSTANCE_ID: {
            int k = idx < 0 ? 0 : idx % 32000;
            for (auto& p : insts) if (p->alive && k-- == 0) { out = Value((double)p->id); return true; }
            out = Value(-4.0); return true;   // noone
        }
        case BV_WORKING_DIRECTORY: out = Value::str(""); return true;
        default: break;
    }
    if (bv >= BV_VIEW_VISIBLE && bv <= BV_VIEW_OBJECT) return viewVar(*this, bv, idx, &out, nullptr);
    if (!i) return false;
    switch (bv) {
        case BV_X: out = i->x; return true; case BV_Y: out = i->y; return true;
        case BV_XPREV: out = i->xprev; return true; case BV_YPREV: out = i->yprev; return true;
        case BV_XSTART: out = i->xstart; return true; case BV_YSTART: out = i->ystart; return true;
        case BV_HSPEED: out = i->hspeed; return true; case BV_VSPEED: out = i->vspeed; return true;
        case BV_SPEED: out = i->speed; return true; case BV_DIRECTION: out = i->direction; return true;
        case BV_IMAGE_INDEX: out = i->image_index; return true; case BV_IMAGE_SPEED: out = i->image_speed; return true;
        case BV_XSCALE: out = i->xscale; return true; case BV_YSCALE: out = i->yscale; return true;
        case BV_ANGLE: out = i->angle; return true; case BV_ALPHA: out = i->alpha; return true;
        case BV_BLEND: out = Value((double)i->blend); return true;
        case BV_IMAGE_NUMBER: out = Value((double)imageNumber(i)); return true;
        case BV_SPRITE_INDEX: out = Value((double)i->sprite); return true;
        case BV_MASK_INDEX: out = Value((double)i->mask); return true;
        case BV_DEPTH: out = i->depth; return true;
        case BV_VISIBLE: out = Value(i->visible ? 1.0 : 0.0); return true;
        case BV_SOLID: out = Value(i->solid ? 1.0 : 0.0); return true;
        case BV_PERSISTENT: out = Value(i->persistent ? 1.0 : 0.0); return true;
        case BV_OBJECT_INDEX: out = Value((double)i->obj); return true;
        case BV_ID: out = Value((double)i->id); return true;
        case BV_ALARM: out = Value(i->alarm[(idx < 0 ? 0 : idx) % 12]); return true;
        case BV_SPRITE_WIDTH: out = Value(i->sprite >= 0 ? sprites[i->sprite].w * std::fabs(i->xscale) : 0.0); return true;
        case BV_SPRITE_HEIGHT: out = Value(i->sprite >= 0 ? sprites[i->sprite].h * std::fabs(i->yscale) : 0.0); return true;
        case BV_BBOX_LEFT: case BV_BBOX_RIGHT: case BV_BBOX_TOP: case BV_BBOX_BOTTOM: {
            double l, t, r, b; bbox(i, l, t, r, b);
            out = Value(bv == BV_BBOX_LEFT ? l : bv == BV_BBOX_RIGHT ? r : bv == BV_BBOX_TOP ? t : b); return true;
        }
        default: return false;
    }
}

bool Game::setBuiltin(int bv, Instance* i, int idx, const Value& v) {
    if (bv >= BV_ARG0 && bv < BV_ARG0 + 16) { int k = bv - BV_ARG0; if (curArgs && k < curArgc) curArgs[k] = v; return true; }
    switch (bv) {
        case BV_ARGUMENT: { int k = idx < 0 ? 0 : idx; if (curArgs && k < curArgc) curArgs[k] = v; return true; }
        case BV_ROOM: pendingRoom = v.ival(); return true;
        case BV_ROOM_SPEED: roomSpeed = std::max(1, v.ival()); return true;
        case BV_ROOM_PERSISTENT: roomPersistent = v.truthy(); if (room >= 0) roomPersistOverride[room] = roomPersistent; return true;
        case BV_VIEW_ENABLED: viewsEnabled = v.truthy(); return true;
        case BV_BACKGROUND_COLOR: bgColor = (uint32_t)v.num(); return true;
        case BV_BACKGROUND_X: bgs[idx < 0 ? 0 : idx & 7].x = v.num(); return true;
        case BV_BACKGROUND_Y: bgs[idx < 0 ? 0 : idx & 7].y = v.num(); return true;
        case BV_BACKGROUND_VISIBLE: bgs[idx < 0 ? 0 : idx & 7].visible = v.truthy(); return true;
        case BV_BACKGROUND_INDEX: bgs[idx < 0 ? 0 : idx & 7].index = v.ival(); return true;
        case BV_KEYBOARD_LASTKEY: in.lastKey = v.ival(); return true;
        case BV_KEYBOARD_LASTCHAR: in.lastChar = v.toString(); return true;
        case BV_ROOM_WIDTH: case BV_ROOM_HEIGHT: case BV_OS_TYPE: case BV_ASYNC_LOAD: case BV_FPS: case BV_CURRENT_TIME:
        case BV_INSTANCE_COUNT: case BV_ARGUMENT_COUNT: case BV_VIEW_CURRENT: case BV_WORKING_DIRECTORY: case BV_INSTANCE_ID: return true;
        default: break;
    }
    if (bv >= BV_VIEW_VISIBLE && bv <= BV_VIEW_OBJECT) return viewVar(*this, bv, idx, nullptr, &v);
    if (!i) return true;
    switch (bv) {
        case BV_X: i->x = v.num(); gGeomEpoch++; geomTouchObj(i->obj); return true; case BV_Y: i->y = v.num(); gGeomEpoch++; geomTouchObj(i->obj); return true;
        case BV_XPREV: i->xprev = v.num(); return true; case BV_YPREV: i->yprev = v.num(); return true;
        case BV_XSTART: i->xstart = v.num(); return true; case BV_YSTART: i->ystart = v.num(); return true;
        case BV_HSPEED: i->hspeed = v.num(); i->setHV(); return true;
        case BV_VSPEED: i->vspeed = v.num(); i->setHV(); return true;
        case BV_SPEED: i->speed = v.num(); i->setSpeedDir(); return true;
        case BV_DIRECTION: { double dd = std::fmod(v.num(), 360.0); if (dd < 0) dd += 360; i->direction = dd; i->setSpeedDir(); return true; }
        case BV_IMAGE_INDEX: i->image_index = v.num(); return true; case BV_IMAGE_SPEED: i->image_speed = v.num(); return true;
        case BV_XSCALE: i->xscale = v.num(); gGeomEpoch++; geomTouchObj(i->obj); return true; case BV_YSCALE: i->yscale = v.num(); gGeomEpoch++; geomTouchObj(i->obj); return true;
        case BV_ANGLE: i->angle = v.num(); gGeomEpoch++; geomTouchObj(i->obj); return true; case BV_ALPHA: i->alpha = v.num(); return true;
        case BV_BLEND: i->blend = (uint32_t)v.num(); return true;
        case BV_SPRITE_INDEX: { int ns = v.ival(); if (ns != i->sprite) { prefetchSprite(*this, ns); prefetchSpriteFamily(*this, ns); } i->sprite = ns; gGeomEpoch++; geomTouchObj(i->obj); return true; }
        case BV_MASK_INDEX: i->mask = v.ival(); gGeomEpoch++; geomTouchObj(i->obj); return true;
        case BV_DEPTH: i->depth = v.num(); return true;
        case BV_VISIBLE: i->visible = v.truthy(); return true;
        case BV_SOLID: i->solid = v.truthy(); return true;
        case BV_PERSISTENT: i->persistent = v.truthy(); return true;
        case BV_ALARM: i->alarm[(idx < 0 ? 0 : idx) % 12] = v.num(); return true;
        case BV_OBJECT_INDEX: case BV_ID: case BV_IMAGE_NUMBER: case BV_SPRITE_WIDTH: case BV_SPRITE_HEIGHT:
        case BV_BBOX_LEFT: case BV_BBOX_RIGHT: case BV_BBOX_TOP: case BV_BBOX_BOTTOM: return true;
        default: return false;
    }
}

// GMS 1.x despacha cada evento objeto por objeto (orden de indice de objeto) y, dentro de cada objeto, por
// orden de creacion. Importa: p.ej. player_wyatt (102) pone persistent=true en su Step y general_control (143)
// lo desactiva despues para abrir el menu; en orden de creacion el jugador se colaba en status_room.
std::vector<Instance*> Game::orderedAlive() {
    std::vector<Instance*> v; v.reserve(insts.size());
    for (auto& p : insts) if (p->alive) v.push_back(p.get());
    std::stable_sort(v.begin(), v.end(), [](const Instance* a, const Instance* b) { return a->obj < b->obj; });
    return v;
}

// ---------------------------------------------------------------- rooms
extern int gSlowFnWatch;   // vm.cpp: >0 = avisar de funciones builtin lentas
extern uint64_t gTileEpoch; // fn_core.cpp: indices de tiles
void watchDump(Game& g);   // vm.cpp: desglose de los frames vigilados
void Game::gotoRoom(int r, bool first) {
    stepGridsReset();
    if (r < 0 || r >= (int)d.rooms.size()) { warnOnce("room invalida " + std::to_string(r)); return; }
    gSlowFnWatch = perfVerbose ? 2 : 0;   // desglose por funcion/script: solo con save/perfverbose (cronometrar cuesta)
    double tA = profNow(), tB = tA, tC = tA, tD = tA;
    struct RoomTimes { Game& g; double& a; double& b; double& c; double& d; int r;
        ~RoomTimes() {
            double e = profNow();
            if ((e - a) / 1000.0 > 30 || g.perfVerbose)
                logf("[sala] entrada a %s: %.0f ms (fin sala anterior %.0f, crear instancias %.0f, eventos create %.0f, room start y precarga %.0f)\n",
                     g.d.rooms[r].name.c_str(), (e - a) / 1000.0, (b - a) / 1000.0, (c - b) / 1000.0, (d - c) / 1000.0, (e - d) / 1000.0);
        } } roomTimes{*this, tA, tB, tC, tD, r};
    // fin de la room actual
    if (!first && room >= 0) {
        for (Instance* i : orderedAlive()) if (i->alive) fireEvent(i, EV_OTHER, OTHER_ROOMEND);
        bool persist = roomPersistent;
        if (persist) {
            PersistentRoom pr;
            for (auto& p : insts) if (p->alive && !p->persistent) { byId.erase(p->id); pr.insts.push_back(std::move(p)); }
            pr.tiles = tiles;
            logf("[room] %s guardada (persistente): %d instancias\n", d.rooms[room].name.c_str(), (int)pr.insts.size());
            savedRooms[room] = std::move(pr);
            insts.erase(std::remove_if(insts.begin(), insts.end(), [](const std::unique_ptr<Instance>& p) { return !p; }), insts.end());
            byObjDirty = true;
        } else {
            for (auto& p : insts) if (p->alive && !p->persistent) { p->alive = false; }
        }
        purgeDead();
    }
    tB = tC = tD = profNow();
    logf("[room] %s -> %s (frame %d)\n", room >= 0 ? d.rooms[room].name.c_str() : "-", d.rooms[r].name.c_str(), frame);
    const Room& rm = d.rooms[r];
    room = r; roomW = rm.w; roomH = rm.h; roomSpeed = rm.speed;
    auto ov = roomPersistOverride.find(r);
    roomPersistent = ov != roomPersistOverride.end() ? ov->second : rm.persistent;
    bgColor = rm.bgcolor; drawBgColor = rm.drawbg;
    viewsEnabled = (rm.flags & 1) != 0;
    for (int v = 0; v < 8; v++) {
        ViewState s;
        if (v < (int)rm.views.size()) {
            auto& rv = rm.views[v];
            s.visible = rv.enabled; s.xview = rv.vx; s.yview = rv.vy; s.wview = rv.vw; s.hview = rv.vh;
            s.xport = rv.px; s.yport = rv.py; s.wport = rv.pw; s.hport = rv.ph;
            s.hborder = rv.bx; s.vborder = rv.by; s.hspeed = rv.hs; s.vspeed = rv.vs; s.obj = rv.obj;
        }
        views[v] = s;
    }
    for (int b = 0; b < 8; b++) {
        BgState s;
        if (b < (int)rm.bgs.size()) {
            auto& rb = rm.bgs[b];
            s.visible = rb.enabled; s.foreground = rb.foreground; s.index = rb.bg; s.x = rb.x; s.y = rb.y;
            s.htiled = rb.htiled; s.vtiled = rb.vtiled; s.hspeed = rb.hspeed; s.vspeed = rb.vspeed; s.stretch = rb.stretch;
        }
        bgs[b] = s;
    }

    auto saved = savedRooms.find(r);
    if (roomPersistent && saved != savedRooms.end()) {
        for (auto& p : saved->second.insts) { geomTouchObj(p->obj); byId[p->id] = p.get(); insts.push_back(std::move(p)); }
        byObjDirty = true; gGeomEpoch++;
        tiles = saved->second.tiles; gTileEpoch++;
        savedRooms.erase(saved);
        tC = tD = profNow();
        for (Instance* i : orderedAlive()) if (i->alive) fireEvent(i, EV_OTHER, OTHER_ROOMSTART);
        preloadRoomPages();   // tambien al volver a una sala persistente (antes no habia precarga ni vecinas)
        return;
    }
    tiles.clear(); gTileEpoch++;
    for (auto& t : rm.tiles) {
        Tile tl; tl.id = (int)t.id; tl.bg = t.bg; tl.x = t.x; tl.y = t.y; tl.sx = t.sx; tl.sy = t.sy; tl.w = t.w; tl.h = t.h;
        tl.depth = t.depth; tl.xscale = t.scx; tl.yscale = t.scy; tl.blend = t.color & 0xFFFFFF;
        tl.alpha = ((t.color >> 24) & 0xFF) / 255.0; if (tl.alpha == 0) tl.alpha = 1;
        tiles.push_back(tl);
        if ((int)t.id >= nextTileId) nextTileId = t.id + 1;
    }
    std::vector<std::pair<Instance*, int>> created;
    for (auto& ri : rm.insts) {
        // no duplicar instancias persistentes que ya existen
        if (ri.obj >= 0 && findInstance((int)ri.id)) continue;
        Instance* i = createInstance(ri.x, ri.y, ri.obj, false, (int)ri.id);
        if (!i) continue;
        i->xscale = ri.sx; i->yscale = ri.sy; i->angle = ri.rot; i->blend = ri.color & 0xFFFFFF; geomTouchObj(i->obj);
        created.push_back({i, ri.code});
    }
    tC = profNow();
    for (auto& [i, code] : created) {
        fireEvent(i, EV_CREATE, 0);
        if (code >= 0 && i->alive) { Ctx c{i, i}; call(code, c, nullptr, 0); }
    }
    tD = profNow();
    if (first) for (Instance* i : orderedAlive()) if (i->alive) fireEvent(i, EV_OTHER, OTHER_GAMESTART);
    // Codigo de creacion de la sala: en GMS1 se ejecuta sobre una instancia "dummy" (id 0, sin objeto) donde las
    // variables de instancia funcionan. Antes self era nulo y se perdian (arena_boss_script elige los jefes del modo
    // Arena con variables propias: i, ii, a_min, boss_1...; tambien hall_3, hall_6, prison_16).
    if (rm.code >= 0) {
        static Instance* dummy = nullptr;
        if (!dummy) { dummy = new Instance(); dummy->id = 0; dummy->obj = -1; dummy->vars.bind(-1); }
        dummy->x = dummy->y = 0; dummy->alive = true;
        Ctx c{dummy, dummy}; call(rm.code, c, nullptr, 0);
    }
    for (Instance* i : orderedAlive()) if (i->alive) fireEvent(i, EV_OTHER, OTHER_ROOMSTART);
    preloadRoomPages();
}

// game_restart() de GMS1: evento Game End, se destruyen TODAS las instancias (tambien las persistentes, sin evento
// Destroy), se olvidan las salas persistentes guardadas y se vuelve a la primera sala con Game Start. Las variables
// globales y las estructuras ds_* se conservan, igual que en GMS1. Lo usa "Salir" -> volver al menu principal
// (reset_game_script); antes la peticion se ignoraba y el juego se quedaba en quit_room.
void audioResetForRestart();   // fn_audio.cpp
void Game::restartGame() {
    logf("[room] game_restart desde %s (frame %d)\n", room >= 0 ? d.rooms[room].name.c_str() : "-", frame);
    for (Instance* i : orderedAlive()) if (i->alive) fireEvent(i, EV_OTHER, OTHER_GAMEEND);
    for (auto& p : insts) if (p) p->alive = false;
    purgeDead();
    savedRooms.clear();
    roomPersistOverride.clear();
    audioResetForRestart();
    langApply(*this, gMods.lang);   // el cambio de idioma del menu de MODS se aplica al volver al menu principal
    gotoRoom(d.roomOrder[0], true);
}

// Carga por adelantado las paginas de textura que la sala va a usar: fondos de tiles y los sprites de sus objetos
// (anim_<objeto>_*: ataques, heridas... que si no se cargarian en mitad del combate).
void Game::preloadRoomPages() {
    gfx::roomStart();
    gNeighbourAt = frame + 120;   // 2 s despues: antes, la cola es para la sala actual
    std::vector<char> seenObj(d.objects.size(), 0);
    std::vector<char> seenPage(512, 0);
    auto page = [&](int tex) { if (tex >= 0 && tex < (int)seenPage.size() && !seenPage[tex]) { seenPage[tex] = 1; gfx::preload(tex); } };
    for (auto& t : tiles) if (t.bg >= 0 && t.bg < (int)backgrounds.size()) page(backgrounds[t.bg].f.tex);
    for (auto& p : insts) {
        if (!p->alive || p->obj < 0 || seenObj[p->obj]) continue;
        seenObj[p->obj] = 1;
        if (p->sprite != d.objects[p->obj].sprite) for (int tx : spritePagesOf(*this, p->sprite)) page(tx);
        for (int tx : objectPagesOf(*this, p->obj)) page(tx);
    }
}

void Game::updateViewsFollow() {
    if (!viewsEnabled) return;
    for (auto& v : views) {
        if (!v.visible || v.obj < 0) continue;
        Instance* t = v.obj >= 100000 ? findInstance(v.obj) : firstOf(v.obj);
        if (!t) continue;
        double nx = v.xview, ny = v.yview;
        if (t->x - v.hborder < v.xview) nx = t->x - v.hborder;
        if (t->x + v.hborder > v.xview + v.wview) nx = t->x + v.hborder - v.wview;
        if (t->y - v.vborder < v.yview) ny = t->y - v.vborder;
        if (t->y + v.vborder > v.yview + v.hview) ny = t->y + v.vborder - v.hview;
        if (v.hspeed >= 0) { double dx = nx - v.xview; if (std::fabs(dx) > v.hspeed) nx = v.xview + (dx > 0 ? v.hspeed : -v.hspeed); }
        if (v.vspeed >= 0) { double dy = ny - v.yview; if (std::fabs(dy) > v.vspeed) ny = v.yview + (dy > 0 ? v.vspeed : -v.vspeed); }
        nx = std::max(0.0, std::min(nx, (double)roomW - v.wview));
        ny = std::max(0.0, std::min(ny, (double)roomH - v.hview));
        v.xview = nx; v.yview = ny;
    }
}

// ---------------------------------------------------------------- loop
void Game::inputBeginFrame() {
    for (int k = 0; k < 256; k++) {
        in.prev[k] = in.down[k];
        in.down[k] = in.phys[k] || in.virt[k];
        in.pressed[k] = in.down[k] && !in.prev[k];
        in.released[k] = !in.down[k] && in.prev[k];
        if (in.pressed[k]) in.lastKey = k;
    }
    // vk_anykey / vk_nokey
    bool any = false; for (int k = 2; k < 256; k++) any |= in.down[k];
    in.down[1] = any; in.down[0] = !any;
}

// ---- rejilla por objeto para la fase de colisiones de step()
// gObjGeom[obj] cambia cuando una instancia de obj se mueve, cambia de caja, se crea o se purga. La rejilla de un
// objetivo solo se reconstruye si cambia la lista de sus instancias o la epoca de alguno de sus objetos (los bloques
// casi nunca se mueven: se construye una vez por sala).
static std::vector<uint64_t> gObjGeom; static uint64_t gObjGeomCtr = 0;
void geomTouchObj(int obj) {
    if (obj < 0) return;
    if ((size_t)obj >= gObjGeom.size()) gObjGeom.resize(obj + 1, 0);
    gObjGeom[obj] = ++gObjGeomCtr;
}
static uint64_t objGeom(int obj) { return obj >= 0 && (size_t)obj < gObjGeom.size() ? gObjGeom[obj] : 0; }
struct StepGrid {
    int target = -1; size_t n = 0; const Instance* first = nullptr; uint64_t useTick = 0;
    std::vector<std::pair<int, uint64_t>> objs;   // objetos presentes en la lista y su epoca al construir
    double ox = 0, oy = 0; int gw = 0, gh = 0; static constexpr double kCell = 64;
    std::vector<std::vector<int>> cells; std::vector<int> always;
};
static StepGrid gStepGrids[8]; static uint64_t gStepTick = 0; static std::vector<int> gStepCand;
void stepGridsReset() { for (auto& sg : gStepGrids) { sg.target = -1; sg.n = 0; sg.first = nullptr; } }
static bool stepGridValid(Game& g, const StepGrid& G, int target) {
    const auto& list = g.instancesOf(target);
    if (G.target != target || list.size() != G.n || list.empty() || list[0] != G.first) return false;
    for (auto& oe : G.objs) if (objGeom(oe.first) != oe.second) return false;
    return true;
}
static StepGrid* stepGridFor(Game& g, int target, const std::vector<Instance*>& list) {
    StepGrid* best = &gStepGrids[0];
    for (auto& sg : gStepGrids) { if (sg.target == target) { best = &sg; break; } if (sg.useTick < best->useTick) best = &sg; }
    StepGrid& G = *best; G.useTick = ++gStepTick;
    if (stepGridValid(g, G, target)) return &G;
    G.target = target; G.n = list.size(); G.first = list.empty() ? nullptr : list[0];
    G.objs.clear(); G.always.clear();
    for (Instance* in : list) {
        bool seen = false; for (auto& oe : G.objs) if (oe.first == in->obj) { seen = true; break; }
        if (!seen) G.objs.push_back({in->obj, objGeom(in->obj)});
    }
    std::vector<double> bb(list.size() * 4); std::vector<char> ok(list.size(), 0);
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (size_t k = 0; k < list.size(); k++) {
        Instance* in = list[k];
        int m = g.maskOf(in);
        if (!in->alive || m < 0 || m >= (int)g.sprites.size()) { G.always.push_back((int)k); continue; }
        double l, t, r, b; g.bbox(in, l, t, r, b);
        l -= 2; t -= 2; r += 2; b += 2;
        if (!(r >= l && b >= t) || r - l > 8 * StepGrid::kCell || b - t > 8 * StepGrid::kCell) { G.always.push_back((int)k); continue; }
        bb[k * 4] = l; bb[k * 4 + 1] = t; bb[k * 4 + 2] = r; bb[k * 4 + 3] = b; ok[k] = 1;
        minx = std::min(minx, l); miny = std::min(miny, t); maxx = std::max(maxx, r); maxy = std::max(maxy, b);
    }
    if (minx > maxx) { G.gw = G.gh = 0; G.cells.clear(); return &G; }
    G.ox = minx; G.oy = miny;
    G.gw = std::min(1024, (int)((maxx - minx) / StepGrid::kCell) + 1);
    G.gh = std::min(1024, (int)((maxy - miny) / StepGrid::kCell) + 1);
    G.cells.assign((size_t)G.gw * G.gh, {});
    for (size_t k = 0; k < list.size(); k++) {
        if (!ok[k]) continue;
        int x0 = (int)((bb[k * 4] - G.ox) / StepGrid::kCell), x1 = std::min(G.gw - 1, (int)((bb[k * 4 + 2] - G.ox) / StepGrid::kCell));
        int y0 = (int)((bb[k * 4 + 1] - G.oy) / StepGrid::kCell), y1 = std::min(G.gh - 1, (int)((bb[k * 4 + 3] - G.oy) / StepGrid::kCell));
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) G.cells[(size_t)y * G.gw + x].push_back((int)k);
    }
    return &G;
}
// indices (ordenados, >= from) cuya caja puede tocar [l,r]x[t,b]; una consulta enorme o rara devuelve todos
static void stepGridQuery(const StepGrid& G, double l, double t, double r, double b, size_t from, std::vector<int>& out) {
    out.clear();
    for (int k : G.always) if ((size_t)k >= from) out.push_back(k);
    bool all = false;
    if (G.gw > 0) {
        if (r < l) std::swap(l, r);
        if (b < t) std::swap(t, b);
        if (!(l == l) || !(t == t) || !(r == r) || !(b == b)) all = true;   // NaN
        else {
            double fx0 = std::floor((l - G.ox) / StepGrid::kCell), fx1 = std::floor((r - G.ox) / StepGrid::kCell);
            double fy0 = std::floor((t - G.oy) / StepGrid::kCell), fy1 = std::floor((b - G.oy) / StepGrid::kCell);
            int x0 = (int)std::max(fx0, 0.0), y0 = (int)std::max(fy0, 0.0);
            int x1 = (int)std::min(fx1, (double)G.gw - 1), y1 = (int)std::min(fy1, (double)G.gh - 1);
            if (x1 >= x0 && y1 >= y0) {
                if ((long long)(x1 - x0 + 1) * (y1 - y0 + 1) > 64) all = true;
                else for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++)
                    for (int k : G.cells[(size_t)y * G.gw + x]) if ((size_t)k >= from) out.push_back(k);
            }
        }
    }
    if (all) { out.clear(); for (size_t k = from; k < G.n; k++) out.push_back((int)k); return; }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void Game::step() {
    frame++;
    if (gProfPhaseOnly && prof && !gProfLight) gSlowFnWatch = 1 << 30;   // acumula por script/funcion todo el rato
    else if (gSlowFnWatch > 0 && --gSlowFnWatch == 0) watchDump(*this);
    if (gNeighbourAt && frame >= gNeighbourAt) { gNeighbourAt = 0; prefetchNeighbours(*this); }
    if (prof) {
        if (profFn.empty() && !gProfPhaseOnly) { profFn.assign(d.functions.size(), 0); profFnN.assign(d.functions.size(), 0);
                              profCode.assign(d.codes.size(), 0); profCodeN.assign(d.codes.size(), 0); }
        profFrames++;
    }
    double pt = prof ? profNow() : 0;
    auto phase = [&](int k) { if (prof) { double t = profNow(); profPhase[k] += t - pt; pt = t; } };
    std::vector<Instance*> list = orderedAlive();

    for (Instance* i : list) fireEvent(i, EV_STEP, STEP_BEGIN);
    phase(0);
    // alarmas
    for (Instance* i : list) {
        if (!i->alive) continue;
        for (int a = 0; a < 12; a++) {
            if (i->alarm[a] > 0) {
                i->alarm[a] -= 1;
                if (i->alarm[a] <= 0) { i->alarm[a] = -1; fireEvent(i, EV_ALARM, a); }
            }
        }
    }
    phase(1);
    // teclado
    for (Instance* i : list) {
        if (!i->alive || !hasKeyEv[i->obj]) continue;
        for (auto& [k, ev] : evList[i->obj]) {
            int t = (int)(k >> 32); int key = (int)(k & 0xffffffff);
            if (key > 255) continue;
            if (t == EV_KEYBOARD && in.down[key]) fireEvent(i, EV_KEYBOARD, key);
            else if (t == EV_KEYPRESS && in.pressed[key]) fireEvent(i, EV_KEYPRESS, key);
            else if (t == EV_KEYRELEASE && in.released[key]) fireEvent(i, EV_KEYRELEASE, key);
        }
    }
    phase(2);
    for (Instance* i : list) fireEvent(i, EV_STEP, STEP_NORMAL);
    phase(3);
    // movimiento
    for (Instance* i : list) {
        if (!i->alive) continue;
        i->xprev = i->x; i->yprev = i->y;
        if (i->hspeed != 0 || i->vspeed != 0) { i->x += i->hspeed; i->y += i->vspeed; geomTouchObj(i->obj); }
    }
    gGeomEpoch++;
    phase(4);
    // fuera de la room
    for (Instance* i : list) {
        if (!i->alive) continue;
        EvRef e = findEvent(i->obj, EV_OTHER, OTHER_OUTSIDE);
        if (e.code < 0) continue;
        double l, t, r, b; bbox(i, l, t, r, b);
        bool out = r < 0 || l > roomW || b < 0 || t > roomH;
        if (out && !i->wasOutside) fireEvent(i, EV_OTHER, OTHER_OUTSIDE);
        i->wasOutside = out;
    }
    phase(5);
    // colisiones
    for (Instance* i : list) {
        if (!i->alive) continue;
        for (int target : collisionPairs[i->obj]) {
            // solo las instancias del objeto objetivo (antes se probaba contra TODAS las de la sala)
            collTmp = instancesOf(target);
            double il, it, ir, ib; bool haveBB = false;
            // radio de i: solo cambia con sprite/mascara/escala, que suben gGeomEpoch (se recalcula entonces)
            uint64_t iEp = ~0ull; bool iOk = false; double Ri = 0;
            const int nSpr = (int)sprites.size();
            auto test = [&](Instance* o) {
                if (o == i || !o->alive || !i->alive) return;
                {   // descarte rapido por distancia entre origenes (antes de calcular cajas)
                    if (iEp != gGeomEpoch) {
                        iEp = gGeomEpoch; int mi = maskOf(i); iOk = mi >= 0 && mi < nSpr;
                        if (iOk) Ri = sprites[mi].ext * std::max(std::abs(i->xscale), std::abs(i->yscale));
                    }
                    int mo;
                    if (iOk && (mo = maskOf(o)) >= 0 && mo < nSpr) {
                        double R = Ri + sprites[mo].ext * std::max(std::abs(o->xscale), std::abs(o->yscale));
                        if (std::abs(o->x - i->x) > R || std::abs(o->y - i->y) > R) return;
                    }
                }
                if (!haveBB) { bbox(i, il, it, ir, ib); haveBB = true; }
                { double ol, ot, orr, ob; bbox(o, ol, ot, orr, ob); if (ol > ir || il > orr || ot > ib || it > ob) return; }
                if (instancesCollide(i, o)) {
                    haveBB = false; // i puede moverse (solido): recalcular
                    if (o->solid || i->solid) { i->x = i->xprev; i->y = i->yprev; gGeomEpoch++; geomTouchObj(i->obj); }
                    EvRef e = findEvent(i->obj, EV_COLLISION, target);
                    if (e.code >= 0) { Ctx c{i, o}; call(e.code, c, nullptr, 0); }
                }
            };
            // Muchas instancias (cada enemigo contra los ~320 block_normal de sewer_11): la rejilla del objeto da, en
            // orden, las unicas cuya caja puede tocar la de i. Si algo cambia la geometria a mitad (un evento, la
            // vuelta atras de un solido), se vuelve a consultar con la caja nueva de i; si se movio alguna instancia del
            // objetivo, el resto se recorre entero. El resultado es el mismo que probar todas en orden.
            static const bool kNoGrid = std::getenv("VS_NOCOLLGRID") != nullptr;   // comparar huellas
            size_t next = 0;
            if (!kNoGrid && collTmp.size() >= 32) {
                StepGrid* G = stepGridFor(*this, target, collTmp);
                bool linear = false;
                while (i->alive) {
                    double ql, qt, qr, qb; bbox(i, ql, qt, qr, qb);
                    stepGridQuery(*G, ql, qt, qr, qb, next, gStepCand);
                    uint64_t ep = gGeomEpoch; bool changed = false;
                    for (int idx : gStepCand) {
                        next = (size_t)idx + 1;
                        test(collTmp[idx]);
                        if (gGeomEpoch != ep) { changed = true; break; }
                    }
                    if (!changed) { next = collTmp.size(); break; }
                    if (!stepGridValid(*this, *G, target)) { linear = true; break; }
                }
                if (!linear) continue;
            }
            for (size_t k = next; k < collTmp.size(); k++) test(collTmp[k]);
        }
    }
    phase(6);
    for (Instance* i : list) fireEvent(i, EV_STEP, STEP_END);
    phase(7);
    // animacion
    for (Instance* i : list) {
        if (!i->alive) continue;
        int n = imageNumber(i);
        if (n <= 0) continue;
        i->image_index += i->image_speed;
        if (i->image_index >= n) { i->image_index -= n; fireEvent(i, EV_OTHER, OTHER_ANIMEND); }
        else if (i->image_index < 0) { i->image_index += n; fireEvent(i, EV_OTHER, OTHER_ANIMEND); }
    }
    phase(8);
    updateParticles();
    updateViewsFollow();
    for (int b = 0; b < 8; b++) { bgs[b].x += bgs[b].hspeed; bgs[b].y += bgs[b].vspeed; }
    audioUpdate();
    phase(9);
    purgeDead();

    if (restartRequested) { restartRequested = false; pendingRoom = -1; restartGame(); }
    if (pendingRoom >= 0) { int r = pendingRoom; pendingRoom = -1; gotoRoom(r); }
    phase(10);
}

} // namespace vs
