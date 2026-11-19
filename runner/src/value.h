// Valor GML (GMS 1.x): real, string, array 2D (referencia con copy-on-write) o undefined.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <utility>

namespace vs {

struct StrObj { int rc; std::string s; };
struct Value;
struct ArrObj { int rc; std::vector<std::vector<Value>> rows; };

enum class VT : uint8_t { Real = 0, Str = 1, Arr = 2, Undef = 3 };

struct Value {
    VT t = VT::Real;
    union { double r; StrObj* s; ArrObj* a; };

    Value() : r(0) {}
    Value(double d) : r(d) {}
    Value(int i) : r(i) {}
    Value(bool b) : r(b ? 1.0 : 0.0) {}
    static Value str(std::string v) { Value x; x.t = VT::Str; x.s = new StrObj{1, std::move(v)}; return x; }
    static Value undef() { Value x; x.t = VT::Undef; return x; }
    static Value newArr() { Value x; x.t = VT::Arr; x.a = new ArrObj{1, {}}; return x; }

    Value(const Value& o) : t(o.t), r(o.r) { retain(); }
    Value(Value&& o) noexcept : t(o.t), r(o.r) { o.t = VT::Real; o.r = 0; }
    Value& operator=(const Value& o) { if (this != &o) { o.retainConst(); release(); t = o.t; r = o.r; } return *this; }
    Value& operator=(Value&& o) noexcept { if (this != &o) { release(); t = o.t; r = o.r; o.t = VT::Real; o.r = 0; } return *this; }
    ~Value() { release(); }

    bool isReal() const { return t == VT::Real; }
    bool isStr() const { return t == VT::Str; }
    bool isArr() const { return t == VT::Arr; }
    bool isUndef() const { return t == VT::Undef; }

    double num() const {
        if (t == VT::Real) return r;
        if (t == VT::Str) return std::atof(s->s.c_str());
        return 0;
    }
    int64_t i64() const { double d = num(); return (int64_t)std::llround(d); }
    int ival() const { return (int)num(); }
    bool truthy() const { return t == VT::Real ? r > 0.5 : (t == VT::Str ? false : false); }
    const std::string& sref() const { static const std::string empty; return t == VT::Str ? s->s : empty; }
    std::string toString() const;

    // array helpers (GMS1: 2D; indice plano i*32000+j)
    Value get(int row, int col) const {
        if (t != VT::Arr || row < 0 || col < 0 || (size_t)row >= a->rows.size() || (size_t)col >= a->rows[row].size()) return Value();
        return a->rows[row][col];
    }
    // asegura array propio (copy-on-write) y lo devuelve
    ArrObj* ownArr() {
        if (t != VT::Arr) { release(); t = VT::Arr; a = new ArrObj{1, {}}; }
        else if (a->rc > 1) { ArrObj* n = new ArrObj{1, a->rows}; a->rc--; a = n; }
        return a;
    }
    void set(int row, int col, const Value& v) {
        if (row < 0 || col < 0) return;
        ArrObj* o = ownArr();
        if ((size_t)row >= o->rows.size()) o->rows.resize(row + 1);
        auto& rr = o->rows[row];
        if ((size_t)col >= rr.size()) rr.resize(col + 1);
        rr[col] = v;
    }

private:
    void retain() { if (t == VT::Str) s->rc++; else if (t == VT::Arr) a->rc++; }
    void retainConst() const { if (t == VT::Str) s->rc++; else if (t == VT::Arr) a->rc++; }
    // camino rapido en linea (la gran mayoria son reales); el resto fuera de linea para no inflar cada uso
    void release() { if (t != VT::Real) releaseSlow(); }
    __attribute__((noinline)) void releaseSlow() {
        if (t == VT::Str) { if (--s->rc == 0) delete s; }
        else if (t == VT::Arr) { if (--a->rc == 0) delete a; }
        t = VT::Real;
    }
};

inline std::string fmtReal(double d) {
    // GMS1 string(real): enteros sin decimales; resto con 2 decimales
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
    double ip;
    if (std::modf(d, &ip) == 0.0 && std::fabs(d) < 1e15) { char b[32]; std::snprintf(b, sizeof b, "%.0f", d); return b; }
    char b[64]; std::snprintf(b, sizeof b, "%.2f", d);
    return b;
}

inline std::string Value::toString() const {
    switch (t) {
        case VT::Real: return fmtReal(r);
        case VT::Str: return s->s;
        case VT::Arr: return "array";
        default: return "undefined";
    }
}

} // namespace vs
