// Variables de instancia con disposicion por objeto (sustituye a IntMap<Value> en Instance::vars).
//
// Antes cada instancia tenia su propia tabla hash (un enemigo de Valdis lleva ~280 variables: ~12 KB de tabla, y cada
// lectura saltaba a un sitio al azar). En la Vita, con decenas de enemigos, esas tablas no caben en cache y cada
// enemigo activo costaba ~1 ms de logica. Ahora:
//   - cada objeto tiene una tabla compartida "variable -> posicion" (slotOf), que crece al escribir variables nuevas;
//   - cada instancia guarda sus valores seguidos en un vector (280 x 16 B = 4,5 KB), en el orden de su objeto.
// Las 12 instancias de soldier comparten la misma tabla, que se queda en cache.
// Una posicion sin valor todavia (la variable existe en el objeto pero esta instancia no la ha escrito) se marca con
// kUnset: get() devuelve nullptr, igual que la tabla hash cuando la variable no estaba.
#pragma once
#include "value.h"
#include <cstdint>
#include <vector>

namespace vs {

class VarStore {
public:
    struct Layout {
        std::vector<int16_t> slotOf;   // variable -> posicion (-1 = aun no)
        std::vector<int> keyOf;        // posicion -> variable
    };
    static constexpr VT kUnset = (VT)7;

    // disposicion del objeto obj (-1 = instancias sin objeto). Se crean al usarlas y no se liberan nunca.
    static Layout* layoutFor(int obj) {
        static std::vector<Layout*> all;
        size_t k = (size_t)(obj + 1);
        if (k >= all.size()) all.resize(k + 1, nullptr);
        if (!all[k]) all[k] = new Layout();
        return all[k];
    }

    VarStore() : lay_(layoutFor(-1)) {}
    // al crear la instancia: reserva ya las posiciones que el objeto conoce (las instancias de un mismo objeto usan
    // casi las mismas variables), asi no hay que ampliar el vector variable a variable
    void bind(int obj) {
        lay_ = layoutFor(obj);
        vals_.clear(); n_ = 0;
        vals_.resize(lay_->keyOf.size(), unsetValue());
    }

    Value* get(int k) {
        const Layout* L = lay_;
        if ((unsigned)k >= L->slotOf.size()) return nullptr;
        int s = L->slotOf[k];
        if (s < 0 || s >= (int)vals_.size()) return nullptr;
        Value& v = vals_[s];
        return v.t == kUnset ? nullptr : &v;
    }
    const Value* get(int k) const { return const_cast<VarStore*>(this)->get(k); }
    bool count(int k) const { return get(k) != nullptr; }

    Value& operator[](int k) {
        Layout* L = lay_;
        if ((unsigned)k >= L->slotOf.size()) L->slotOf.resize((size_t)k + 1 > 8192 ? (size_t)k + 1 : 8192, -1);
        int s = L->slotOf[k];
        if (s < 0) { s = (int)L->keyOf.size(); L->slotOf[k] = (int16_t)s; L->keyOf.push_back(k); }
        if (s >= (int)vals_.size()) vals_.resize(L->keyOf.size(), unsetValue());
        Value& v = vals_[s];
        if (v.t == kUnset) { v = Value(); n_++; }
        return v;
    }

    int size() const { return n_; }
    bool empty() const { return n_ == 0; }

    // recorre las variables con valor: f(variable, const Value&)
    template <class F> void forEach(F&& f) const {
        for (size_t s = 0; s < vals_.size(); s++) if (vals_[s].t != kUnset) f(lay_->keyOf[s], vals_[s]);
    }

private:
    static Value unsetValue() { Value v; v.t = kUnset; return v; }
    Layout* lay_;
    std::vector<Value> vals_;
    int n_ = 0;
};

} // namespace vs
