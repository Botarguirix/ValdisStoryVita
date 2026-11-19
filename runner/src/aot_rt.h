// Soporte en tiempo de ejecucion del codigo nativo generado por tools/vsaot.cpp (bytecode GML -> C++).
// Cada funcion generada reproduce exactamente al interprete (vm.cpp): la pila de la VM se convierte en variables
// locales s0..sN (profundidad conocida al traducir), los saltos en goto y las variables locales GML en Value locales.
#pragma once
#include "runtime.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace vs {
namespace aot {

enum : int { IT_SELF = -1, IT_OTHER = -2, IT_ALL = -3, IT_NOONE = -4, IT_GLOBAL = -5, IT_BUILTIN = -6, IT_LOCAL = -7 };
enum : uint8_t { OP_MUL = 8, OP_DIV = 9, OP_REM = 10, OP_MOD = 11, OP_ADD = 12, OP_SUB = 13, OP_AND = 14, OP_OR = 15,
                 OP_XOR = 16, OP_SHL = 19, OP_SHR = 20 };
enum : uint8_t { DT_BOOL = 4 };

struct With { std::vector<Instance*> list; size_t idx = 0; Ctx saved; };

inline double dbits(uint64_t b) { double d; std::memcpy(&d, &b, 8); return d; }
inline bool isReal(const Value& v) { return v.t == VT::Real; }
// valor consumido: soltar referencias (cadena/array) para no alargar su vida ni provocar copias en arrays
inline void drop(Value& v) { if (v.t != VT::Real) v = Value(); }

inline void pushStr(Game& g, Value& out, int idx) {
    if (g.strConst.empty()) { g.strConst.resize(g.d.strings.size()); for (size_t k = 0; k < g.d.strings.size(); k++) g.strConst[k] = new StrObj{1, g.d.strings[k]}; }
    Value sv; sv.t = VT::Str; sv.s = g.strConst[idx]; sv.s->rc++;
    out = std::move(sv);
}

inline void split(const Value& iv, int& row, int& col) {
    int idx = iv.ival();
    if (idx >= 32000) { row = idx / 32000; col = idx - row * 32000; } else { row = 0; col = idx; }
}

// lectura de variable propia sin indice (el caso mas frecuente)
// Variables integradas (x, y, object_index, sprite_index...): directo a getBuiltin/setBuiltin, sin pasar por
// vmRead/vmWrite (los scripts de enemigos leen object_index decenas de veces por llamada). Mismo resultado: vmRead
// con self tambien empieza por getBuiltin, y si no lo resuelve se sigue por el camino general.
inline void readSelf(Game& g, Ctx& ctx, int var, Value& out) {
    if (ctx.self) {
        int bv = g.varBuiltin[var];
        if (!bv) { if (const Value* v = ctx.self->vars.get(var)) { out = *v; return; } }
        else if (g.getBuiltin(bv, ctx.self, -1, out)) return;
    }
    g.vmRead(ctx, IT_SELF, var, false, 0, 0, out);
}
inline void writeSelf(Game& g, Ctx& ctx, int var, Value& v) {
    if (ctx.self) {
        int bv = g.varBuiltin[var];
        if (!bv) { ctx.self->vars[var] = std::move(v); v = Value(); return; }
        if (g.setBuiltin(bv, ctx.self, -1, v)) { drop(v); return; }
    }
    g.vmWrite(ctx, IT_SELF, var, false, 0, 0, v); drop(v);
}

inline void readGlobal(Game& g, Ctx& ctx, int var, Value& out) {
    if (!g.varBuiltin[var]) { if (const Value* v = g.globals.get(var)) { out = *v; return; } }
    g.vmRead(ctx, IT_GLOBAL, var, false, 0, 0, out);
}
inline void writeGlobal(Game& g, Ctx& ctx, int var, Value& v) {
    if (!g.varBuiltin[var]) { g.globals[var] = std::move(v); v = Value(); return; }
    g.vmWrite(ctx, IT_GLOBAL, var, false, 0, 0, v); drop(v);
}

// a = a OP b (resultado en a, b se suelta)
template <uint8_t OP> inline void bin(Game& g, Value& a, Value& b, uint8_t t1, uint8_t t2) {
    if (a.t == VT::Real && b.t == VT::Real) {
        double A = a.r, B = b.r;
        switch (OP) {
            case OP_ADD: a.r = A + B; return;
            case OP_SUB: a.r = A - B; return;
            case OP_MUL: a.r = A * B; return;
            case OP_DIV: if (B != 0) { a.r = A / B; return; } break;
            case OP_REM: if (B != 0) { a.r = std::trunc(A / B); return; } break;
            case OP_MOD: if (B != 0) { a.r = std::fmod(A, B); return; } break;
            case OP_AND: case OP_OR: case OP_XOR:
                if (t1 == DT_BOOL && t2 == DT_BOOL) {
                    bool ba = A > 0.5, bb = B > 0.5;
                    a.r = OP == OP_AND ? (ba && bb) : OP == OP_OR ? (ba || bb) : (ba != bb); return;
                }
                a.r = OP == OP_AND ? (double)((int64_t)A & (int64_t)B) : OP == OP_OR ? (double)((int64_t)A | (int64_t)B) : (double)((int64_t)A ^ (int64_t)B);
                return;
            case OP_SHL: a.r = (double)((int64_t)A << (int64_t)B); return;
            case OP_SHR: a.r = (double)((int64_t)A >> (int64_t)B); return;
        }
    }
    Value r; g.vmBinary(OP, t1, t2, a, b, r); a = std::move(r); drop(b);
}

// comparacion; con dos reales en linea (epsilon 1e-5 como GMS1)
inline bool cmp(Game& g, uint8_t k, Value& a, Value& b) {
    bool r;
    if (a.t == VT::Real && b.t == VT::Real) {
        const double eps = 0.00001, diff = a.r - b.r;
        switch (k) {
            case 1: r = diff < -eps; break; case 2: r = diff <= eps; break; case 3: r = std::fabs(diff) <= eps; break;
            case 4: r = std::fabs(diff) > eps; break; case 5: r = diff >= -eps; break; default: r = diff > eps; break;
        }
        return r;
    }
    r = g.vmCompare(k, a, b);
    drop(a); drop(b);
    return r;
}

// variantes con el operando derecho constante (literal numerico; el compilador lo pliega)
inline bool cmpK(Game& g, uint8_t k, Value& a, double B) {
    if (a.t == VT::Real) {
        const double eps = 0.00001, diff = a.r - B;
        switch (k) {
            case 1: return diff < -eps; case 2: return diff <= eps; case 3: return std::fabs(diff) <= eps;
            case 4: return std::fabs(diff) > eps; case 5: return diff >= -eps; default: return diff > eps;
        }
    }
    Value b(B);
    bool r = g.vmCompare(k, a, b);
    drop(a);
    return r;
}
template <uint8_t OP> inline void binK(Game& g, Value& a, double B, uint8_t t1, uint8_t t2) {
    Value b(B);
    bin<OP>(g, a, b, t1, t2);
}

inline void neg(Value& a) { double v = a.num(); a = Value(-v); }
inline void notOp(Value& a, bool isBool) { if (isBool) { bool t = a.truthy(); a = Value(t ? 0.0 : 1.0); } else { double v = a.num(); a = Value((double)(~(int64_t)v)); } }

} // namespace aot
} // namespace vs
