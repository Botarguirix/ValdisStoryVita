// AOT en la consola (5.6): cada codigo GML se traduce una sola vez, la primera vez que se ejecuta, a una lista de
// operaciones ya resueltas (sin decodificar instrucciones ni pila dinamica). Es la misma traduccion que hace
// tools/vsaot.cpp en el PC (pila convertida en ranuras fijas s0..sN, variables locales en ranuras, constantes
// incrustadas, comparar+saltar fusionado, atajo de colisiones contra objetos sin instancias) y cada operacion llama a
// los mismos ayudantes de aot_rt.h, ya compilados dentro del runner. No necesita PC ni compilador: lo hace la propia
// Vita con los datos del juego de Steam del usuario, y no deja nada derivado del juego en el VPK.
// La lista tiene forma de codigo de maquina (operacion normal, salto condicional, salto, fin) para poder emitirla
// tal cual como ARM en la Vita (fase 2, mas abajo; opcional con save/vaot_arm).
#include "aot_rt.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace vs {
void logf(const char* fmt, ...);
bool testFileExists(const std::string& dir, const std::string& base);   // debugtour.cpp
namespace vaot {
using namespace aot;

enum : uint8_t {
    OP_CONV = 7, OP_NEG = 17, OP_NOT = 18, OP_CMP = 21, OP_POP = 69, OP_PUSHI = 132, OP_DUP = 134, OP_RET = 156,
    OP_EXIT = 157, OP_POPZ = 158, OP_B = 182, OP_BT = 183, OP_BF = 184, OP_PUSHENV = 186, OP_POPENV = 187,
    OP_PUSH = 192, OP_PUSHLOC = 193, OP_PUSHGLB = 194, OP_PUSHBLTN = 195, OP_CALL = 217, OP_BREAK = 255 };
enum : uint8_t { DT_DOUBLE = 0, DT_FLOAT = 1, DT_INT32 = 2, DT_INT64 = 3, DT_VAR = 5, DT_STRING = 6, DT_INT16 = 15 };
enum { RT_ARRAY = 0, RT_STACKTOP = 128, RT_NORMAL = 160 };
static const uint8_t kSize[16] = {8, 4, 4, 8, 4, 16, 16, 4, 4, 4, 4, 4, 4, 4, 4, 4};
static bool isPush(uint8_t op) { return op == OP_PUSH || op == OP_PUSHLOC || op == OP_PUSHGLB || op == OP_PUSHBLTN || op == OP_PUSHI; }
static bool isBinOp(uint8_t op) { return op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_REM || op == OP_MOD ||
                                         op == OP_AND || op == OP_OR || op == OP_XOR || op == OP_SHL || op == OP_SHR; }

// ---------------------------------------------------------------- analisis (igual que vsaot.cpp)
struct Analysis {
    bool ok = true; std::string why;
    std::vector<int> depth; std::vector<std::vector<uint8_t>> sizes;
    std::set<int> labels; int maxDepth = 0; bool usesWith = false; std::set<int> locals;
};
static Analysis analyze(const Code& c) {
    Analysis A; const int n = (int)c.ins.size();
    A.depth.assign(n + 1, -1); A.sizes.assign(n + 1, {});
    std::vector<int> work;
    auto flow = [&](int to, const std::vector<uint8_t>& st) {
        if (to < 0 || to > n) { A.ok = false; A.why = "salto fuera"; return; }
        if (A.depth[to] < 0) { A.depth[to] = (int)st.size(); A.sizes[to] = st; work.push_back(to); }
        else if (A.sizes[to] != st) { A.ok = false; A.why = "pila inconsistente"; }
    };
    flow(0, {});
    while (!work.empty() && A.ok) {
        int pc = work.back(); work.pop_back();
        if (pc == n) continue;
        std::vector<uint8_t> st = A.sizes[pc];
        const Ins& x = c.ins[pc];
        auto pop = [&](int k) { if ((int)st.size() < k) { A.ok = false; A.why = "pila vacia"; return false; } st.resize(st.size() - k); return true; };
        auto push = [&](uint8_t sz) { st.push_back(sz); A.maxDepth = std::max(A.maxDepth, (int)st.size()); };
        if (isPush(x.op)) {
            if (x.t1 == DT_VAR) {
                if (x.b == RT_ARRAY) { if (!pop(2)) break; A.locals.insert(x.a); }
                else if (x.b == RT_STACKTOP) { if (!pop(1)) break; A.locals.insert(x.a); }
                if (x.inst == IT_LOCAL) A.locals.insert(x.a);
                push(16);
            } else {
                uint8_t sz = 4;
                switch (x.t1) { case DT_DOUBLE: case DT_INT64: sz = 8; break; case DT_STRING: sz = 16; break; default: sz = 4; }
                push(sz);
            }
            flow(pc + 1, st);
        } else switch (x.op) {
            case OP_POP:
                if (x.flags & 4) { A.ok = false; A.why = "pop swap"; break; }
                if (x.b == RT_ARRAY) { if (!pop(3)) break; A.locals.insert(x.a); }
                else if (x.b == RT_STACKTOP) { if (!pop(2)) break; A.locals.insert(x.a); }
                else if (!pop(1)) break;
                if (x.inst == IT_LOCAL) A.locals.insert(x.a);
                flow(pc + 1, st); break;
            case OP_POPZ: if (!pop(1)) break; flow(pc + 1, st); break;
            case OP_DUP: {
                int bytes = (x.extra + 1) * kSize[x.t1 & 15], acc = 0; size_t k = 0;
                while (acc < bytes && k < st.size()) { acc += st[st.size() - 1 - k]; k++; }
                size_t start = st.size() - k;
                for (size_t i = 0; i < k; i++) push(st[start + i]);
                flow(pc + 1, st); break;
            }
            case OP_CONV: if (st.empty()) { A.ok = false; A.why = "conv"; break; } st.back() = kSize[x.t2 & 15]; flow(pc + 1, st); break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
            case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR:
                if (!pop(2)) break; push(std::max(kSize[x.t1 & 15], kSize[x.t2 & 15])); flow(pc + 1, st); break;
            case OP_NEG: case OP_NOT: if (!pop(1)) break; push(kSize[x.t1 & 15]); flow(pc + 1, st); break;
            case OP_CMP: if (!pop(2)) break; push(4); flow(pc + 1, st); break;
            case OP_B: A.labels.insert(x.a); flow(x.a, st); break;
            case OP_BT: case OP_BF: if (!pop(1)) break; A.labels.insert(x.a); flow(x.a, st); flow(pc + 1, st); break;
            case OP_PUSHENV: A.usesWith = true; if (!pop(1)) break; A.labels.insert(x.a); flow(x.a, st); flow(pc + 1, st); break;
            case OP_POPENV: A.usesWith = true; if (!(x.flags & 1)) { A.labels.insert(x.a); flow(x.a, st); } flow(pc + 1, st); break;
            case OP_CALL: if (!pop(x.b)) break; push(16); flow(pc + 1, st); break;
            case OP_RET: if (!pop(1)) break; break;
            case OP_EXIT: break;
            case OP_BREAK: flow(pc + 1, st); break;
            default: A.ok = false; A.why = "opcode"; break;
        }
    }
    return A;
}

// ---------------------------------------------------------------- operaciones
struct Fr {
    Game* g; Ctx* ctx; Value* s; Value* L; std::vector<With>* withs;
    int row = 0, col = 0, inst = 0; Value ret;
};
struct Op;
using PlainFn = void (*)(Fr&, const Op&);
using CondFn = bool (*)(Fr&, const Op&);
enum Kind : uint8_t { K_PLAIN, K_COND, K_JUMP, K_RET };
struct Op {
    void* fn = nullptr; uint8_t kind = K_PLAIN, t1 = 0, t2 = 0, u = 0;
    int16_t a = 0, c = 0;             // a: ranura principal; c: variable local (o lo que diga la operacion)
    int b = 0, d = 0;                 // variable, instancia, posiciones...
    int jump = -1;                    // destino (indice de operacion) de K_COND / K_JUMP
    double k = 0;                     // constante (32 bytes por operacion en la Vita)
};
struct Fr;
using NativeFn = void (*)(Fr*, void**);
struct Prog { std::vector<Op> ops; int nSlots = 0, nLocals = 0; bool usesWith = false; NativeFn native = nullptr; };

#define F_G (*F.g)
#define F_C (*F.ctx)
// empujes
static void hConst(Fr& F, const Op& o) { F.s[o.a] = Value(o.k); }
static void hStr(Fr& F, const Op& o) { pushStr(F_G, F.s[o.a], o.b); }
static void hLocal(Fr& F, const Op& o) { F.s[o.a] = F.L[o.c]; }
static void hSelf(Fr& F, const Op& o) { readSelf(F_G, F_C, o.b, F.s[o.a]); }
static void hGlobal(Fr& F, const Op& o) { readGlobal(F_G, F_C, o.b, F.s[o.a]); }
static void hOther(Fr& F, const Op& o) { F_G.vmRead(F_C, o.d, o.b, false, 0, 0, F.s[o.a]); }
static void hPushArr(Fr& F, const Op& o) {   // a = d: indice en s[d-1], instancia en s[d-2]
    Value* s = F.s; int d = o.a;
    split(s[d - 1], F.row, F.col); F.inst = (int)s[d - 2].num(); drop(s[d - 1]);
    if (F.inst == IT_LOCAL) s[d - 2] = F.L[o.c].get(F.row, F.col); else F_G.vmRead(F_C, F.inst, o.b, true, F.row, F.col, s[d - 2]);
}
static void hPushTop(Fr& F, const Op& o) {
    Value* s = F.s; int d = o.a;
    F.inst = (int)s[d - 1].num();
    if (F.inst == IT_LOCAL) s[d - 1] = F.L[o.c]; else F_G.vmRead(F_C, F.inst, o.b, false, 0, 0, s[d - 1]);
}
// escrituras (a = vpos, c = local, b = variable, d = inst o posiciones)
static void hPopArr(Fr& F, const Op& o) {   // a vpos, d xpos, u... ipos en o.k
    Value* s = F.s; int vpos = o.a, xpos = o.d, ipos = (int)o.k;
    split(s[xpos], F.row, F.col); F.inst = (int)s[ipos].num();
    if (F.inst == IT_LOCAL) F.L[o.c].set(F.row, F.col, s[vpos]); else F_G.vmWrite(F_C, F.inst, o.b, true, F.row, F.col, s[vpos]);
    drop(s[vpos]); drop(s[xpos]); drop(s[ipos]);
}
static void hPopTop(Fr& F, const Op& o) {
    Value* s = F.s; int vpos = o.a, ipos = o.d;
    F.inst = (int)s[ipos].num();
    if (F.inst == IT_LOCAL) F.L[o.c] = std::move(s[vpos]); else { F_G.vmWrite(F_C, F.inst, o.b, false, 0, 0, s[vpos]); drop(s[vpos]); }
    drop(s[ipos]);
}
static void hPopLocal(Fr& F, const Op& o) { F.L[o.c] = std::move(F.s[o.a]); }
static void hPopSelf(Fr& F, const Op& o) { writeSelf(F_G, F_C, o.b, F.s[o.a]); }
static void hPopGlobal(Fr& F, const Op& o) { writeGlobal(F_G, F_C, o.b, F.s[o.a]); }
static void hPopOther(Fr& F, const Op& o) { F_G.vmWrite(F_C, o.d, o.b, false, 0, 0, F.s[o.a]); drop(F.s[o.a]); }
static void hPopz(Fr& F, const Op& o) { drop(F.s[o.a]); }
static void hDup(Fr& F, const Op& o) { for (int i = 0; i < o.b; i++) F.s[o.a + i] = F.s[o.d + i]; }   // a destino, d origen, b cuantas
// aritmetica
template <uint8_t OP> static void hBin(Fr& F, const Op& o) { bin<OP>(F_G, F.s[o.a], F.s[o.a + 1], o.t1, o.t2); }
template <uint8_t OP> static void hBinK(Fr& F, const Op& o) { binK<OP>(F_G, F.s[o.a], o.k, o.t1, o.t2); }
static void hNeg(Fr& F, const Op& o) { neg(F.s[o.a]); }
static void hNot(Fr& F, const Op& o) { notOp(F.s[o.a], o.u != 0); }
static void hCmp(Fr& F, const Op& o) { bool r = cmp(F_G, o.u, F.s[o.a], F.s[o.a + 1]); F.s[o.a] = Value(r ? 1.0 : 0.0); }
static void hCmpK(Fr& F, const Op& o) { bool r = cmpK(F_G, o.u, F.s[o.a], o.k); F.s[o.a] = Value(r ? 1.0 : 0.0); }
// saltos condicionales: devuelven true para saltar (t1 = 1: saltar si se cumple; 0: si no se cumple)
static bool cCmp(Fr& F, const Op& o) { return cmp(F_G, o.u, F.s[o.a], F.s[o.a + 1]) == (o.t1 != 0); }
static bool cCmpK(Fr& F, const Op& o) { return cmpK(F_G, o.u, F.s[o.a], o.k) == (o.t1 != 0); }
static bool cTruth(Fr& F, const Op& o) { bool t = F.s[o.a].truthy(); drop(F.s[o.a]); return t == (o.t1 != 0); }
static bool cGuard(Fr& F, const Op& o) {   // atajo de colisiones: sin instancias del objeto, resultado fijo y se salta la llamada
    if (!F_G.instancesOf(o.b).empty()) return false;
    F.s[o.a] = Value(o.k); return true;
}
static bool cPushEnv(Fr& F, const Op& o) {
    With w; w.saved = F_C; w.idx = 0; double t = F.s[o.a].num(); drop(F.s[o.a]); F_G.collectTargets(t, F_C, w.list);
    if (w.list.empty()) return true;
    F.withs->push_back(std::move(w)); F_C.other = F.withs->back().saved.self; F_C.self = F.withs->back().list[0];
    return false;
}
static void hPopEnvBreak(Fr& F, const Op&) { if (!F.withs->empty()) { F_C = F.withs->back().saved; F.withs->pop_back(); } }
static bool cPopEnv(Fr& F, const Op&) {
    if (F.withs->empty()) return false;
    With& w = F.withs->back(); w.idx++;
    while (w.idx < w.list.size() && !w.list[w.idx]->alive) w.idx++;
    if (w.idx < w.list.size()) { F_C.self = w.list[w.idx]; return true; }
    F_C = w.saved; F.withs->pop_back(); return false;
}
// llamadas (a = d - na, resultado ahi; b = funcion; c = na)
static void hCall(Fr& F, const Op& o) {
    int na = o.c, d = o.a + na;
    if (na == 0) { F.s[o.a] = F_G.vmCallFn(o.b, F_C, nullptr, 0); return; }
    Value small[8]; std::vector<Value> big; Value* av = small;
    if (na > 8) { big.resize(na); av = big.data(); }
    for (int i = 0; i < na; i++) av[i] = std::move(F.s[d - 1 - i]);
    if (o.u) F.s[o.a] = F_G.runScript(av[0].ival(), F_C, av + 1, na - 1);   // script_execute: directo
    else F.s[o.a] = F_G.vmCallFn(o.b, F_C, av, na);
}
static void hRet(Fr& F, const Op& o) { F.ret = std::move(F.s[o.a]); }
#undef F_G
#undef F_C

// ---------------------------------------------------------------- traduccion
static Prog* build(const GameData& g, const Code& c, const Analysis& A) {
    const int n = (int)c.ins.size();
    Prog* P = new Prog; P->nSlots = A.maxDepth + 1; P->usesWith = A.usesWith;
    std::map<int, int> li; for (int v : A.locals) { int k = (int)li.size(); li[v] = k; }
    P->nLocals = (int)li.size();
    auto L = [&](int v) { auto it = li.find(v); return it == li.end() ? 0 : it->second; };
    std::set<int> need = A.labels;
    for (int pc = 0; pc < n; pc++) if (A.depth[pc] >= 0 && c.ins[pc].op == OP_CMP && pc + 1 < n && (c.ins[pc + 1].op == OP_BT || c.ins[pc + 1].op == OP_BF)) need.insert(pc + 2);
    auto constLit = [&](const Ins& p, double& v) -> bool {
        if (!isPush(p.op)) return false;
        switch (p.t1) {
            case DT_DOUBLE: case DT_INT64: v = g.doubles[p.a]; return true;
            case DT_INT32: case DT_INT16: case 4 /*DT_BOOL*/: v = (double)p.a; return true;
            default: return false;
        }
    };
    struct Guard { int callPc, obj; double none; };
    std::map<int, Guard> guards;
    for (int pc = 0; pc < n; pc++) {   // mismo atajo de colisiones que vsaot
        const Ins& x = c.ins[pc];
        if (x.op != OP_CALL || A.depth[pc] < 0 || x.a < 0 || x.a >= (int)g.functions.size()) continue;
        const std::string& fn = g.functions[x.a]; int na = x.b, objArg = -1; double none = -4;
        if (fn == "collision_rectangle" || fn == "collision_line") { if (na == 7) objArg = 4; }
        else if (fn == "collision_point") { if (na == 5) objArg = 2; }
        else if (fn == "collision_circle") { if (na == 6) objArg = 3; }
        else if (fn == "instance_place") { if (na == 3) objArg = 2; }
        else if (fn == "place_meeting" || fn == "position_meeting") { if (na == 3) { objArg = 2; none = 0; } }
        if (objArg < 0) continue;
        std::vector<int> pushes; int q = pc - 1; bool ok = true;
        while ((int)pushes.size() < na && q >= 0 && ok) {
            const Ins& y = c.ins[q];
            if (y.op == OP_CONV) { q--; continue; }
            if (!isPush(y.op)) { ok = false; break; }
            if (y.t1 == DT_VAR && (y.b == RT_ARRAY || y.b == RT_STACKTOP || !(y.inst == IT_SELF || y.inst == -5 || y.inst == IT_LOCAL))) { ok = false; break; }
            if (y.t1 == DT_STRING) { ok = false; break; }
            pushes.push_back(q); q--;
        }
        if (!ok || (int)pushes.size() != na) continue;
        int first = pushes.back();
        for (int k = first + 1; k <= pc; k++) if (need.count(k)) { ok = false; break; }
        if (!ok) continue;
        const Ins& o = c.ins[pushes[objArg]];
        if (!(o.t1 == DT_INT32 || o.t1 == DT_INT16) || o.a < 0 || o.a >= (int)g.objects.size()) continue;
        if (A.depth[first] < 0 || A.depth[pc + 1 < n ? pc + 1 : pc] < 0) continue;
        guards[first] = Guard{pc, o.a, none};
        need.insert(pc + 1);
    }
    std::vector<int> at(n + 1, -1);                 // pc -> primera operacion emitida ahi
    std::vector<std::pair<int, int>> fix;            // (operacion, pc destino)
    auto emit = [&](void* fn, uint8_t kind) -> Op& { P->ops.emplace_back(); Op& o = P->ops.back(); o.fn = fn; o.kind = kind; return o; };
    auto jumpTo = [&](int target) { fix.push_back({(int)P->ops.size() - 1, target}); };
    for (int pc = 0; pc < n; pc++) {
        if (A.depth[pc] < 0) continue;
        at[pc] = (int)P->ops.size();
        const Ins& x = c.ins[pc]; const int d = A.depth[pc];
        if (auto gi = guards.find(pc); gi != guards.end()) {
            Op& o = emit((void*)cGuard, K_COND); o.a = d; o.b = gi->second.obj; o.k = gi->second.none; jumpTo(gi->second.callPc + 1);
        }
        {
            double lit;
            if (pc + 1 < n && !need.count(pc + 1) && A.depth[pc + 1] >= 0 && constLit(x, lit)) {
                const Ins& y = c.ins[pc + 1];
                if (y.op == OP_CMP) {
                    if (pc + 2 < n && (c.ins[pc + 2].op == OP_BT || c.ins[pc + 2].op == OP_BF)) {
                        const Ins& nx = c.ins[pc + 2];
                        Op& o = emit((void*)cCmpK, K_COND); o.a = d - 1; o.u = y.cmp; o.k = lit; o.t1 = nx.op == OP_BT; jumpTo(nx.a);
                        Op& j = emit(nullptr, K_JUMP); (void)j; jumpTo(pc + 3);
                    } else { Op& o = emit((void*)hCmpK, K_PLAIN); o.a = d - 1; o.u = y.cmp; o.k = lit; }
                    pc++; continue;
                }
                if (isBinOp(y.op)) {
                    void* fn = nullptr;
                    switch (y.op) {
                        case OP_ADD: fn = (void*)hBinK<OP_ADD>; break; case OP_SUB: fn = (void*)hBinK<OP_SUB>; break;
                        case OP_MUL: fn = (void*)hBinK<OP_MUL>; break; case OP_DIV: fn = (void*)hBinK<OP_DIV>; break;
                        case OP_REM: fn = (void*)hBinK<OP_REM>; break; case OP_MOD: fn = (void*)hBinK<OP_MOD>; break;
                        case OP_AND: fn = (void*)hBinK<OP_AND>; break; case OP_OR: fn = (void*)hBinK<OP_OR>; break;
                        case OP_XOR: fn = (void*)hBinK<OP_XOR>; break; case OP_SHL: fn = (void*)hBinK<OP_SHL>; break;
                        default: fn = (void*)hBinK<OP_SHR>; break;
                    }
                    Op& o = emit(fn, K_PLAIN); o.a = d - 1; o.k = lit; o.t1 = y.t1; o.t2 = y.t2;
                    pc++; continue;
                }
            }
        }
        if (isPush(x.op)) {
            switch (x.t1) {
                case DT_DOUBLE: case DT_INT64: { Op& o = emit((void*)hConst, K_PLAIN); o.a = d; o.k = g.doubles[x.a]; break; }
                case DT_STRING: { Op& o = emit((void*)hStr, K_PLAIN); o.a = d; o.b = x.a; break; }
                case DT_VAR:
                    if (x.b == RT_ARRAY) { Op& o = emit((void*)hPushArr, K_PLAIN); o.a = d; o.b = x.a; o.c = L(x.a); }
                    else if (x.b == RT_STACKTOP) { Op& o = emit((void*)hPushTop, K_PLAIN); o.a = d; o.b = x.a; o.c = L(x.a); }
                    else if (x.inst == IT_LOCAL) { Op& o = emit((void*)hLocal, K_PLAIN); o.a = d; o.c = L(x.a); }
                    else if (x.inst == IT_SELF) { Op& o = emit((void*)hSelf, K_PLAIN); o.a = d; o.b = x.a; }
                    else if (x.inst == -5) { Op& o = emit((void*)hGlobal, K_PLAIN); o.a = d; o.b = x.a; }
                    else { Op& o = emit((void*)hOther, K_PLAIN); o.a = d; o.b = x.a; o.d = x.inst; }
                    break;
                default: { Op& o = emit((void*)hConst, K_PLAIN); o.a = d; o.k = (double)x.a; break; }
            }
            continue;
        }
        switch (x.op) {
            case OP_POP: {
                int vpos, ipos = -1, xpos = -1;
                if (x.b == RT_ARRAY) { if (x.t1 == DT_INT32) { vpos = d - 1; xpos = d - 2; ipos = d - 3; } else { xpos = d - 1; ipos = d - 2; vpos = d - 3; } }
                else if (x.b == RT_STACKTOP) { if (x.t1 == DT_INT32) { vpos = d - 1; ipos = d - 2; } else { ipos = d - 1; vpos = d - 2; } }
                else vpos = d - 1;
                if (x.b == RT_ARRAY) { Op& o = emit((void*)hPopArr, K_PLAIN); o.a = vpos; o.d = xpos; o.k = ipos; o.b = x.a; o.c = L(x.a); }
                else if (x.b == RT_STACKTOP) { Op& o = emit((void*)hPopTop, K_PLAIN); o.a = vpos; o.d = ipos; o.b = x.a; o.c = L(x.a); }
                else if (x.inst == IT_LOCAL) { Op& o = emit((void*)hPopLocal, K_PLAIN); o.a = vpos; o.c = L(x.a); }
                else if (x.inst == IT_SELF) { Op& o = emit((void*)hPopSelf, K_PLAIN); o.a = vpos; o.b = x.a; }
                else if (x.inst == -5) { Op& o = emit((void*)hPopGlobal, K_PLAIN); o.a = vpos; o.b = x.a; }
                else { Op& o = emit((void*)hPopOther, K_PLAIN); o.a = vpos; o.b = x.a; o.d = x.inst; }
                break;
            }
            case OP_POPZ: { Op& o = emit((void*)hPopz, K_PLAIN); o.a = d - 1; break; }
            case OP_DUP: {
                const auto& st = A.sizes[pc];
                int bytes = (x.extra + 1) * kSize[x.t1 & 15], acc = 0; size_t k = 0;
                while (acc < bytes && k < st.size()) { acc += st[st.size() - 1 - k]; k++; }
                Op& o = emit((void*)hDup, K_PLAIN); o.a = d; o.d = d - (int)k; o.b = (int)k;
                break;
            }
            case OP_CONV: case OP_BREAK: break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
            case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR: {
                void* fn;
                switch (x.op) {
                    case OP_ADD: fn = (void*)hBin<OP_ADD>; break; case OP_SUB: fn = (void*)hBin<OP_SUB>; break;
                    case OP_MUL: fn = (void*)hBin<OP_MUL>; break; case OP_DIV: fn = (void*)hBin<OP_DIV>; break;
                    case OP_REM: fn = (void*)hBin<OP_REM>; break; case OP_MOD: fn = (void*)hBin<OP_MOD>; break;
                    case OP_AND: fn = (void*)hBin<OP_AND>; break; case OP_OR: fn = (void*)hBin<OP_OR>; break;
                    case OP_XOR: fn = (void*)hBin<OP_XOR>; break; case OP_SHL: fn = (void*)hBin<OP_SHL>; break;
                    default: fn = (void*)hBin<OP_SHR>; break;
                }
                Op& o = emit(fn, K_PLAIN); o.a = d - 2; o.t1 = x.t1; o.t2 = x.t2; break;
            }
            case OP_NEG: { Op& o = emit((void*)hNeg, K_PLAIN); o.a = d - 1; break; }
            case OP_NOT: { Op& o = emit((void*)hNot, K_PLAIN); o.a = d - 1; o.u = x.t1 == 4; break; }
            case OP_CMP:
                if (pc + 1 < n && (c.ins[pc + 1].op == OP_BT || c.ins[pc + 1].op == OP_BF)) {
                    const Ins& nx = c.ins[pc + 1];
                    Op& o = emit((void*)cCmp, K_COND); o.a = d - 2; o.u = x.cmp; o.t1 = nx.op == OP_BT; jumpTo(nx.a);
                    emit(nullptr, K_JUMP); jumpTo(pc + 2);
                } else { Op& o = emit((void*)hCmp, K_PLAIN); o.a = d - 2; o.u = x.cmp; }
                break;
            case OP_B: emit(nullptr, K_JUMP); jumpTo(x.a); break;
            case OP_BT: case OP_BF: { Op& o = emit((void*)cTruth, K_COND); o.a = d - 1; o.t1 = x.op == OP_BT; jumpTo(x.a); break; }
            case OP_PUSHENV: { Op& o = emit((void*)cPushEnv, K_COND); o.a = d - 1; jumpTo(x.a); break; }
            case OP_POPENV:
                if (x.flags & 1) emit((void*)hPopEnvBreak, K_PLAIN);
                else { emit((void*)cPopEnv, K_COND); jumpTo(x.a); }
                break;
            case OP_CALL: {
                Op& o = emit((void*)hCall, K_PLAIN); o.a = d - x.b; o.b = x.a; o.c = x.b;
                o.u = x.b > 0 && x.a >= 0 && x.a < (int)g.functions.size() && g.functions[x.a] == "script_execute";
                break;
            }
            case OP_RET: { Op& o = emit((void*)hRet, K_RET); o.a = d - 1; break; }
            case OP_EXIT: emit(nullptr, K_RET); break;
        }
    }
    at[n] = (int)P->ops.size();
    emit(nullptr, K_RET);
    for (auto& [op, target] : fix) {
        int t = target;
        while (t <= n && at[t] < 0) t++;   // destino sin operacion propia (CONV, BREAK): la siguiente
        P->ops[op].jump = at[std::min(t, n)];
    }
    return P;
}

// ---------------------------------------------------------------- fase 2: codigo ARM (Thumb-2) generado en la consola
// Cada operacion pasa a ser una llamada directa a su ayudante y cada salto del juego un salto del procesador: sin el
// bucle de despacho de run(). Funcion generada: void f(Fr* F, void** tabla)  (r4 = F, r6 = tabla de ayudantes)
//   push {r4, r5, r6, lr}; mov r4, r0; mov r6, r1
//   por operacion: mov r0, r4; movw/movt r1, &op; ldr.w ip, [r6, #k*4]; blx ip
//     K_COND: ... + lsls r0, r0, #24; bne.w destino      K_JUMP: b.w destino      K_RET: ... + b.w fin
//   fin: pop {r4, r5, r6, pc}
// En PC se genera igual (VS_VAOTARM_DUMP) para revisarlo con objdump; solo la Vita lo ejecuta.
static void* gFnTab[256]; static int gFnCount = 0;
static int fnIndex(void* fn) {
    for (int i = 0; i < gFnCount; i++) if (gFnTab[i] == fn) return i;
    if (gFnCount >= 256) return -1;
    gFnTab[gFnCount] = fn; return gFnCount++;
}
struct ArmOut { std::vector<uint16_t> h; std::vector<int> label; int epilogue = 0; };
static void movImm(std::vector<uint16_t>& h, int rd, uint32_t v, bool top) {   // movw (top = false) / movt (top = true)
    uint32_t imm = top ? (v >> 16) : (v & 0xFFFF);
    uint16_t i = (imm >> 11) & 1, imm4 = (imm >> 12) & 15, imm3 = (imm >> 8) & 7, imm8 = imm & 255;
    h.push_back((uint16_t)((top ? 0xF2C0 : 0xF240) | (i << 10) | imm4));
    h.push_back((uint16_t)((imm3 << 12) | (rd << 8) | imm8));
}
static bool branchW(uint16_t* at, int fromHw, int toHw, int cond) {   // cond < 0: b.w (T4); si no, b<cond>.w (T3)
    int32_t off = (toHw - (fromHw + 2)) * 2;   // PC = direccion de la instruccion + 4
    if (cond < 0) {
        if (off < -(1 << 24) || off >= (1 << 24)) return false;
        uint32_t S = (off >> 24) & 1, I1 = (off >> 23) & 1, I2 = (off >> 22) & 1, imm10 = (off >> 12) & 0x3FF, imm11 = (off >> 1) & 0x7FF;
        uint32_t J1 = (~I1 ^ S) & 1, J2 = (~I2 ^ S) & 1;
        at[0] = (uint16_t)(0xF000 | (S << 10) | imm10); at[1] = (uint16_t)(0x9000 | (J1 << 13) | (J2 << 11) | imm11);
    } else {
        if (off < -(1 << 20) || off >= (1 << 20)) return false;
        uint32_t S = (off >> 20) & 1, J2 = (off >> 19) & 1, J1 = (off >> 18) & 1, imm6 = (off >> 12) & 0x3F, imm11 = (off >> 1) & 0x7FF;
        at[0] = (uint16_t)(0xF000 | (S << 10) | ((uint32_t)cond << 6) | imm6); at[1] = (uint16_t)(0x8000 | (J1 << 13) | (J2 << 11) | imm11);
    }
    return true;
}
static bool armEmit(const Prog& P, ArmOut& A) {
    auto& h = A.h; h.clear();
    struct Fix { int at, op; int cond; };   // op = -1: al final
    std::vector<Fix> fix;
    h.push_back(0xB570); h.push_back(0x4604); h.push_back(0x460E);   // push {r4-r6,lr}; mov r4, r0; mov r6, r1
    A.label.assign(P.ops.size(), 0);
    for (size_t i = 0; i < P.ops.size(); i++) {
        const Op& o = P.ops[i];
        A.label[i] = (int)h.size();
        if (o.fn) {
            int k = fnIndex(o.fn); if (k < 0) return false;
            uint32_t addr = (uint32_t)(uintptr_t)&o;
            h.push_back(0x4620);                                  // mov r0, r4
            movImm(h, 1, addr, false); movImm(h, 1, addr, true);  // r1 = &op
            h.push_back(0xF8D6); h.push_back((uint16_t)(0xC000 | (k * 4)));   // ldr.w ip, [r6, #k*4]
            h.push_back(0x47E0);                                  // blx ip
        }
        if (o.kind == K_COND) { h.push_back(0x0600); fix.push_back({(int)h.size(), o.jump, 1}); h.push_back(0); h.push_back(0); }   // lsls r0,r0,#24; bne.w
        else if (o.kind == K_JUMP) { fix.push_back({(int)h.size(), o.jump, -1}); h.push_back(0); h.push_back(0); }
        else if (o.kind == K_RET) { fix.push_back({(int)h.size(), -1, -1}); h.push_back(0); h.push_back(0); }
    }
    A.epilogue = (int)h.size();
    h.push_back(0xBD70);                                          // pop {r4-r6,pc}
    if (h.size() & 1) h.push_back(0xBF00);                        // nop: tamano multiplo de 4
    for (auto& f : fix) {
        int to = f.op < 0 ? A.epilogue : A.label[f.op];
        if (!branchW(&h[f.at], f.at, to, f.cond)) return false;
    }
    return true;
}
#ifdef __vita__
}  // namespace vaot
}  // namespace vs
#include <psp2/kernel/sysmem.h>
namespace vs {
namespace vaot {
// Memoria ejecutable en bloques de 1 MB (sceKernelAllocMemBlockForVM, como los dynarec de la Vita)
static uint8_t* gCodeBase = nullptr; static SceUID gCodeBlock = -1; static size_t gCodeUsed = 0, gCodeCap = 0, gCodeTotal = 0;
static bool gArmBroken = false;
static NativeFn armInstall(const ArmOut& A) {
    size_t bytes = A.h.size() * 2;
    if (gArmBroken || bytes > (1u << 20)) return nullptr;
    if (!gCodeBase || gCodeUsed + bytes > gCodeCap) {
        SceUID b = sceKernelAllocMemBlockForVM("vaot_code", 1u << 20);
        void* base = nullptr;
        if (b < 0 || sceKernelGetMemBlockBase(b, &base) < 0 || !base) {
            logf("[vaot] sin memoria ejecutable (0x%08X): se sigue sin codigo ARM\n", (unsigned)b); gArmBroken = true; return nullptr;
        }
        gCodeBlock = b; gCodeBase = (uint8_t*)base; gCodeUsed = 0; gCodeCap = 1u << 20;
        logf("[vaot] bloque de codigo ARM %u MB\n", (unsigned)((gCodeTotal >> 20) + 1));   // la Vita no entiende %zu
        gCodeTotal += gCodeCap;
    }
    uint8_t* dst = gCodeBase + gCodeUsed;
    sceKernelOpenVMDomain();
    std::memcpy(dst, A.h.data(), bytes);
    sceKernelCloseVMDomain();
    sceKernelSyncVMDomain(gCodeBlock, dst, bytes);
    gCodeUsed += (bytes + 15) & ~(size_t)15;
    return (NativeFn)(uintptr_t)((uintptr_t)dst | 1);   // Thumb
}
#endif
// ---------------------------------------------------------------- ejecucion
// Ranuras de todas las llamadas en curso en un bloque fijo (sin reservar memoria en cada llamada)
static Value gArena[32768]; static int gTop = 0;

static Value run(const Prog& P, Game& g, Ctx& ctx) {
    int need = P.nSlots + P.nLocals;
    std::vector<Value> heap; Value* base;
    bool inArena = gTop + need <= (int)(sizeof(gArena) / sizeof(gArena[0]));
    if (inArena) { base = gArena + gTop; gTop += need; } else { heap.resize(need); base = heap.data(); }
    std::vector<With> withs;
    Fr F; F.g = &g; F.ctx = &ctx; F.s = base; F.L = base + P.nSlots; F.withs = &withs;
    if (P.native) { P.native(&F, gFnTab); goto done; }   // fase 2: codigo ARM generado en la consola
    {
    const Op* ops = P.ops.data(); int pc = 0;
    for (;;) {
        const Op& o = ops[pc];
        switch (o.kind) {
            case K_PLAIN: ((PlainFn)o.fn)(F, o); pc++; break;
            case K_COND: pc = ((CondFn)o.fn)(F, o) ? o.jump : pc + 1; break;
            case K_JUMP: pc = o.jump; break;
            default: if (o.fn) ((PlainFn)o.fn)(F, o); goto done;
        }
    }
    }
done:
    if (inArena) { gTop -= need; for (int i = 0; i < need; i++) base[i] = Value(); }   // ranuras y locales limpios para la siguiente llamada
    return std::move(F.ret);
}

static std::vector<Prog*> gProgs; static std::vector<uint8_t> gState;   // 0 sin mirar, 1 traducido, 2 al interprete
static size_t gBuilt = 0, gRejected = 0, gOpsBytes = 0;
// Fase 2: genera el codigo ARM de un programa recien traducido (Vita). En PC, VS_VAOTARM_DUMP=carpeta guarda el codigo
// de los primeros programas (.bin) y sus etiquetas (.txt) para revisarlos con objdump.
static void armPrepare(Game& g, int codeId, Prog& P) {
    static int on = -1, dumped = 0; static const char* dump = std::getenv("VS_VAOTARM_DUMP");
    if (on < 0) {
#ifdef __vita__
        // Opcional hasta medirlo en la Vita (decision del usuario para v0.9.0): se activa con save/vaot_arm
        on = testFileExists(g.saveDir, "vaot_arm");
        logf("[vaot] codigo ARM en la consola %s\n", on ? "activado (save/vaot_arm)" : "desactivado (se activa con save/vaot_arm)");
#else
        on = dump != nullptr;
#endif
    }
    if (!on) return;
    ArmOut A;
    if (!armEmit(P, A)) return;
    if (dump && dumped < 40) {
        dumped++;
        std::string base = std::string(dump) + "/c" + std::to_string(codeId);
        if (FILE* f = std::fopen((base + ".bin").c_str(), "wb")) { std::fwrite(A.h.data(), 2, A.h.size(), f); std::fclose(f); }
        if (FILE* f = std::fopen((base + ".txt").c_str(), "w")) {
            std::fprintf(f, "epilogo %d\n", A.epilogue * 2);
            for (size_t i = 0; i < P.ops.size(); i++) {
                const Op& o = P.ops[i];
                std::fprintf(f, "%zu %d %d %d %d %u\n", i, A.label[i] * 2, (int)o.kind, o.jump >= 0 ? A.label[o.jump] * 2 : -1,
                             o.fn ? fnIndex(o.fn) : -1, (unsigned)(uint32_t)(uintptr_t)&o);
            }
            std::fclose(f);
        }
    }
#ifdef __vita__
    P.native = armInstall(A);
#endif
    (void)g; (void)codeId;
}
}  // namespace vaot

bool gVaotOn = false;
// Ejecuta el codigo traducido; false si este codigo se deja al interprete (construcciones no soportadas)
bool vaotRun(Game& g, int codeId, Ctx& ctx, Value& out) {
    using namespace vaot;
    if (gProgs.size() != g.d.codes.size()) {
        gProgs.assign(g.d.codes.size(), nullptr); gState.assign(g.d.codes.size(), 0);
        if (std::getenv("VS_VAOTALL")) { void vaotBuildAll(Game&); vaotBuildAll(g); }
    }
    if (gState[codeId] == 0) {
        Analysis A = analyze(g.d.codes[codeId]);
        if (!A.ok) { gState[codeId] = 2; gRejected++; return false; }
        gProgs[codeId] = build(g.d, g.d.codes[codeId], A); gState[codeId] = 1; gBuilt++;
        gOpsBytes += gProgs[codeId]->ops.size() * sizeof(Op);
        armPrepare(g, codeId, *gProgs[codeId]);
        if (gBuilt % 500 == 0) logf("[vaot] %u codigos traducidos (%u KB)\n", (unsigned)gBuilt, (unsigned)(gOpsBytes / 1024));
    }
    if (gState[codeId] != 1) return false;
    out = run(*gProgs[codeId], g, ctx);
    return true;
}
void vaotStats(size_t& built, size_t& rejected) { built = vaot::gBuilt; rejected = vaot::gRejected; }
// Pruebas (VS_VAOTALL): traduce todos los codigos de golpe y anota cuanto tarda y cuanta memoria ocupa
static double nowMsVaot() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void vaotBuildAll(Game& g) {
    using namespace vaot;
    if (gProgs.size() != g.d.codes.size()) { gProgs.assign(g.d.codes.size(), nullptr); gState.assign(g.d.codes.size(), 0); }
    double t0 = nowMsVaot(); size_t ops = 0, ins = 0;
    for (size_t id = 0; id < g.d.codes.size(); id++) {
        if (gState[id] == 0) {
            Analysis A = analyze(g.d.codes[id]);
            if (!A.ok) { gState[id] = 2; gRejected++; continue; }
            gProgs[id] = build(g.d, g.d.codes[id], A); gState[id] = 1; gBuilt++;
        }
        if (gState[id] == 1) { ops += gProgs[id]->ops.size(); ins += g.d.codes[id].ins.size(); }
    }
    logf("[vaot] %u codigos traducidos, %u al interprete; %u instrucciones -> %u operaciones (%u KB), %.0f ms\n",
         (unsigned)gBuilt, (unsigned)gRejected, (unsigned)ins, (unsigned)ops, (unsigned)(ops * sizeof(Op) / 1024), nowMsVaot() - t0);
}
}  // namespace vs
