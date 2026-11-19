// Compresor/descompresor propio en formato de bloque LZ4 (compatible con LZ4_decompress_safe).
// Se usa para las paginas de textura VDX4: la tarjeta de la Vita lee ~20 MB/s y leer menos bytes es lo que acorta la
// carga; descomprimir LZ4 es casi un memcpy. El SDK de la Vita no trae liblz4, por eso va aqui (sin dependencias).
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace lz4lite {

// Compresion voraz con tabla hash de 4 bytes y busqueda perezosa de un paso. Devuelve el tamano escrito en 'out'.
inline size_t compress(const uint8_t* src, size_t n, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(n + n / 255 + 16);
    auto emit = [&](const uint8_t* lit, size_t litLen, size_t off, size_t matchLen /*0 = solo literales*/) {
        size_t ml = matchLen ? matchLen - 4 : 0;
        uint8_t tok = (uint8_t)((litLen >= 15 ? 15 : litLen) << 4) | (uint8_t)(matchLen ? (ml >= 15 ? 15 : ml) : 0);
        out.push_back(tok);
        if (litLen >= 15) { size_t r = litLen - 15; while (r >= 255) { out.push_back(255); r -= 255; } out.push_back((uint8_t)r); }
        out.insert(out.end(), lit, lit + litLen);
        if (!matchLen) return;
        out.push_back((uint8_t)(off & 255)); out.push_back((uint8_t)(off >> 8));
        if (ml >= 15) { size_t r = ml - 15; while (r >= 255) { out.push_back(255); r -= 255; } out.push_back((uint8_t)r); }
    };
    if (n < 13) { emit(src, n, 0, 0); return out.size(); }
    const int HB = 16;
    std::vector<int32_t> table((size_t)1 << HB, -1);
    auto rd32 = [&](size_t p) { uint32_t v; std::memcpy(&v, src + p, 4); return v; };
    auto hash = [&](uint32_t v) { return (v * 2654435761u) >> (32 - HB); };
    const size_t mflimit = n - 12;      // el ultimo match debe empezar al menos 12 bytes antes del final
    const size_t matchEnd = n - 5;      // los ultimos 5 bytes son siempre literales
    size_t anchor = 0, ip = 0;
    auto matchAt = [&](size_t p, size_t& off) -> size_t {
        uint32_t v = rd32(p); uint32_t h = hash(v);
        int32_t c = table[h]; table[h] = (int32_t)p;
        if (c < 0 || p - (size_t)c > 65535 || rd32((size_t)c) != v) return 0;
        size_t len = 4;
        while (p + len < matchEnd && src[c + len] == src[p + len]) len++;
        off = p - (size_t)c;
        return len;
    };
    while (ip < mflimit) {
        size_t off, len = matchAt(ip, off);
        if (!len) { ip++; continue; }
        // perezosa: si en ip+1 hay un match mas largo, emitir un literal mas
        if (ip + 1 < mflimit) {
            size_t off2, len2 = matchAt(ip + 1, off2);
            if (len2 > len + 1) { ip++; off = off2; len = len2; }
        }
        // extender hacia atras sobre los literales pendientes
        while (ip > anchor && ip - off > 0 && src[ip - 1] == src[ip - off - 1] && off < ip) { ip--; len++; }
        emit(src + anchor, ip - anchor, off, len);
        ip += len; anchor = ip;
        if (ip >= 2 && ip - 2 < mflimit) table[hash(rd32(ip - 2))] = (int32_t)(ip - 2);
    }
    emit(src + anchor, n - anchor, 0, 0);
    return out.size();
}

// Descompresion segura (comprueba limites). true si produce exactamente dstLen bytes.
inline bool decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) {
    const uint8_t* ip = src; const uint8_t* iend = src + srcLen;
    uint8_t* op = dst; uint8_t* oend = dst + dstLen;
    while (ip < iend) {
        unsigned tok = *ip++;
        size_t lit = tok >> 4;
        if (lit == 15) { unsigned b; do { if (ip >= iend) return false; b = *ip++; lit += b; } while (b == 255); }
        if ((size_t)(iend - ip) < lit || (size_t)(oend - op) < lit) return false;
        std::memcpy(op, ip, lit); op += lit; ip += lit;
        if (ip >= iend) break;   // fin del bloque: ultimo grupo de literales
        if (iend - ip < 2) return false;
        size_t off = ip[0] | (ip[1] << 8); ip += 2;
        if (off == 0 || off > (size_t)(op - dst)) return false;
        size_t ml = (tok & 15);
        if (ml == 15) { unsigned b; do { if (ip >= iend) return false; b = *ip++; ml += b; } while (b == 255); }
        ml += 4;
        if ((size_t)(oend - op) < ml) return false;
        const uint8_t* m = op - off;
        if (off >= 8) { while (ml >= 8) { std::memcpy(op, m, 8); op += 8; m += 8; ml -= 8; } }
        while (ml--) *op++ = *m++;
    }
    return op == oend;
}

// Version rapida: copias de 8 bytes que pueden escribir hasta kSlack bytes DESPUES de dstLen (el bufer debe tener
// capacidad dstLen + kSlack). En la Cortex-A9 la version byte a byte costaba ~40 ms por pagina de 4 MB.
constexpr size_t kSlack = 32;
inline bool decompressFast(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen) {
    const uint8_t* ip = src; const uint8_t* iend = src + srcLen;
    uint8_t* op = dst; uint8_t* oend = dst + dstLen;
    auto copy8 = [](uint8_t* d, const uint8_t* s) { uint64_t v; std::memcpy(&v, s, 8); std::memcpy(d, &v, 8); };
    while (ip < iend) {
        unsigned tok = *ip++;
        size_t lit = tok >> 4;
        if (lit == 15) { unsigned b; do { if (ip >= iend) return false; b = *ip++; lit += b; } while (b == 255); }
        if ((size_t)(iend - ip) < lit || (size_t)(oend - op) < lit) return false;
        if (lit <= 16 && iend - ip >= 16) { copy8(op, ip); copy8(op + 8, ip + 8); }
        else std::memcpy(op, ip, lit);
        op += lit; ip += lit;
        if (ip >= iend) break;
        if (iend - ip < 2) return false;
        size_t off = ip[0] | (ip[1] << 8); ip += 2;
        if (off == 0 || off > (size_t)(op - dst)) return false;
        size_t ml = (tok & 15);
        if (ml == 15) { unsigned b; do { if (ip >= iend) return false; b = *ip++; ml += b; } while (b == 255); }
        ml += 4;
        if ((size_t)(oend - op) < ml) return false;
        const uint8_t* m = op - off;
        uint8_t* e = op + ml;
        if (off >= 8) { do { copy8(op, m); op += 8; m += 8; } while (op < e); }
        else { while (op < e) *op++ = *m++; }   // solapado corto (raro en datos DXT)
        op = e;
    }
    return op == oend;
}

} // namespace lz4lite
