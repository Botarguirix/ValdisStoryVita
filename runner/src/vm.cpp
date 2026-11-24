// Interprete del bytecode GMS1 (BC13 normalizado por UTMT a los opcodes nuevos).
#include "runtime.h"
#include "gfx.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace vs {
#ifdef VS_OPSTATS
uint64_t gOpStats[256][8];
void dumpOpStats() { for (int o = 0; o < 256; o++) for (int k = 0; k < 8; k++) if (gOpStats[o][k]) std::printf("op %3d k%d %llu\n", o, k, (unsigned long long)gOpStats[o][k]); }
#endif

enum Op : uint8_t {
    OP_CONV = 7, OP_MUL = 8, OP_DIV = 9, OP_REM = 10, OP_MOD = 11, OP_ADD = 12, OP_SUB = 13, OP_AND = 14, OP_OR = 15,
    OP_XOR = 16, OP_NEG = 17, OP_NOT = 18, OP_SHL = 19, OP_SHR = 20, OP_CMP = 21, OP_POP = 69, OP_PUSHI = 132,
    OP_DUP = 134, OP_CALLV = 153, OP_RET = 156, OP_EXIT = 157, OP_POPZ = 158, OP_B = 182, OP_BT = 183, OP_BF = 184,
    OP_PUSHENV = 186, OP_POPENV = 187, OP_PUSH = 192, OP_PUSHLOC = 193, OP_PUSHGLB = 194, OP_PUSHBLTN = 195,
    OP_CALL = 217, OP_BREAK = 255 };
enum DT : uint8_t { DT_DOUBLE = 0, DT_FLOAT = 1, DT_INT32 = 2, DT_INT64 = 3, DT_BOOL = 4, DT_VAR = 5, DT_STRING = 6, DT_INT16 = 15 };
enum { IT_SELF = -1, IT_OTHER = -2, IT_ALL = -3, IT_NOONE = -4, IT_GLOBAL = -5, IT_BUILTIN = -6, IT_LOCAL = -7, IT_STACKTOP = -9 };
enum { RT_ARRAY = 0, RT_STACKTOP = 128, RT_NORMAL = 160 };

static const uint8_t kSize[16] = {8, 4, 4, 8, 4, 16, 16, 4, 4, 4, 4, 4, 4, 4, 4, 4};

struct Slot { Value v; uint8_t sz; };

struct Frame {
    std::vector<std::pair<int, Value>> locals;
    Value* local(int id) {
        for (auto& p : locals) if (p.first == id) return &p.second;
        return nullptr;
    }
    Value& localRef(int id) {
        if (Value* v = local(id)) return *v;
        locals.emplace_back(id, Value());
        return locals.back().second;
    }
};

struct WithFrame { std::vector<Instance*> list; size_t idx; Ctx saved; };

// Pila de la VM: array propio (sin las comprobaciones de std::vector en cada push/pop, que es >60% de las instrucciones)
struct VStack {
    Slot* d = nullptr; size_t n = 0, cap = 0;
    VStack() { grow(1 << 15); }
    void grow(size_t c) {
        Slot* nd = static_cast<Slot*>(::operator new(sizeof(Slot) * c));
        for (size_t i = 0; i < n; i++) { new (&nd[i]) Slot(std::move(d[i])); d[i].~Slot(); }
        ::operator delete(d); d = nd; cap = c;
    }
    size_t size() const { return n; }
    Slot& back() { return d[n - 1]; }
    Slot& operator[](size_t i) { return d[i]; }
    void push_back(Slot&& s) { if (n == cap) grow(cap * 2); new (&d[n]) Slot(std::move(s)); n++; }
    void push_back(const Slot& s) { if (n == cap) { Slot c = s; grow(cap * 2); new (&d[n]) Slot(std::move(c)); n++; return; } new (&d[n]) Slot(s); n++; }
    void pop_back() { d[--n].~Slot(); }
    void resize(size_t m) { while (n > m) pop_back(); }
};
static VStack gStack; // la VM corre solo en el hilo principal

long gWarnCount = 0;   // avisos distintos emitidos (para el Boss Rush de prueba)
void Game::warnOnce(const std::string& what) {
    for (auto& w : warnedFuncs) if (w == what) return;
    warnedFuncs.push_back(what);
    gWarnCount++;
    logf("[aviso] %s\n", what.c_str());
}

// Resuelve el destino de una variable (instancia o especial) a partir de un valor/instancia
static void resolveInstances(Game& g, Ctx& ctx, double target, std::vector<Instance*>& out) {
    out.clear();
    g.collectTargets(target, ctx, out);
}

static inline double cmpNum(const Value& a, const Value& b, bool& isStr, int& sc) {
    if (a.isStr() && b.isStr()) { isStr = true; sc = a.s->s.compare(b.s->s); return 0; }
    isStr = false;
    return a.num() - b.num();
}

// ------------------------------------------------------------------ semantica compartida (interprete + AOT)
void Game::vmWarnLocal(int var) {
    if (curCode >= 0 && warnVarOnce(curCode, var)) warnOnce("local sin inicializar: " + d.varNames[var] + " en " + d.codes[curCode].name);
}

void Game::vmRead(Ctx& ctx, int inst, int var, bool hasIdx, int row, int col, Value& out) {
    int bv = varBuiltin[var];
    const int codeId = curCode < 0 ? 0 : curCode;
    const std::string& codeName = curCode >= 0 ? d.codes[curCode].name : d.varNames[var];
    if (inst == IT_GLOBAL) {
        if (bv) { if (getBuiltin(bv, ctx.self, hasIdx ? row * 32000 + col : -1, out)) return; }
        Value* gv = globals.get(var);
        if (!gv) { if (warnVarOnce(codeId, var)) warnOnce("global sin inicializar: " + d.varNames[var]); out = Value(); return; }
        out = hasIdx ? gv->get(row, col) : *gv; return;
    }
    Instance* who = nullptr;
    if (inst == IT_SELF || inst == IT_BUILTIN) who = ctx.self;
    else if (inst == IT_OTHER) who = ctx.other;
    else if (inst >= 0) {
        if (inst >= 100000) who = findInstance(inst);
        else who = firstOf(inst);
    } else if (inst == IT_NOONE || inst == IT_ALL) {
        if (inst == IT_ALL && !insts.empty()) { for (auto& p : insts) if (p->alive) { who = p.get(); break; } }
    }
    if (bv) {
        if (bv >= BV_ROOM) { if (getBuiltin(bv, who, hasIdx ? row * 32000 + col : -1, out)) return; }
        else if (who) { if (getBuiltin(bv, who, hasIdx ? row * 32000 + col : -1, out)) return; }
    }
    if (!who) { if (warnVarOnce(codeId, var)) warnOnce("lectura en instancia inexistente: " + d.varNames[var] + " (" + std::to_string(inst) + ") en " + codeName); out = Value(); return; }
    Value* iv = who->vars.get(var);
    if (!iv) {
        if (warnVarOnce(codeId, var)) warnOnce("variable sin inicializar: " + d.varNames[var] + " en " + codeName);
        out = Value(); return;
    }
    out = hasIdx ? iv->get(row, col) : *iv;
}

static inline void writeOne(Game& g, Instance* who, int var, bool hasIdx, int row, int col, const Value& v) {
    int bv = g.varBuiltin[var];
    if (bv && g.setBuiltin(bv, who, hasIdx ? row * 32000 + col : -1, v)) return;
    if (!who) return;
    Value& slot = who->vars[var];
    if (hasIdx) slot.set(row, col, v); else slot = v;
}

void Game::vmWrite(Ctx& ctx, int inst, int var, bool hasIdx, int row, int col, const Value& v) {
    if (inst == IT_GLOBAL) {
        int bv = varBuiltin[var];
        if (bv && setBuiltin(bv, ctx.self, hasIdx ? row * 32000 + col : -1, v)) return;
        Value& s = globals[var]; if (hasIdx) s.set(row, col, v); else s = v; return;
    }
    if (inst == IT_SELF || inst == IT_BUILTIN) { writeOne(*this, ctx.self, var, hasIdx, row, col, v); return; }
    if (inst == IT_OTHER) { writeOne(*this, ctx.other, var, hasIdx, row, col, v); return; }
    if (inst >= 100000) { if (Instance* w = findInstance(inst)) writeOne(*this, w, var, hasIdx, row, col, v); return; }
    if (inst >= 0 || inst == IT_ALL) {
        std::vector<Instance*> list;
        resolveInstances(*this, ctx, inst, list);
        for (Instance* w : list) writeOne(*this, w, var, hasIdx, row, col, v);
        if (list.empty() && varBuiltin[var] >= BV_ROOM) setBuiltin(varBuiltin[var], nullptr, hasIdx ? row * 32000 + col : -1, v);
        return;
    }
}

void Game::vmBinary(uint8_t op, uint8_t t1, uint8_t t2, const Value& a, const Value& b, Value& out) {
    const std::string& codeName = curCode >= 0 ? d.codes[curCode].name : std::string();
    if (op == OP_ADD && (a.isStr() || b.isStr())) {
        if (a.isStr() && b.isStr()) out = Value::str(a.s->s + b.s->s);
        else { warnOnce("suma string+real en " + codeName); out = Value::str(a.toString() + b.toString()); }
        return;
    }
    double A = a.num(), B = b.num(), R = 0;
    switch (op) {
        case OP_ADD: R = A + B; break;
        case OP_SUB: R = A - B; break;
        case OP_MUL:
            if (a.isStr() && !b.isStr()) { std::string s; for (int i = 0; i < (int)B; i++) s += a.s->s; out = Value::str(s); return; }
            R = A * B; break;
        case OP_DIV: R = (B == 0) ? 0 : A / B; if (B == 0) warnOnce("division por cero en " + codeName); break;
        case OP_REM: { if (B == 0) { R = 0; break; } R = std::trunc(A / B); break; }
        case OP_MOD: { if (B == 0) { R = 0; break; } R = std::fmod(A, B); break; }
        case OP_AND: R = (double)((int64_t)A & (int64_t)B); break;
        case OP_OR: R = (double)((int64_t)A | (int64_t)B); break;
        case OP_XOR: R = (double)((int64_t)A ^ (int64_t)B); break;
        case OP_SHL: R = (double)((int64_t)A << (int64_t)B); break;
        case OP_SHR: R = (double)((int64_t)A >> (int64_t)B); break;
    }
    if (t1 == DT_BOOL && t2 == DT_BOOL && (op == OP_AND || op == OP_OR || op == OP_XOR)) {
        bool ba = a.truthy(), bb = b.truthy();
        R = op == OP_AND ? (ba && bb) : op == OP_OR ? (ba || bb) : (ba != bb);
    }
    out = Value(R);
}

bool Game::vmCompare(uint8_t cmp, const Value& a, const Value& b) {
    bool isStr; int sc = 0; double diff = cmpNum(a, b, isStr, sc);
    bool r = false;
    if (isStr) {
        switch (cmp) { case 1: r = sc < 0; break; case 2: r = sc <= 0; break; case 3: r = sc == 0; break;
                       case 4: r = sc != 0; break; case 5: r = sc >= 0; break; case 6: r = sc > 0; break; }
    } else if (a.isStr() != b.isStr()) {
        r = (cmp == 4); // tipos distintos: solo != es cierto
    } else {
        const double eps = 0.00001; // GMS1: igualdad con epsilon (math_set_epsilon por defecto 1e-5)
        switch (cmp) {
            case 1: r = diff < -eps; break; case 2: r = diff <= eps; break; case 3: r = std::fabs(diff) <= eps; break;
            case 4: r = std::fabs(diff) > eps; break; case 5: r = diff >= -eps; break; case 6: r = diff > eps; break;
        }
    }
    return r;
}

int gSlowFnWatch = 0;   // >0 (entrada a sala y frame siguiente): avisar de builtins de mas de 15 ms
// suma por funcion builtin y por script durante esos frames (cientos de llamadas pequenas no salen como "lentas")
static std::vector<double> gWatchFnMs, gWatchCodeMs; static std::vector<int> gWatchFnN, gWatchCodeN;
void watchDump(Game& g) {
    double tot = 0; for (double v : gWatchFnMs) tot += v;
    auto top = [&](std::vector<double>& ms, std::vector<int>& n, bool fn) {
        std::vector<int> idx; for (size_t i = 0; i < ms.size(); i++) if (ms[i] > 2) idx.push_back((int)i);
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return ms[a] > ms[b]; });
        for (size_t k = 0; k < idx.size() && k < 6; k++)
            logf("[sala]   %s %s: %.0f ms en %d llamadas\n", fn ? "funcion" : "script", fn ? g.d.functions[idx[k]].c_str() : g.d.codes[idx[k]].name.c_str(), ms[idx[k]], n[idx[k]]);
    };
    logf("[sala] desglose de la entrada a %s (builtins %.0f ms):\n", g.room >= 0 ? g.d.rooms[g.room].name.c_str() : "-", tot);
    top(gWatchFnMs, gWatchFnN, true);
    top(gWatchCodeMs, gWatchCodeN, false);
    std::fill(gWatchFnMs.begin(), gWatchFnMs.end(), 0); std::fill(gWatchFnN.begin(), gWatchFnN.end(), 0);
    std::fill(gWatchCodeMs.begin(), gWatchCodeMs.end(), 0); std::fill(gWatchCodeN.begin(), gWatchCodeN.end(), 0);
}
// Pruebas en PC (VS_SLOWFRAME=ms): tras cada paso, si la logica paso del umbral, los scripts y funciones que mas
// tiempo gastaron en ESE frame (tiempos inclusivos: un script incluye lo que llama). Siempre vacia los contadores.
void watchFrame(Game& g, double stepMs, double thrMs) {
    if (stepMs > thrMs) {
        auto top = [&](std::vector<double>& ms, std::vector<int>& n, bool fn) {
            std::vector<int> idx; for (size_t i = 0; i < ms.size(); i++) if (ms[i] > 0.05) idx.push_back((int)i);
            std::sort(idx.begin(), idx.end(), [&](int a, int b) { return ms[a] > ms[b]; });
            for (size_t k = 0; k < idx.size() && k < 8; k++)
                logf("[lento]   %s %-40s %7.2f ms %6d llamadas\n", fn ? "fn  " : "code", fn ? g.d.functions[idx[k]].c_str() : g.d.codes[idx[k]].name.c_str(), ms[idx[k]], n[idx[k]]);
        };
        logf("[lento] frame %d sala %s: logica %.1f ms, %d instancias\n", g.frame, g.room >= 0 ? g.d.rooms[g.room].name.c_str() : "-", stepMs, (int)g.insts.size());
        if (g.prof) {   // con --profphase: fases de este frame (us)
            static double last[16] = {};
            static const char* ph[] = {"begin", "alarmas", "teclado", "step", "movim", "fuera", "colis", "endstep", "anim", "part+vis+aud", "purga", "draw"};
            char b[512]; int o = std::snprintf(b, sizeof b, "[lento]   fases us:");
            for (int i = 0; i < 12; i++) o += std::snprintf(b + o, sizeof b - o, " %s %.0f", ph[i], (g.profPhase[i] - last[i]) * 1.0);
            logf("%s\n", b);
        }
        top(gWatchCodeMs, gWatchCodeN, false);
        top(gWatchFnMs, gWatchFnN, true);
    }
    std::fill(gWatchFnMs.begin(), gWatchFnMs.end(), 0); std::fill(gWatchFnN.begin(), gWatchFnN.end(), 0);
    std::fill(gWatchCodeMs.begin(), gWatchCodeMs.end(), 0); std::fill(gWatchCodeN.begin(), gWatchCodeN.end(), 0);
    if (g.prof) for (int i = 0; i < 16; i++) g.profPhase[i] = 0;   // las fases tambien por frame
}
// funcion integrada en curso, para el muestreador de perfiles de la Vita (save/sampler); -1 = codigo GML
volatile int gSampFn = -1;
struct SampFnScope { int prev; explicit SampFnScope(int f) : prev(gSampFn) { gSampFn = f; } ~SampFnScope() { gSampFn = prev; } };

Value Game::vmCallFnSlow(int fn, Ctx& ctx, Value* av, int argc) {
    if (BuiltinFn f = funcs[fn]) {
        SampFnScope sfs(fn);
        Ctx cc = ctx;
        if (gSlowFnWatch > 0) {
            double t0 = profNow();
            Value r = f(*this, cc, av, argc);
            double ms = (profNow() - t0) / 1000.0;
            if (gWatchFnMs.size() < d.functions.size()) { gWatchFnMs.resize(d.functions.size(), 0); gWatchFnN.resize(d.functions.size(), 0); }
            gWatchFnMs[fn] += ms; gWatchFnN[fn]++;
            if (ms > 15) logf("[bandwidth-bound?] funcion lenta %s: %.0f ms (sala %s, script %s)\n", d.functions[fn].c_str(), ms,
                              room >= 0 ? d.rooms[room].name.c_str() : "-", curCode >= 0 ? d.codes[curCode].name.c_str() : "-");
            return r;
        }
        if (prof && !profCode.empty()) {
            double t0 = profNow(), sc = profChild; profChild = 0;
            Value r = f(*this, cc, av, argc);
            double el = profNow() - t0;
            profFn[fn] += el - profChild; profFnN[fn]++;
            profChild = sc + el;
            return r;
        }
        return f(*this, cc, av, argc);
    }
    if (funcScript[fn] >= 0) return call(funcScript[fn], ctx, av, argc);
    warnOnce("funcion sin implementar: " + d.functions[fn]);
    return Value();
}

// Escala del HUD (9.4, MODS): los scripts que dibujan el HUD se dibujan encogidos hacia su esquina (gfx::hudSet).
// 1 = HUD (vida, mana, combos, avisos, panel de jefe derrotado); -1 = dentro del HUD pero a tamano normal (dialogos y
// tutoriales: texto que hay que leer). En los menus (pausa, estado, muerte...) nunca se escala.
int gHudPercent = 100;   // debugtour.cpp lo pone desde save/mods.ini
static std::vector<int8_t> gHudCode;
static bool gHudMenuRoom = false; static int gHudRoomSeen = -2;
static void hudTableInit(Game& g) {
    gHudCode.assign(g.d.codes.size(), 0);
    static const char* on[] = {"gml_Script_ui_wyatt_script", "gml_Script_draw_ui_overlay_script", "gml_Script_draw_level_boss_script",
                               "gml_Object_boss_rating_Draw_0"};   // panel de puntuacion del jefe (tapaba las recompensas, 9.5)
    static const char* always[] = {"gml_Object_boss_rating_Draw_0"};   // se encoge siempre (como mucho al 70 %), aunque el HUD este al 100 %
    static const char* off[] = {"gml_Script_npc_ui_script", "gml_Script_tutorial_script"};
    for (size_t i = 0; i < g.d.codes.size(); i++) {
        for (const char* n : on) if (g.d.codes[i].name == n) gHudCode[i] = 1;
        for (const char* n : off) if (g.d.codes[i].name == n) gHudCode[i] = -1;
        for (const char* n : always) if (g.d.codes[i].name == n) gHudCode[i] = 2;
    }
}
struct HudScope {
    bool active = false; gfx::HudState saved;
    HudScope(Game& g, int codeId) {
        if (gHudCode.size() != g.d.codes.size()) hudTableInit(g);
        int8_t k = gHudCode[codeId];
        if (!k || (gHudPercent >= 100 && k != 2)) return;
        if (g.room != gHudRoomSeen) {
            gHudRoomSeen = g.room;
            const std::string& n = g.d.rooms[g.room].name;
            gHudMenuRoom = n == "status_room" || n == "quit_room" || n == "death_room" || n == "control_room" || n == "eqp_room" ||
                           n == "spell_room" || n == "profile_room" || n == "start_game" || n == "char_room" || n == "lore_room" || n == "store_room";
        }
        if (gHudMenuRoom) return;
        active = true; saved = gfx::hudGet();
        gfx::HudState st = saved;
        if (k > 0) {
            const ViewState& v = g.views[0];
            st.on = true; st.lock = false; st.s = (k == 2 ? std::min(gHudPercent, 70) : gHudPercent) / 100.f;
            if (g.viewsEnabled && v.wview > 0) { st.l = (float)v.xview; st.t = (float)v.yview; st.w = (float)v.wview; st.h = (float)v.hview; }
            else { st.l = 0; st.t = 0; st.w = (float)g.roomW; st.h = (float)g.roomH; }
            st.ax = st.l; st.ay = st.t;
        } else st.on = false;
        gfx::hudSet(st);
    }
    ~HudScope() { if (active) gfx::hudSet(saved); }
};

// Pantalla de logros (logros.cpp): se dibuja al terminar draw_option_menu_script, en el sitio del mapa
void logroScreenBeforeMenu(Game& g, Instance* self); void logroScreenDraw(Game& g, Instance* self); bool logroScreenOpen();
static int gOptMenuCode = -2;
struct OptMenuHook {
    Game* g = nullptr; Instance* self = nullptr;
    OptMenuHook(Game& gm, int codeId, Instance* s) {
        if (gOptMenuCode == -2) { gOptMenuCode = -1; for (size_t i = 0; i < gm.d.codes.size(); i++) if (gm.d.codes[i].name == "gml_Script_draw_option_menu_script") gOptMenuCode = (int)i; }
        if (codeId != gOptMenuCode || !logroScreenOpen()) return;
        g = &gm; self = s; logroScreenBeforeMenu(gm, s);
    }
    ~OptMenuHook() { if (g) logroScreenDraw(*g, self); }
};

Value Game::call(int codeId, Ctx ctx, Value* args, int argc) {
    if (codeId < 0 || codeId >= (int)d.codes.size()) return Value();
    HudScope hudScope(*this, codeId);
    OptMenuHook optMenuHook(*this, codeId, ctx.self);
    SampFnScope sfs(-1);   // dentro del script ya no se esta en la funcion integrada que lo llamo
    if (aotEnabled && aot[codeId] && !(prof && !profCode.empty())) {   // codigo nativo (sin perfilador por script)
        Value* savedArgs = curArgs; int savedArgc = curArgc; int savedCode = curCode;
        curCode = codeId; curArgs = args; curArgc = argc;
        double w0 = gSlowFnWatch > 0 ? profNow() : 0;
        Value r = aot[codeId](*this, ctx, args, argc);
        if (gSlowFnWatch > 0) {   // primeros frames de una sala: scripts lentos (se anidan: el mas interno es el culpable)
            double ms = (profNow() - w0) / 1000.0;
            if (gWatchCodeMs.size() < d.codes.size()) { gWatchCodeMs.resize(d.codes.size(), 0); gWatchCodeN.resize(d.codes.size(), 0); }
            gWatchCodeMs[codeId] += ms; gWatchCodeN[codeId]++;
            if (ms > 15) logf("[CPU-bound] script lento %s: %.0f ms (sala %s, instancia de %s)\n", d.codes[codeId].name.c_str(), ms,
                              room >= 0 ? d.rooms[room].name.c_str() : "-",
                              ctx.self && ctx.self->obj >= 0 ? d.objects[ctx.self->obj].name.c_str() : "-");
        }
        curArgs = savedArgs; curArgc = savedArgc; curCode = savedCode;
        return r;
    }
    const Code& code = d.codes[codeId];
    const Ins* ins = code.ins.data();
    const int nIns = (int)code.ins.size();
    auto& st = gStack;
    const size_t base = st.size();
    Frame fr;
    std::vector<WithFrame> withs;
    Value* savedArgs = curArgs; int savedArgc = curArgc; int savedCode = curCode; curCode = codeId;
    double profT0 = 0, profSavedChild = 0;
    if (prof && !profCode.empty()) { profT0 = profNow(); profSavedChild = profChild; profChild = 0; }
    curArgs = args; curArgc = argc;
    Value ret;
    std::vector<Instance*> tmpList;

    auto pop = [&]() -> Value { Value v = std::move(st.back().v); st.pop_back(); return v; };
    auto push = [&](Value v, uint8_t sz) { st.push_back(Slot{std::move(v), sz}); };

    // ---- lectura/escritura de variables
    auto readVar = [&](int inst, int var, bool hasIdx, int row, int col, Value& out) {
        if (inst == IT_LOCAL) {
            Value* v = fr.local(var);
            if (!v) { vmWarnLocal(var); out = Value(); return; }
            out = hasIdx ? v->get(row, col) : *v; return;
        }
        vmRead(ctx, inst, var, hasIdx, row, col, out);
    };
    auto writeVar = [&](int inst, int var, bool hasIdx, int row, int col, const Value& v) {
        if (inst == IT_LOCAL) { Value& s = fr.localRef(var); if (hasIdx) s.set(row, col, v); else s = v; return; }
        vmWrite(ctx, inst, var, hasIdx, row, col, v);
    };
    auto splitIdx = [](const Value& iv, int& row, int& col) {
        int idx = iv.ival();
        if (idx >= 32000) { row = idx / 32000; col = idx - row * 32000; } else { row = 0; col = idx; }
    };

    int pc = 0;
    while (pc < nIns) {
        const Ins& x = ins[pc];
        insCount++;
#ifdef VS_OPSTATS
        { extern uint64_t gOpStats[256][8]; int k = 0;
          if (x.op == OP_PUSH || x.op == OP_PUSHLOC || x.op == OP_PUSHGLB || x.op == OP_PUSHBLTN || x.op == OP_PUSHI) k = x.t1 == DT_VAR ? (x.inst == IT_SELF ? 1 : x.inst == IT_LOCAL ? 2 : x.inst == IT_GLOBAL ? 3 : x.inst == IT_BUILTIN ? 4 : x.inst >= 0 ? 5 : 6) : 7;
          else if (x.op == OP_POP) k = x.inst == IT_SELF ? 1 : x.inst == IT_LOCAL ? 2 : x.inst == IT_GLOBAL ? 3 : x.inst == IT_BUILTIN ? 4 : x.inst >= 0 ? 5 : 6;
          gOpStats[x.op][k]++; }
#endif
        switch (x.op) {
        case OP_PUSH: case OP_PUSHLOC: case OP_PUSHGLB: case OP_PUSHBLTN: case OP_PUSHI:
            switch (x.t1) {
                case DT_DOUBLE: case DT_INT64: push(Value(d.doubles[x.a]), 8); break;
                case DT_INT32: push(Value((double)x.a), 4); break;
                case DT_INT16: push(Value((double)x.a), 4); break;
                case DT_BOOL: push(Value((double)x.a), 4); break;
                case DT_STRING: {
                    if (strConst.empty()) { strConst.resize(d.strings.size()); for (size_t k = 0; k < d.strings.size(); k++) strConst[k] = new StrObj{1, d.strings[k]}; }
                    Value sv; sv.t = VT::Str; sv.s = strConst[x.a]; sv.s->rc++;
                    push(std::move(sv), 16); break;
                }
                case DT_VAR: {
                    if (x.inst == IT_SELF && x.b == RT_NORMAL && ctx.self && !varBuiltin[x.a]) {
                        if (const Value* v = ctx.self->vars.get(x.a)) { push(Value(*v), 16); break; }
                    }
                    int inst = x.inst; bool hasIdx = false; int row = 0, col = 0;
                    if (x.b == RT_ARRAY) {
                        Value iv = pop(); splitIdx(iv, row, col); hasIdx = true;
                        inst = (int)pop().num();
                    } else if (x.b == RT_STACKTOP) {
                        inst = (int)pop().num();
                    }
                    Value out;
                    if (inst >= 0 && inst < 100000 && x.b != RT_NORMAL) {
                        // objeto/instancia desde la pila
                        readVar(inst, x.a, hasIdx, row, col, out);
                    } else readVar(inst, x.a, hasIdx, row, col, out);
                    push(std::move(out), 16);
                    break;
                }
                default: push(Value((double)x.a), 4); break;
            }
            pc++; break;

        case OP_POP: {
            if (x.flags & 4) { warnOnce("pop swap no soportado"); pc++; break; }
            int inst = x.inst; bool hasIdx = false; int row = 0, col = 0;
            Value v;
            if (x.t1 == DT_INT32) v = pop();
            if (x.b == RT_ARRAY) {
                Value iv = pop(); splitIdx(iv, row, col); hasIdx = true;
                inst = (int)pop().num();
            } else if (x.b == RT_STACKTOP) {
                inst = (int)pop().num();
            }
            if (x.t1 != DT_INT32) v = pop();
            writeVar(inst, x.a, hasIdx, row, col, v);
            pc++; break;
        }

        case OP_POPZ: st.pop_back(); pc++; break;

        case OP_DUP: {
            int bytes = (x.extra + 1) * kSize[x.t1 & 15];
            size_t n = 0; int acc = 0;
            while (acc < bytes && n < st.size() - base) { acc += st[st.size() - 1 - n].sz; n++; }
            size_t start = st.size() - n;
            for (size_t i = 0; i < n; i++) { Slot s = st[start + i]; st.push_back(s); }
            pc++; break;
        }

        case OP_CONV: {
            Slot& s = st.back();
            switch (x.t2) {
                case DT_STRING: if (!s.v.isStr()) { /* GMS1 no convierte implicitamente */ } break;
                case DT_BOOL: case DT_INT32: case DT_INT64: case DT_DOUBLE: case DT_FLOAT: case DT_INT16:
                    if (s.v.isStr() && x.t1 == DT_VAR) { /* deja la cadena (comparaciones) */ }
                    break;
                default: break;
            }
            s.sz = kSize[x.t2 & 15];
            pc++; break;
        }

        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
        case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR: {
            if (x.op == OP_ADD || x.op == OP_SUB || x.op == OP_MUL || x.op == OP_DIV) {
                // camino rapido: dos reales, operando sobre la pila sin mover valores
                size_t n = st.size(); Slot& sb = st[n - 1]; Slot& sa = st[n - 2];
                if (sa.v.t == VT::Real && sb.v.t == VT::Real && !(x.op == OP_DIV && sb.v.r == 0)) {
                    double A = sa.v.r, B = sb.v.r;
                    sa.v.r = x.op == OP_ADD ? A + B : x.op == OP_SUB ? A - B : x.op == OP_MUL ? A * B : A / B;
                    sa.sz = std::max(kSize[x.t1 & 15], kSize[x.t2 & 15]);
                    st.pop_back(); pc++; break;
                }
            }
            Value b = pop(); Value a = pop();
            uint8_t sz = std::max(kSize[x.t1 & 15], kSize[x.t2 & 15]);
            Value r; vmBinary(x.op, x.t1, x.t2, a, b, r);
            push(std::move(r), r.isStr() ? 16 : sz);
            pc++; break;
        }
        case OP_NEG: { Value a = pop(); push(Value(-a.num()), kSize[x.t1 & 15]); pc++; break; }
        case OP_NOT: {
            Value a = pop();
            if (x.t1 == DT_BOOL) push(Value(a.truthy() ? 0.0 : 1.0), 4);
            else push(Value((double)(~(int64_t)a.num())), kSize[x.t1 & 15]);
            pc++; break;
        }
        case OP_CMP: {
            {   // camino rapido: dos reales
                size_t n = st.size(); Slot& sb = st[n - 1]; Slot& sa = st[n - 2];
                if (sa.v.t == VT::Real && sb.v.t == VT::Real) {
                    const double eps = 0.00001; double diff = sa.v.r - sb.v.r; bool r = false;
                    switch (x.cmp) {
                        case 1: r = diff < -eps; break; case 2: r = diff <= eps; break; case 3: r = std::fabs(diff) <= eps; break;
                        case 4: r = std::fabs(diff) > eps; break; case 5: r = diff >= -eps; break; case 6: r = diff > eps; break;
                    }
                    const Ins& nx = ins[pc + 1 < nIns ? pc + 1 : pc];
                    if (pc + 1 < nIns && (nx.op == OP_BF || nx.op == OP_BT)) {   // comparacion + salto: sin apilar el resultado
                        st.pop_back(); st.pop_back(); insCount++;
                        pc = (nx.op == OP_BF ? !r : r) ? nx.a : pc + 2; break;
                    }
                    sa.v.r = r ? 1.0 : 0.0; sa.sz = 4;
                    st.pop_back(); pc++; break;
                }
            }
            Value b = pop(); Value a = pop();
            push(Value(vmCompare(x.cmp, a, b) ? 1.0 : 0.0), 4);
            pc++; break;
        }

        case OP_B: pc = x.a; break;
        case OP_BT: { bool tv = st.back().v.truthy(); st.pop_back(); pc = tv ? x.a : pc + 1; break; }
        case OP_BF: { bool tv = st.back().v.truthy(); st.pop_back(); pc = !tv ? x.a : pc + 1; break; }

        case OP_PUSHENV: {
            Value t = pop();
            WithFrame w; w.saved = ctx; w.idx = 0;
            collectTargets(t.num(), ctx, w.list);
            if (w.list.empty()) { pc = x.a; break; }
            withs.push_back(std::move(w));
            ctx.other = withs.back().saved.self;
            ctx.self = withs.back().list[0];
            pc++; break;
        }
        case OP_POPENV: {
            if (withs.empty()) { pc++; break; }
            if (x.flags & 1) { ctx = withs.back().saved; withs.pop_back(); pc++; break; }
            WithFrame& w = withs.back();
            w.idx++;
            while (w.idx < w.list.size() && !w.list[w.idx]->alive) w.idx++;
            if (w.idx < w.list.size()) { ctx.self = w.list[w.idx]; pc = x.a; }
            else { ctx = w.saved; withs.pop_back(); pc++; }
            break;
        }

        case OP_CALL: {
            int argcN = x.b;
            alignas(Value) unsigned char argBuf[sizeof(Value) * 16];
            std::vector<Value> big;
            Value* av;
            if (argcN <= 16) { av = reinterpret_cast<Value*>(argBuf); for (int i = 0; i < argcN; i++) new (&av[i]) Value(pop()); }
            else { big.resize(argcN); av = big.data(); for (int i = 0; i < argcN; i++) av[i] = pop(); }
            struct ArgGuard { Value* p; int n; bool on; ~ArgGuard() { if (on) for (int i = 0; i < n; i++) p[i].~Value(); } } guard{av, argcN, argcN <= 16};
            Value r = vmCallFn(x.a, ctx, av, argcN);
            push(std::move(r), 16);
            pc++; break;
        }

        case OP_RET: ret = pop(); pc = nIns; break;
        case OP_EXIT: pc = nIns; break;
        case OP_BREAK: pc++; break;

        default:
            warnOnce("opcode desconocido " + std::to_string(x.op) + " en " + code.name);
            pc++; break;
        }
    }
    st.resize(base);
    curArgs = savedArgs; curArgc = savedArgc; curCode = savedCode;
    if (profT0 > 0) {
        double el = profNow() - profT0;
        profCode[codeId] += el - profChild; profCodeN[codeId]++;
        profChild = profSavedChild + el;
    }
    return ret;
}

Value Game::runScript(int scriptIdx, Ctx& ctx, Value* args, int argc) {
    if (scriptIdx < 0 || scriptIdx >= (int)d.scripts.size()) { warnOnce("script_execute indice invalido " + std::to_string(scriptIdx)); return Value(); }
    return call(d.scripts[scriptIdx].code, ctx, args, argc);
}

} // namespace vs
