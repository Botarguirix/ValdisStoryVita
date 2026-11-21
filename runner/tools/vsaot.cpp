// vsaot: traduce el bytecode GML de game.vsd a C++ (compilacion anticipada, AOT) para el runner.
//   vsaot <game.vsd> <carpeta_salida> [instrucciones_por_archivo]
// Genera aot_NNN.cpp con una funcion por codigo y aot_table.cpp con la tabla y la huella del codigo.
// Los codigos con construcciones no soportadas se dejan al interprete (entrada nula en la tabla).
// El resultado contiene codigo derivado del juego: es para uso personal, no se distribuye.
#include "vsd.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace vs;

enum : uint8_t {
    OP_CONV = 7, OP_MUL = 8, OP_DIV = 9, OP_REM = 10, OP_MOD = 11, OP_ADD = 12, OP_SUB = 13, OP_AND = 14, OP_OR = 15,
    OP_XOR = 16, OP_NEG = 17, OP_NOT = 18, OP_SHL = 19, OP_SHR = 20, OP_CMP = 21, OP_POP = 69, OP_PUSHI = 132,
    OP_DUP = 134, OP_RET = 156, OP_EXIT = 157, OP_POPZ = 158, OP_B = 182, OP_BT = 183, OP_BF = 184,
    OP_PUSHENV = 186, OP_POPENV = 187, OP_PUSH = 192, OP_PUSHLOC = 193, OP_PUSHGLB = 194, OP_PUSHBLTN = 195,
    OP_CALL = 217, OP_BREAK = 255 };
enum : uint8_t { DT_DOUBLE = 0, DT_FLOAT = 1, DT_INT32 = 2, DT_INT64 = 3, DT_BOOL = 4, DT_VAR = 5, DT_STRING = 6, DT_INT16 = 15 };
enum { IT_SELF = -1, IT_LOCAL = -7 };
enum { RT_ARRAY = 0, RT_STACKTOP = 128, RT_NORMAL = 160 };
static const uint8_t kSize[16] = {8, 4, 4, 8, 4, 16, 16, 4, 4, 4, 4, 4, 4, 4, 4, 4};

static bool isPush(uint8_t op) { return op == OP_PUSH || op == OP_PUSHLOC || op == OP_PUSHGLB || op == OP_PUSHBLTN || op == OP_PUSHI; }
static bool isJump(uint8_t op) { return op == OP_B || op == OP_BT || op == OP_BF || op == OP_PUSHENV || op == OP_POPENV; }

// huella del codigo (debe coincidir con la que calcula el runner al cargar game.vsd)
uint64_t codeHash(const GameData& g) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } };
    uint64_t n = g.codes.size(); mix(&n, 8);
    for (auto& c : g.codes) { uint64_t k = c.ins.size(); mix(&k, 8); if (k) mix(c.ins.data(), k * sizeof(Ins)); }
    n = g.doubles.size(); mix(&n, 8); if (n) mix(g.doubles.data(), n * 8);
    n = g.varNames.size(); mix(&n, 8);
    n = g.functions.size(); mix(&n, 8);
    n = g.strings.size(); mix(&n, 8);
    return h;
}

struct Analysis {
    bool ok = true; std::string why;
    std::vector<int> depth;                 // profundidad de pila antes de cada instruccion (-1 = inalcanzable)
    std::vector<std::vector<uint8_t>> sizes; // tamanos de las ranuras antes de cada instruccion
    std::set<int> labels;                   // destinos de salto
    int maxDepth = 0; bool usesWith = false;
    std::set<int> locals;                   // variables que pueden ser locales
};

static Analysis analyze(const Code& c) {
    Analysis A; const int n = (int)c.ins.size();
    A.depth.assign(n + 1, -1); A.sizes.assign(n + 1, {});
    std::vector<int> work;
    auto flow = [&](int to, const std::vector<uint8_t>& st) {
        if (to < 0 || to > n) { A.ok = false; A.why = "salto fuera"; return; }
        if (A.depth[to] < 0) { A.depth[to] = (int)st.size(); A.sizes[to] = st; work.push_back(to); }
        else if (A.sizes[to] != st) { A.ok = false; A.why = "pila inconsistente en " + std::to_string(to); }
    };
    flow(0, {});
    while (!work.empty() && A.ok) {
        int pc = work.back(); work.pop_back();
        if (pc == n) continue;
        std::vector<uint8_t> st = A.sizes[pc];
        const Ins& x = c.ins[pc];
        auto pop = [&](int k) { if ((int)st.size() < k) { A.ok = false; A.why = "pila vacia en " + std::to_string(pc); return false; } st.resize(st.size() - k); return true; };
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
            default: A.ok = false; A.why = "opcode " + std::to_string(x.op); break;
        }
    }
    return A;
}

static std::string S(int k) { return "s" + std::to_string(k); }
static std::string L(int v) { return "L" + std::to_string(v); }

static const char* binName(uint8_t op) {
    switch (op) {
        case OP_ADD: return "OP_ADD"; case OP_SUB: return "OP_SUB"; case OP_MUL: return "OP_MUL"; case OP_DIV: return "OP_DIV";
        case OP_REM: return "OP_REM"; case OP_MOD: return "OP_MOD"; case OP_AND: return "OP_AND"; case OP_OR: return "OP_OR";
        case OP_XOR: return "OP_XOR"; case OP_SHL: return "OP_SHL"; case OP_SHR: return "OP_SHR";
    }
    return "?";
}

static void emitCode(FILE* f, const GameData& g, int id, const Analysis& A) {
    const Code& c = g.codes[id]; const int n = (int)c.ins.size();
    std::fprintf(f, "// %s\nValue c%d(Game& g, Ctx& ctx, Value*, int) {\n", c.name.c_str(), id);
    if (A.maxDepth) {
        std::fprintf(f, "    Value");
        for (int k = 0; k < A.maxDepth; k++) std::fprintf(f, "%s s%d", k ? "," : "", k);
        std::fprintf(f, ";\n");
    }
    if (!A.locals.empty()) {
        std::fprintf(f, "    Value");
        bool first = true;
        for (int v : A.locals) { std::fprintf(f, "%s L%d", first ? "" : ",", v); first = false; }
        std::fprintf(f, ";\n");
    }
    if (A.usesWith) std::fprintf(f, "    std::vector<With> withs;\n");
    std::fprintf(f, "    int row = 0, col = 0, inst = 0; (void)row; (void)col; (void)inst;\n");
    auto lab = [&](int pc) { return "I" + std::to_string(pc); };
    std::set<int> need = A.labels;
    // destinos de comparacion+salto fusionados
    for (int pc = 0; pc < n; pc++) if (A.depth[pc] >= 0 && c.ins[pc].op == OP_CMP && pc + 1 < n && (c.ins[pc + 1].op == OP_BT || c.ins[pc + 1].op == OP_BF)) need.insert(pc + 2);
    // constante numerica empujada justo antes de una comparacion u operacion: se usa como literal (sin pasar por la pila)
    auto constLit = [&](const Ins& p, std::string& lit) -> bool {
        if (!isPush(p.op)) return false;
        char b[64];
        switch (p.t1) {
            case DT_DOUBLE: case DT_INT64: { uint64_t bits; double dv = g.doubles[p.a]; std::memcpy(&bits, &dv, 8); std::snprintf(b, sizeof b, "dbits(0x%016llxull)", (unsigned long long)bits); break; }
            case DT_INT32: case DT_INT16: case DT_BOOL: std::snprintf(b, sizeof b, "%d.0", p.a); break;
            default: return false;
        }
        lit = b; return true;
    };
    auto isBinOp = [](uint8_t op) { return op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV || op == OP_REM || op == OP_MOD || op == OP_AND || op == OP_OR || op == OP_XOR || op == OP_SHL || op == OP_SHR; };
    // Atajo de colisiones contra un tipo de objeto fijo (p. ej. nme_crowding_script: ~50 collision_rectangle por enemigo
    // y frame, casi todos contra tipos que no estan en la sala). Si los argumentos solo son constantes o lecturas simples
    // de variables (sin efectos), antes del primero se mira si el objeto tiene instancias: si no tiene ninguna el
    // resultado es el de "no hay nada" (noone / 0) sin evaluar los argumentos ni llamar a la funcion. Mismo resultado.
    struct Guard { int callPc, obj; double none; };
    std::map<int, Guard> guards;   // pc del primer argumento -> atajo
    for (int pc = 0; pc < n; pc++) {
        const Ins& x = c.ins[pc];
        if (x.op != OP_CALL || A.depth[pc] < 0 || x.a < 0 || x.a >= (int)g.functions.size()) continue;
        const std::string& fn = g.functions[x.a]; int na = x.b, objArg = -1; double none = -4;
        if (fn == "collision_rectangle" || fn == "collision_line") { if (na == 7) objArg = 4; }
        else if (fn == "collision_point") { if (na == 5) objArg = 2; }
        else if (fn == "collision_circle") { if (na == 6) objArg = 3; }
        else if (fn == "instance_place") { if (na == 3) objArg = 2; }
        else if (fn == "place_meeting" || fn == "position_meeting") { if (na == 3) { objArg = 2; none = 0; } }
        if (objArg < 0) continue;
        // hacia atras: na empujes (cada uno puede ir seguido de CONV), todos simples y sin etiquetas entre medias
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
        const Ins& o = c.ins[pushes[objArg]];   // pushes[0] = ultimo empujado = primer argumento
        if (!(o.t1 == DT_INT32 || o.t1 == DT_INT16) || o.a < 0 || o.a >= (int)g.objects.size()) continue;
        if (A.depth[first] < 0 || A.depth[pc + 1 < n ? pc + 1 : pc] < 0) continue;
        guards[first] = Guard{pc, o.a, none};
        need.insert(pc + 1);
    }
    for (int pc = 0; pc < n; pc++) {
        if (A.depth[pc] < 0) continue;
        if (need.count(pc)) std::fprintf(f, "%s:;\n", lab(pc).c_str());
        const Ins& x = c.ins[pc]; const int d = A.depth[pc];
        if (auto gi = guards.find(pc); gi != guards.end())
            std::fprintf(f, "    if (g.instancesOf(%d).empty()) { s%d = Value(%s); goto %s; }\n", gi->second.obj, d,
                         gi->second.none < 0 ? "-4.0" : "0.0", lab(gi->second.callPc + 1).c_str());
        std::fprintf(f, "    ");
        {
            std::string lit;
            if (pc + 1 < n && !need.count(pc + 1) && A.depth[pc + 1] >= 0 && constLit(x, lit)) {
                const Ins& y = c.ins[pc + 1];
                if (y.op == OP_CMP) {
                    if (pc + 2 < n && (c.ins[pc + 2].op == OP_BT || c.ins[pc + 2].op == OP_BF)) {
                        const Ins& nx = c.ins[pc + 2];
                        std::fprintf(f, "if (%scmpK(g, %d, s%d, %s)) goto %s; goto %s;\n", nx.op == OP_BF ? "!" : "", y.cmp, d - 1, lit.c_str(), lab(nx.a).c_str(), lab(pc + 3).c_str());
                        need.insert(nx.a);
                    } else std::fprintf(f, "{ bool r = cmpK(g, %d, s%d, %s); s%d = Value(r ? 1.0 : 0.0); }\n", y.cmp, d - 1, lit.c_str(), d - 1);
                    pc++;            // la comparacion ya esta emitida (la constante no se apila)
                    continue;
                }
                if (isBinOp(y.op)) {
                    std::fprintf(f, "binK<%s>(g, s%d, %s, %d, %d);\n", binName(y.op), d - 1, lit.c_str(), y.t1, y.t2);
                    pc++;
                    continue;
                }
            }
        }
        if (isPush(x.op)) {
            switch (x.t1) {
                case DT_DOUBLE: case DT_INT64: {
                    uint64_t b; double dv = g.doubles[x.a]; std::memcpy(&b, &dv, 8);
                    std::fprintf(f, "s%d = Value(dbits(0x%016llxull));\n", d, (unsigned long long)b); break;
                }
                case DT_INT32: case DT_INT16: case DT_BOOL: std::fprintf(f, "s%d = Value((double)%d);\n", d, x.a); break;
                case DT_STRING: std::fprintf(f, "pushStr(g, s%d, %d);\n", d, x.a); break;
                case DT_VAR:
                    if (x.b == RT_ARRAY) {
                        // indice en s[d-1], instancia en s[d-2]; resultado en s[d-2]
                        std::fprintf(f, "split(s%d, row, col); inst = (int)s%d.num(); drop(s%d); ", d - 1, d - 2, d - 1);
                        std::fprintf(f, "if (inst == IT_LOCAL) s%d = %s.get(row, col); else g.vmRead(ctx, inst, %d, true, row, col, s%d);\n", d - 2, L(x.a).c_str(), x.a, d - 2);
                    } else if (x.b == RT_STACKTOP) {
                        std::fprintf(f, "inst = (int)s%d.num(); ", d - 1);
                        std::fprintf(f, "if (inst == IT_LOCAL) s%d = %s; else g.vmRead(ctx, inst, %d, false, 0, 0, s%d);\n", d - 1, L(x.a).c_str(), x.a, d - 1);
                    } else if (x.inst == IT_LOCAL) std::fprintf(f, "s%d = %s;\n", d, L(x.a).c_str());
                    else if (x.inst == IT_SELF) std::fprintf(f, "readSelf(g, ctx, %d, s%d);\n", x.a, d);
                    else if (x.inst == -5) std::fprintf(f, "readGlobal(g, ctx, %d, s%d);\n", x.a, d);
                    else std::fprintf(f, "g.vmRead(ctx, %d, %d, false, 0, 0, s%d);\n", x.inst, x.a, d);
                    break;
                default: std::fprintf(f, "s%d = Value((double)%d);\n", d, x.a); break;
            }
            continue;
        }
        switch (x.op) {
            case OP_POP: {
                // orden del interprete: con t1==INT32 el valor esta arriba; si no, debajo de indice/instancia
                int vpos, ipos = -1, xpos = -1;
                if (x.b == RT_ARRAY) { if (x.t1 == DT_INT32) { vpos = d - 1; xpos = d - 2; ipos = d - 3; } else { xpos = d - 1; ipos = d - 2; vpos = d - 3; } }
                else if (x.b == RT_STACKTOP) { if (x.t1 == DT_INT32) { vpos = d - 1; ipos = d - 2; } else { ipos = d - 1; vpos = d - 2; } }
                else vpos = d - 1;
                if (x.b == RT_ARRAY) {
                    std::fprintf(f, "split(s%d, row, col); inst = (int)s%d.num(); ", xpos, ipos);
                    std::fprintf(f, "if (inst == IT_LOCAL) %s.set(row, col, s%d); else g.vmWrite(ctx, inst, %d, true, row, col, s%d); drop(s%d); drop(s%d); drop(s%d);\n",
                                 L(x.a).c_str(), vpos, x.a, vpos, vpos, xpos, ipos);
                } else if (x.b == RT_STACKTOP) {
                    std::fprintf(f, "inst = (int)s%d.num(); ", ipos);
                    std::fprintf(f, "if (inst == IT_LOCAL) %s = std::move(s%d); else { g.vmWrite(ctx, inst, %d, false, 0, 0, s%d); drop(s%d); } drop(s%d);\n",
                                 L(x.a).c_str(), vpos, x.a, vpos, vpos, ipos);
                } else if (x.inst == IT_LOCAL) std::fprintf(f, "%s = std::move(s%d);\n", L(x.a).c_str(), vpos);
                else if (x.inst == IT_SELF) std::fprintf(f, "writeSelf(g, ctx, %d, s%d);\n", x.a, vpos);
                else if (x.inst == -5) std::fprintf(f, "writeGlobal(g, ctx, %d, s%d);\n", x.a, vpos);
                else std::fprintf(f, "g.vmWrite(ctx, %d, %d, false, 0, 0, s%d); drop(s%d);\n", x.inst, x.a, vpos, vpos);
                break;
            }
            case OP_POPZ: std::fprintf(f, "drop(s%d);\n", d - 1); break;
            case OP_DUP: {
                const auto& st = A.sizes[pc];
                int bytes = (x.extra + 1) * kSize[x.t1 & 15], acc = 0; size_t k = 0;
                while (acc < bytes && k < st.size()) { acc += st[st.size() - 1 - k]; k++; }
                int start = d - (int)k;
                for (size_t i = 0; i < k; i++) std::fprintf(f, "s%d = s%d; ", d + (int)i, start + (int)i);
                std::fprintf(f, "\n"); break;
            }
            case OP_CONV: case OP_BREAK: std::fprintf(f, ";\n"); break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
            case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR:
                std::fprintf(f, "bin<%s>(g, s%d, s%d, %d, %d);\n", binName(x.op), d - 2, d - 1, x.t1, x.t2); break;
            case OP_NEG: std::fprintf(f, "neg(s%d);\n", d - 1); break;
            case OP_NOT: std::fprintf(f, "notOp(s%d, %s);\n", d - 1, x.t1 == DT_BOOL ? "true" : "false"); break;
            case OP_CMP:
                if (pc + 1 < n && (c.ins[pc + 1].op == OP_BT || c.ins[pc + 1].op == OP_BF)) {
                    const Ins& nx = c.ins[pc + 1];
                    std::fprintf(f, "if (%scmp(g, %d, s%d, s%d)) goto %s; goto %s;\n", nx.op == OP_BF ? "!" : "", x.cmp, d - 2, d - 1,
                                 lab(nx.a).c_str(), lab(pc + 2).c_str());
                    need.insert(nx.a);
                } else std::fprintf(f, "{ bool r = cmp(g, %d, s%d, s%d); s%d = Value(r ? 1.0 : 0.0); }\n", x.cmp, d - 2, d - 1, d - 2);
                break;
            case OP_B: std::fprintf(f, "goto %s;\n", lab(x.a).c_str()); break;
            case OP_BT: std::fprintf(f, "{ bool t = s%d.truthy(); drop(s%d); if (t) goto %s; }\n", d - 1, d - 1, lab(x.a).c_str()); break;
            case OP_BF: std::fprintf(f, "{ bool t = s%d.truthy(); drop(s%d); if (!t) goto %s; }\n", d - 1, d - 1, lab(x.a).c_str()); break;
            case OP_PUSHENV:
                std::fprintf(f, "{ With w; w.saved = ctx; w.idx = 0; double t = s%d.num(); drop(s%d); g.collectTargets(t, ctx, w.list); "
                                "if (w.list.empty()) goto %s; withs.push_back(std::move(w)); ctx.other = withs.back().saved.self; ctx.self = withs.back().list[0]; }\n",
                             d - 1, d - 1, lab(x.a).c_str());
                break;
            case OP_POPENV:
                if (x.flags & 1) std::fprintf(f, "if (!withs.empty()) { ctx = withs.back().saved; withs.pop_back(); }\n");
                else std::fprintf(f, "if (!withs.empty()) { With& w = withs.back(); w.idx++; while (w.idx < w.list.size() && !w.list[w.idx]->alive) w.idx++; "
                                     "if (w.idx < w.list.size()) { ctx.self = w.list[w.idx]; goto %s; } ctx = w.saved; withs.pop_back(); }\n", lab(x.a).c_str());
                break;
            case OP_CALL: {
                int na = x.b;
                if (na == 0) std::fprintf(f, "s%d = g.vmCallFn(%d, ctx, nullptr, 0);\n", d, x.a);
                else {
                    std::fprintf(f, "{ Value av[%d]; ", na);
                    for (int i = 0; i < na; i++) std::fprintf(f, "av[%d] = std::move(s%d); ", i, d - 1 - i);
                    if (x.a >= 0 && x.a < (int)g.functions.size() && g.functions[x.a] == "script_execute")   // directo, sin la tabla de integradas
                        std::fprintf(f, "s%d = g.runScript(av[0].ival(), ctx, av + 1, %d); }\n", d - na, na - 1);
                    else std::fprintf(f, "s%d = g.vmCallFn(%d, ctx, av, %d); }\n", d - na, x.a, na);
                }
                break;
            }
            case OP_RET: std::fprintf(f, "return std::move(s%d);\n", d - 1); break;
            case OP_EXIT: std::fprintf(f, "return Value();\n"); break;
        }
    }
    std::fprintf(f, "I%d:;\n    return Value();\n}\n\n", n);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("uso: vsaot <game.vsd> <salida> [instrucciones_por_archivo]\n"); return 1; }
    GameData g; std::string err;
    if (!g.load(argv[1], err)) { std::printf("error: %s\n", err.c_str()); return 1; }
    std::string out = argv[2];
    size_t perFile = argc > 3 ? (size_t)std::atoi(argv[3]) : 40000;
    std::vector<bool> compiled(g.codes.size(), false);
    std::map<std::string, int> whyCount;
    int fileNo = 0; size_t inFile = 0; FILE* f = nullptr;
    size_t nOk = 0, insOk = 0, insAll = 0;
    auto openNext = [&]() {
        if (f) { std::fprintf(f, "} // namespace aot\n} // namespace vs\n"); std::fclose(f); }
        char name[64]; std::snprintf(name, sizeof name, "/aot_%03d.cpp", fileNo++);
        f = std::fopen((out + name).c_str(), "w");
        std::fprintf(f, "// Generado por vsaot a partir de game.vsd. Codigo derivado del juego: solo para uso personal.\n"
                        "#include \"aot_rt.h\"\n#pragma GCC diagnostic ignored \"-Wunused-label\"\n#pragma GCC diagnostic ignored \"-Wunused-variable\"\n"
                        "namespace vs {\nnamespace aot {\n\n");
        inFile = 0;
    };
    openNext();
    for (size_t id = 0; id < g.codes.size(); id++) {
        insAll += g.codes[id].ins.size();
        Analysis A = analyze(g.codes[id]);
        if (!A.ok) { whyCount[A.why.substr(0, A.why.find(' '))]++; continue; }
        if (inFile > perFile) openNext();
        emitCode(f, g, (int)id, A);
        compiled[id] = true; nOk++; insOk += g.codes[id].ins.size(); inFile += g.codes[id].ins.size();
    }
    std::fprintf(f, "} // namespace aot\n} // namespace vs\n"); std::fclose(f);

    FILE* t = std::fopen((out + "/aot_table.cpp").c_str(), "w");
    std::fprintf(t, "// Generado por vsaot\n#include \"aot_rt.h\"\nnamespace vs {\nnamespace aot {\n");
    for (size_t id = 0; id < g.codes.size(); id++) if (compiled[id]) std::fprintf(t, "Value c%zu(Game&, Ctx&, Value*, int);\n", id);
    std::fprintf(t, "} // namespace aot\nextern const uint64_t kAotHash = 0x%016llxull;\nextern const unsigned kAotCount = %zu;\n", (unsigned long long)codeHash(g), g.codes.size());
    std::fprintf(t, "void aotFill(Game::AotFn* tab) {\n");
    for (size_t id = 0; id < g.codes.size(); id++) if (compiled[id]) std::fprintf(t, "    tab[%zu] = aot::c%zu;\n", id, id);
    std::fprintf(t, "}\n} // namespace vs\n");
    std::fclose(t);
    std::printf("codigos: %zu de %zu traducidos (%zu de %zu instrucciones), %d archivos\n", nOk, g.codes.size(), insOk, insAll, fileNo);
    for (auto& [w, k] : whyCount) std::printf("  sin traducir por %s: %d\n", w.c_str(), k);
    return 0;
}
