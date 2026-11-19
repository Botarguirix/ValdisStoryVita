// Tabla hash int -> V con direccionamiento abierto (potencia de 2, sondeo lineal, borrado por desplazamiento).
// Sustituye a std::unordered_map en los caminos calientes de la VM: el Cortex-A9 de la Vita no tiene division
// por hardware y unordered_map hace un modulo (y salta por nodos en memoria) en cada busqueda.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace vs {

template <class V>
class IntMap {
public:
    struct E { int first = kEmpty; V second{}; };
    static constexpr int kEmpty = INT32_MIN;

    V* get(int k) {
        if (!n_) return nullptr;
        for (uint32_t i = slot(k);; i = (i + 1) & mask_) {
            E& e = t_[i];
            if (e.first == k) return &e.second;
            if (e.first == kEmpty) return nullptr;
        }
    }
    const V* get(int k) const { return const_cast<IntMap*>(this)->get(k); }
    bool count(int k) const { return get(k) != nullptr; }

    V& operator[](int k) {
        if ((n_ + 1) * 4 > (int)t_.size() * 3) grow();
        for (uint32_t i = slot(k);; i = (i + 1) & mask_) {
            E& e = t_[i];
            if (e.first == k) return e.second;
            if (e.first == kEmpty) { e.first = k; e.second = V{}; n_++; return e.second; }
        }
    }

    bool erase(int k) {
        if (!n_) return false;
        uint32_t i = slot(k);
        for (;; i = (i + 1) & mask_) {
            if (t_[i].first == k) break;
            if (t_[i].first == kEmpty) return false;
        }
        // borrado por desplazamiento hacia atras (sin lapidas)
        uint32_t j = i;
        for (;;) {
            j = (j + 1) & mask_;
            if (t_[j].first == kEmpty) break;
            uint32_t h = slot(t_[j].first);
            bool move = (i <= j) ? (h <= i || h > j) : (h <= i && h > j);
            if (move) { t_[i] = std::move(t_[j]); i = j; }
        }
        t_[i].first = kEmpty; t_[i].second = V{};
        n_--;
        return true;
    }

    void clear() { t_.clear(); n_ = 0; mask_ = 0; shift_ = 32; }
    int size() const { return n_; }
    bool empty() const { return n_ == 0; }

    // iteracion sobre las entradas ocupadas: for (auto& e : m) e.first / e.second
    class iterator {
    public:
        iterator(E* p, E* end) : p_(p), end_(end) { skip(); }
        E& operator*() const { return *p_; }
        E* operator->() const { return p_; }
        iterator& operator++() { ++p_; skip(); return *this; }
        bool operator!=(const iterator& o) const { return p_ != o.p_; }
        bool operator==(const iterator& o) const { return p_ == o.p_; }
    private:
        void skip() { while (p_ != end_ && p_->first == kEmpty) ++p_; }
        E* p_; E* end_;
    };
    iterator begin() { return iterator(t_.data(), t_.data() + t_.size()); }
    iterator end() { return iterator(t_.data() + t_.size(), t_.data() + t_.size()); }

private:
    uint32_t slot(int k) const { return ((uint32_t)k * 2654435761u) >> shift_; }
    void grow() {
        std::vector<E> old = std::move(t_);
        int bits = old.empty() ? 3 : 0;
        if (!old.empty()) { size_t c = old.size() * 2; while ((1u << bits) < c) bits++; }
        t_.assign((size_t)1 << bits, E{});
        mask_ = (1u << bits) - 1; shift_ = 32 - bits; n_ = 0;
        for (E& e : old) if (e.first != kEmpty) (*this)[e.first] = std::move(e.second);
    }
    std::vector<E> t_;
    int n_ = 0;
    uint32_t mask_ = 0, shift_ = 32;
};

} // namespace vs
