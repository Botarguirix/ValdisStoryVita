// data.win (GameMaker Studio 1.x, bytecode 13) -> game.vsd + snd/*.wav, sin UndertaleModTool.
// Hace lo mismo que tools/export_vsdata.csx: el resultado debe ser identico byte a byte.
// Se lee por trozos (chunks) con fseek: data.win pesa ~250 MB y la Vita tiene poca RAM.
#include "convert.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>

namespace vs {

namespace {

struct Chunk {
    uint32_t base = 0;            // direccion absoluta del primer byte de datos
    std::vector<uint8_t> d;
    bool has(uint32_t a, uint32_t n = 4) const { return a >= base && (uint64_t)a - base + n <= d.size(); }
    uint32_t u32(uint32_t a) const { uint32_t v = 0; if (has(a)) std::memcpy(&v, &d[a - base], 4); return v; }
    int32_t i32(uint32_t a) const { return (int32_t)u32(a); }
    uint16_t u16(uint32_t a) const { uint16_t v = 0; if (has(a, 2)) std::memcpy(&v, &d[a - base], 2); return v; }
    int16_t i16(uint32_t a) const { return (int16_t)u16(a); }
    uint8_t u8(uint32_t a) const { return has(a, 1) ? d[a - base] : 0; }
    float f32(uint32_t a) const { float v = 0; if (has(a)) std::memcpy(&v, &d[a - base], 4); return v; }
};

struct Out {
    FILE* f = nullptr;
    void raw(const void* p, size_t n) { std::fwrite(p, 1, n, f); }
    void u32(uint32_t v) { raw(&v, 4); }
    void i32(int32_t v) { raw(&v, 4); }
    void u16(uint16_t v) { raw(&v, 2); }
    void i16(int16_t v) { raw(&v, 2); }
    void f32(float v) { raw(&v, 4); }
    void b(bool v) { uint8_t x = v ? 1 : 0; raw(&x, 1); }
    void str(const std::string& s) { u32((uint32_t)s.size()); raw(s.data(), s.size()); }
};

struct Data {
    FILE* f = nullptr;
    std::map<std::string, std::pair<uint32_t, uint32_t>> chunks; // nombre -> (inicio de datos, tamano)
    std::string err;

    bool open(const std::string& path) {
        f = std::fopen(path.c_str(), "rb");
        if (!f) { err = "no se puede abrir " + path; return false; }
        char hd[8];
        if (std::fread(hd, 1, 8, f) != 8 || std::memcmp(hd, "FORM", 4) != 0) { err = "data.win no es un archivo FORM de GameMaker"; return false; }
        uint32_t total; std::memcpy(&total, hd + 4, 4);
        uint64_t o = 8;
        while (o + 8 <= (uint64_t)total + 8) {
            std::fseek(f, (long)o, SEEK_SET);
            if (std::fread(hd, 1, 8, f) != 8) break;
            uint32_t sz; std::memcpy(&sz, hd + 4, 4);
            chunks[std::string(hd, 4)] = {(uint32_t)(o + 8), sz};
            o += 8 + sz;
        }
        return true;
    }
    ~Data() { if (f) std::fclose(f); }
    bool load(const char* name, Chunk& c) {
        auto it = chunks.find(name);
        if (it == chunks.end()) { err = std::string("falta el chunk ") + name; return false; }
        c.base = it->second.first; c.d.resize(it->second.second);
        std::fseek(f, (long)c.base, SEEK_SET);
        if (std::fread(c.d.data(), 1, c.d.size(), f) != c.d.size()) { err = std::string("lectura incompleta de ") + name; return false; }
        return true;
    }
    // solo la tabla de punteros (u32 cuenta + direcciones) de un chunk grande, sin cargar sus datos
    bool table(const char* name, std::vector<uint32_t>& out) {
        auto it = chunks.find(name);
        if (it == chunks.end()) { err = std::string("falta el chunk ") + name; return false; }
        uint32_t n = 0;
        if (it->second.second < 4 || !readAt(it->second.first, &n, 4) || n > 100000) { err = std::string(name) + " corrupto"; return false; }
        out.resize(n);
        return n == 0 || readAt(it->second.first + 4, out.data(), n * 4);
    }
    bool readAt(uint32_t addr, void* dst, size_t n) { std::fseek(f, (long)addr, SEEK_SET); return std::fread(dst, 1, n, f) == n; }
};

// lista con tabla de punteros: u32 cuenta + cuenta x u32 direccion
std::vector<uint32_t> ptrList(const Chunk& c, uint32_t addr) {
    std::vector<uint32_t> v; uint32_t n = c.u32(addr);
    if (n > 10000000) return v;
    v.reserve(n);
    for (uint32_t i = 0; i < n; i++) v.push_back(c.u32(addr + 4 + 4 * i));
    return v;
}

using IndexMap = std::unordered_map<uint32_t, int>;
IndexMap indexOf(const std::vector<uint32_t>& v) { IndexMap m; m.reserve(v.size() * 2); for (size_t i = 0; i < v.size(); i++) m[v[i]] = (int)i; return m; }
int idx(const IndexMap& m, uint32_t ptr) { if (!ptr) return -1; auto it = m.find(ptr); return it == m.end() ? -1 : it->second; }

// ------- opcodes: bytecode 13 usa numeracion antigua; el runner usa la nueva (la que expone UndertaleModTool)
enum : uint8_t {
    N_CONV = 7, N_CMP = 21, N_POP = 69, N_PUSHI = 132, N_DUP = 134, N_RET = 156, N_EXIT = 157, N_POPZ = 158,
    N_B = 182, N_BT = 183, N_BF = 184, N_PUSHENV = 186, N_POPENV = 187, N_PUSH = 192, N_PUSHLOC = 193, N_PUSHGLB = 194,
    N_PUSHBLTN = 195, N_CALL = 217, N_BREAK = 255 };
enum : uint8_t { DT_DOUBLE = 0, DT_FLOAT = 1, DT_INT32 = 2, DT_INT64 = 3, DT_BOOL = 4, DT_VAR = 5, DT_STRING = 6, DT_INT16 = 15 };

// devuelve el opcode nuevo (0 = desconocido); cmp rellena el tipo de comparacion
uint8_t newOpcode(uint8_t o, uint8_t& cmp) {
    cmp = 0;
    if (o >= 0x03 && o <= 0x10) return (uint8_t)(o + 4);          // conv..shr
    if (o >= 0x11 && o <= 0x16) { cmp = (uint8_t)(o - 0x10); return N_CMP; }
    switch (o) {
        case 0x41: return N_POP;
        case 0x82: return N_DUP;
        case 0x84: return N_PUSHI;
        case 0x9D: return N_RET; case 0x9E: return N_EXIT; case 0x9F: return N_POPZ;
        case 0xB7: return N_B; case 0xB8: return N_BT; case 0xB9: return N_BF;
        case 0xBB: return N_PUSHENV; case 0xBC: return N_POPENV;
        case 0xC0: return N_PUSH; case 0xC1: return N_PUSHLOC; case 0xC2: return N_PUSHGLB; case 0xC3: return N_PUSHBLTN;
        case 0xDA: return N_CALL;
        case 0xFF: return N_BREAK;
    }
    return 0;
}

#pragma pack(push, 1)
struct VIns { uint8_t op, t1, t2, cmp; int16_t inst; uint8_t extra, flags; int32_t a, b; };
#pragma pack(pop)
static_assert(sizeof(VIns) == 16, "VIns");

} // namespace

bool convertVsd(const std::string& dataWin, const std::string& outDir, std::string& err) {
    Data dw;
    if (!dw.open(dataWin)) { err = dw.err; return false; }
    Chunk gen8, strg, tpag, sond, sprt, bgnd, scpt, font, objt, room, code, vari, func;
    for (auto [name, c] : std::initializer_list<std::pair<const char*, Chunk*>>{
             {"GEN8", &gen8}, {"STRG", &strg}, {"TPAG", &tpag}, {"SOND", &sond}, {"BGND", &bgnd}, {"SCPT", &scpt},
             {"FONT", &font}, {"OBJT", &objt}, {"VARI", &vari}, {"FUNC", &func}})
        if (!dw.load(name, *c)) { err = dw.err; return false; }

    // ---- strings (puntero a los caracteres; la longitud esta 4 bytes antes)
    std::vector<uint32_t> strPtr = ptrList(strg, strg.base);
    std::vector<std::string> strings(strPtr.size());
    IndexMap strIdx; strIdx.reserve(strPtr.size() * 2);
    for (size_t i = 0; i < strPtr.size(); i++) {
        uint32_t n = strg.u32(strPtr[i]);
        if (!strg.has(strPtr[i] + 4, n)) { err = "STRG corrupto"; return false; }
        strings[i].assign((const char*)&strg.d[strPtr[i] + 4 - strg.base], n);
        strIdx[strPtr[i] + 4] = (int)i;
    }
    auto S = [&](uint32_t ptr) -> const std::string& {
        static const std::string empty; auto it = strIdx.find(ptr); return it == strIdx.end() ? empty : strings[it->second];
    };

    // ---- tablas de punteros -> indices
    std::vector<uint32_t> tpagPtr = ptrList(tpag, tpag.base), sondPtr = ptrList(sond, sond.base),
        bgndPtr = ptrList(bgnd, bgnd.base), scptPtr = ptrList(scpt, scpt.base), fontPtr = ptrList(font, font.base),
        objtPtr = ptrList(objt, objt.base);
    IndexMap tpagIdx = indexOf(tpagPtr), bgndIdx = indexOf(bgndPtr), objtIdx = indexOf(objtPtr);

    std::string vsdata = outDir;
    Out o;
    o.f = std::fopen((vsdata + "/game.vsd").c_str(), "wb");
    if (!o.f) { err = "no se puede crear " + vsdata + "/game.vsd"; return false; }
    o.raw("VSD1", 4); o.u32(1);

    // ---- general
    o.str(S(gen8.u32(gen8.base + 40)));
    o.u32(gen8.u32(gen8.base + 60)); o.u32(gen8.u32(gen8.base + 64));
    {
        uint32_t n = gen8.u32(gen8.base + 124);
        o.u32(n);
        for (uint32_t i = 0; i < n; i++) o.i32(gen8.i32(gen8.base + 128 + 4 * i));
    }

    // ---- strings
    o.u32((uint32_t)strings.size());
    for (auto& s : strings) o.str(s);

    // ---- paginas de textura: solo dimensiones (los PNG se extraen aparte)
    {
        // TXTR pesa ~200 MB: solo se lee la tabla y la cabecera de cada PNG (cargarlo entero agotaba la RAM de la Vita)
        std::vector<uint32_t> tp;
        if (!dw.table("TXTR", tp)) { std::fclose(o.f); err = dw.err; return false; }
        o.u32((uint32_t)tp.size());
        for (uint32_t p : tp) {
            uint32_t rec[2];
            if (!dw.readAt(p, rec, 8)) { std::fclose(o.f); err = "TXTR corrupto"; return false; }
            uint32_t blob = rec[1];
            uint8_t ih[24];
            if (!dw.readAt(blob, ih, 24) || std::memcmp(ih, "\x89PNG", 4) != 0) { std::fclose(o.f); err = "textura no es PNG"; return false; }
            uint32_t w = (ih[16] << 24) | (ih[17] << 16) | (ih[18] << 8) | ih[19], h = (ih[20] << 24) | (ih[21] << 16) | (ih[22] << 8) | ih[23];
            o.i32((int32_t)w); o.i32((int32_t)h);
        }
    }

    // ---- texture page items
    o.u32((uint32_t)tpagPtr.size());
    for (uint32_t p : tpagPtr) {
        for (int k = 0; k < 10; k++) o.u16(tpag.u16(p + 2 * k));
        o.i32(tpag.i16(p + 20));
    }

    // ---- sprites
    if (!dw.load("SPRT", sprt)) { std::fclose(o.f); err = dw.err; return false; }
    std::vector<uint32_t> sprtPtr = ptrList(sprt, sprt.base);
    IndexMap sprtIdx = indexOf(sprtPtr);
    o.u32((uint32_t)sprtPtr.size());
    for (uint32_t p : sprtPtr) {
        uint32_t w = sprt.u32(p + 4), h = sprt.u32(p + 8);
        o.str(S(sprt.u32(p)));
        o.u32(w); o.u32(h);
        o.i32(sprt.i32(p + 48)); o.i32(sprt.i32(p + 52));                              // origen
        o.i32(sprt.i32(p + 12)); o.i32(sprt.i32(p + 16)); o.i32(sprt.i32(p + 24)); o.i32(sprt.i32(p + 20)); // L, R, T, B
        o.u32(sprt.u32(p + 40)); o.u32(sprt.u32(p + 44));
        uint32_t nt = sprt.u32(p + 56);
        o.u32(nt);
        for (uint32_t i = 0; i < nt; i++) o.i32(idx(tpagIdx, sprt.u32(p + 60 + 4 * i)));
        uint32_t mp = p + 60 + 4 * nt;
        uint32_t nm = sprt.u32(mp); mp += 4;
        o.u32(nm);
        uint32_t len = ((w + 7) / 8) * h;
        for (uint32_t i = 0; i < nm; i++) {
            o.u32(w); o.u32(h); o.u32(len);
            if (!sprt.has(mp, len)) { std::fclose(o.f); err = "mascara de sprite fuera de rango"; return false; }
            o.raw(&sprt.d[mp - sprt.base], len); mp += len;
        }
    }
    sprt.d.clear(); sprt.d.shrink_to_fit();

    // ---- backgrounds
    o.u32((uint32_t)bgndPtr.size());
    for (uint32_t p : bgndPtr) { o.str(S(bgnd.u32(p))); o.i32(idx(tpagIdx, bgnd.u32(p + 16))); o.b(bgnd.u32(p + 4) != 0); o.b(bgnd.u32(p + 8) != 0); }

    // ---- fuentes
    o.u32((uint32_t)fontPtr.size());
    for (uint32_t p : fontPtr) {
        o.str(S(font.u32(p))); o.str(S(font.u32(p + 4)));
        { uint32_t em = font.u32(p + 8); float fe; if (em & 0x80000000u) { uint32_t bits = em; std::memcpy(&fe, &bits, 4); fe = -fe; } else fe = (float)em; o.f32(fe); }
        o.b(font.u32(p + 12) != 0); o.b(font.u32(p + 16) != 0);
        o.i32(idx(tpagIdx, font.u32(p + 28))); o.f32(font.f32(p + 32)); o.f32(font.f32(p + 36));
        std::vector<uint32_t> gl = ptrList(font, p + 40);
        o.u32((uint32_t)gl.size());
        for (uint32_t g : gl) {
            o.u16(font.u16(g)); o.u16(font.u16(g + 2)); o.u16(font.u16(g + 4)); o.u16(font.u16(g + 6)); o.u16(font.u16(g + 8));
            o.i16(font.i16(g + 10)); o.i16(font.i16(g + 12));
        }
    }

    // ---- sonidos + audio incrustado
    {
        // AUDO (~38 MB) tambien se lee sonido a sonido
        std::vector<uint32_t> ap;
        if (!dw.table("AUDO", ap)) ap.clear();   // sin audio incrustado
        o.u32((uint32_t)ap.size());
        std::vector<uint8_t> wav;
        for (size_t i = 0; i < ap.size(); i++) {
            uint32_t len = 0;
            if (!dw.readAt(ap[i], &len, 4) || len > 64u * 1024 * 1024) { std::fclose(o.f); err = "AUDO corrupto"; return false; }
            wav.resize(len);
            if (len && !dw.readAt(ap[i] + 4, wav.data(), len)) { std::fclose(o.f); err = "AUDO corrupto"; return false; }
            FILE* wf = std::fopen((vsdata + "/snd/" + std::to_string(i) + ".wav").c_str(), "wb");
            if (!wf) { std::fclose(o.f); err = "no se puede crear snd/" + std::to_string(i) + ".wav"; return false; }
            bool wok = std::fwrite(wav.data(), 1, len, wf) == len; wok = std::fclose(wf) == 0 && wok;
            if (!wok) { std::fclose(o.f); err = "escritura incompleta de snd/" + std::to_string(i) + ".wav (tarjeta llena?)"; return false; }
        }
        std::vector<uint8_t>().swap(wav);
        o.u32((uint32_t)sondPtr.size());
        for (uint32_t p : sondPtr) {
            uint32_t flags = sond.u32(p + 4); int32_t aid = sond.i32(p + 32);
            o.str(S(sond.u32(p))); o.u32(flags); o.str(S(sond.u32(p + 8))); o.str(S(sond.u32(p + 12)));
            o.f32(sond.f32(p + 20)); o.f32(sond.f32(p + 24)); o.i32(aid);
            // sonido incrustado (o comprimido dentro de data.win): AUDO[audioId]
            // (el exportador de UndertaleModTool marcaba tambien como incrustados los .ogg externos con audioId 0:
            //  la musica de jefes sonaba con snd/0.wav; aqui solo lo son los que llevan el flag 1)
            bool embedded = (flags & 0x1) != 0;
            o.i32(embedded && aid >= 0 && (size_t)aid < ap.size() ? aid : -1);
        }
    }

    // ---- nombres de funciones
    size_t nFunc = func.d.size() / 12;
    o.u32((uint32_t)nFunc);
    for (size_t i = 0; i < nFunc; i++) o.str(S(func.u32(func.base + (uint32_t)i * 12)));

    // ---- variables (nombres unicos) y codigo
    size_t nVari = vari.d.size() / 12;
    std::vector<std::string> varNames; std::unordered_map<std::string, int> varId;
    std::vector<int> variNameId(nVari);
    auto VarName = [&](const std::string& n) { auto it = varId.find(n); if (it != varId.end()) return it->second; int id = (int)varNames.size(); varNames.push_back(n); varId[n] = id; return id; };
    for (size_t i = 0; i < nVari; i++) variNameId[i] = VarName(S(vari.u32(vari.base + (uint32_t)i * 12)));

    if (!dw.load("CODE", code)) { std::fclose(o.f); err = dw.err; return false; }
    std::vector<uint32_t> codePtr = ptrList(code, code.base);
    IndexMap codeIdx = indexOf(codePtr);
    std::vector<std::vector<VIns>> ins(codePtr.size());
    std::vector<std::vector<uint32_t>> insAddr(codePtr.size()); // direccion absoluta (bytes) de cada instruccion
    std::vector<uint32_t> allAddr; std::vector<std::pair<uint32_t, uint32_t>> allRef; // para resolver cadenas VARI/FUNC
    std::vector<double> dbl; std::unordered_map<uint64_t, int> dblIdx;
    auto Dbl = [&](double d) { uint64_t k; std::memcpy(&k, &d, 8); auto it = dblIdx.find(k); if (it != dblIdx.end()) return it->second; int i = (int)dbl.size(); dbl.push_back(d); dblIdx[k] = i; return i; };
    int unknownOps = 0;

    for (size_t c = 0; c < codePtr.size(); c++) {
        uint32_t p = codePtr[c];
        uint32_t len = code.u32(p + 4);
        uint32_t a0 = p + 8, end = a0 + len;
        std::vector<uint32_t> addrs; std::vector<VIns>& vi = ins[c];
        for (uint32_t a = a0; a < end;) {
            uint32_t w = code.u32(a);
            uint8_t oldOp = (uint8_t)(w >> 24), cmp = 0;
            uint8_t op = newOpcode(oldOp, cmp);
            VIns x{}; x.op = op;
            uint32_t size = 4;
            x.t1 = (w >> 16) & 0xF; x.t2 = (w >> 20) & 0xF;
            if (!op) { unknownOps++; x.op = oldOp; static std::map<int,int> hist; if (++hist[oldOp] == 1) std::fprintf(stderr, "opcode antiguo desconocido 0x%02X\n", oldOp); }
            x.cmp = op == N_CMP ? cmp : (uint8_t)((w >> 8) & 0xFF); // como UndertaleModTool: bits 15-8 de la palabra
            switch (op) {
                case N_B: case N_BT: case N_BF: case N_PUSHENV: case N_POPENV: {
                    int32_t off = (int32_t)(w & 0x00FFFFFF);
                    if (op == N_POPENV && off == 0xF00000) { x.flags = 1; x.a = -1; }
                    else { if (off & 0x00800000) off |= (int32_t)0xFF000000; x.a = off; /* temporal: palabras relativas */ }
                    break;
                }
                case N_PUSH: case N_PUSHLOC: case N_PUSHGLB: case N_PUSHBLTN: case N_PUSHI:
                    x.t1 = (w >> 16) & 0xF;
                    switch (x.t1) {
                        case DT_DOUBLE: { uint64_t b = (uint64_t)code.u32(a + 4) | ((uint64_t)code.u32(a + 8) << 32); double d; std::memcpy(&d, &b, 8); x.a = Dbl(d); size = 12; break; }
                        case DT_INT64: { uint64_t b = (uint64_t)code.u32(a + 4) | ((uint64_t)code.u32(a + 8) << 32); x.a = Dbl((double)(int64_t)b); size = 12; break; }
                        case DT_INT32: x.a = code.i32(a + 4); size = 8; break;
                        case DT_FLOAT: x.a = code.i32(a + 4); size = 8; break;
                        case DT_BOOL: x.a = code.i32(a + 4); size = 8; break;
                        case DT_INT16: x.a = (int16_t)(w & 0xFFFF); break;
                        case DT_STRING: x.a = code.i32(a + 4); size = 8; break;
                        case DT_VAR: { x.inst = (int16_t)(w & 0xFFFF); uint32_t ref = code.u32(a + 4); x.b = (int)((ref >> 24) & 0xF8); size = 8; break; }
                        default: x.a = code.i32(a + 4); size = 8; break;
                    }
                    break;
                case N_POP:
                    if (x.t1 == DT_INT16) { x.flags = 4; x.extra = (uint8_t)(w & 0xFF); x.b = (int)((w >> 8) & 0xFF); }
                    else { x.inst = (int16_t)(w & 0xFFFF); uint32_t ref = code.u32(a + 4); x.b = (int)((ref >> 24) & 0xF8); size = 8; }
                    break;
                case N_CALL: x.b = (int)(w & 0xFFFF); size = 8; break;
                case N_DUP: x.extra = (uint8_t)(w & 0xFF); x.b = (int)((((w >> 8) & 0xFF) & 0x7F) >> 3); break;
                case N_BREAK: x.a = (int16_t)(w & 0xFFFF); break;
                default: break;
            }
            addrs.push_back(a); vi.push_back(x);
            a += size;
        }
        insAddr[c] = std::move(addrs);
        // saltos: de palabras relativas a indice de instruccion
        std::unordered_map<uint32_t, int> byAddr; byAddr.reserve(vi.size() * 2);
        for (size_t i = 0; i < vi.size(); i++) byAddr[insAddr[c][i]] = (int)i;
        byAddr[end] = (int)vi.size();
        for (size_t i = 0; i < vi.size(); i++) {
            VIns& x = vi[i];
            if ((x.op == N_B || x.op == N_BT || x.op == N_BF || x.op == N_PUSHENV || x.op == N_POPENV) && !(x.flags & 1)) {
                uint32_t target = insAddr[c][i] + (uint32_t)(x.a * 4);
                auto it = byAddr.find(target);
                if (it == byAddr.end()) { std::fclose(o.f); err = "salto invalido en " + S(code.u32(p)); return false; }
                x.a = it->second;
            }
        }
    }

    // referencias a variables / funciones: cadenas de apariciones (cada una apunta a la siguiente)
    for (size_t c = 0; c < ins.size(); c++) for (size_t i = 0; i < ins[c].size(); i++) { allAddr.push_back(insAddr[c][i]); allRef.push_back({(uint32_t)c, (uint32_t)i}); }
    auto findIns = [&](uint32_t addr) -> VIns* {
        auto it = std::lower_bound(allAddr.begin(), allAddr.end(), addr);
        if (it == allAddr.end() || *it != addr) return nullptr;
        auto r = allRef[it - allAddr.begin()]; return &ins[r.first][r.second];
    };
    for (size_t k = 0; k < nVari; k++) {
        uint32_t n = vari.u32(vari.base + (uint32_t)k * 12 + 4); int32_t first = vari.i32(vari.base + (uint32_t)k * 12 + 8);
        if (first < 0) continue;
        uint32_t addr = (uint32_t)first;
        for (uint32_t i = 0; i < n; i++) {
            VIns* x = findIns(addr);
            if (!x) { std::fclose(o.f); err = "referencia a variable fuera del codigo"; return false; }
            x->a = variNameId[k];
            addr += code.u32(addr + 4) & 0x07FFFFFF;
        }
    }
    for (size_t k = 0; k < nFunc; k++) {
        uint32_t n = func.u32(func.base + (uint32_t)k * 12 + 4); int32_t first = func.i32(func.base + (uint32_t)k * 12 + 8);
        if (first < 0) continue;
        uint32_t addr = (uint32_t)first;
        for (uint32_t i = 0; i < n; i++) {
            VIns* x = findIns(addr);
            if (!x) { std::fclose(o.f); err = "referencia a funcion fuera del codigo"; return false; }
            if (x->op == N_CALL) x->a = (int)k; else { x->flags |= 2; x->a = (int)k; }
            addr += code.u32(addr + 4) & 0x07FFFFFF;
        }
    }

    o.u32((uint32_t)dbl.size()); for (double d : dbl) o.raw(&d, 8);
    o.u32((uint32_t)codePtr.size());
    for (size_t c = 0; c < codePtr.size(); c++) {
        const std::string& nm = S(code.u32(codePtr[c]));
        o.i32((int32_t)nm.size()); o.raw(nm.data(), nm.size());
        o.u32(0); o.u32(0);   // argumentos / locales: no existen en bytecode 13
        o.u32((uint32_t)ins[c].size());
        if (!ins[c].empty()) o.raw(ins[c].data(), ins[c].size() * sizeof(VIns));
    }
    o.u32((uint32_t)varNames.size()); for (auto& n : varNames) o.str(n);
    std::fprintf(stderr, "codigo: %zu entradas, %zu variables, %zu doubles, opcodes desconocidos=%d\n", codePtr.size(), varNames.size(), dbl.size(), unknownOps);
    // liberar ya la memoria del codigo (la Vita tiene poca): vectores y el chunk CODE
    std::vector<std::vector<VIns>>().swap(ins); std::vector<std::vector<uint32_t>>().swap(insAddr);
    std::vector<uint32_t>().swap(allAddr); std::vector<std::pair<uint32_t, uint32_t>>().swap(allRef);
    code = Chunk();

    // ---- scripts
    o.u32((uint32_t)scptPtr.size());
    for (uint32_t p : scptPtr) { o.str(S(scpt.u32(p))); int32_t cid = scpt.i32(p + 4); o.i32(cid >= 0 && (size_t)cid < codePtr.size() ? cid : -1); }

    // ---- objetos
    {
        std::vector<uint32_t> sprtPtr2 = sprtPtr; (void)sprtPtr2;
        o.u32((uint32_t)objtPtr.size());
        for (uint32_t p : objtPtr) {
            o.str(S(objt.u32(p)));
            int32_t spr = objt.i32(p + 4);
            o.i32(spr >= 0 && (size_t)spr < sprtPtr.size() ? spr : -1);
            o.b(objt.u32(p + 8) != 0); o.b(objt.u32(p + 12) != 0); o.i32(objt.i32(p + 16)); o.b(objt.u32(p + 20) != 0);
            int32_t par = objt.i32(p + 24);
            o.i32(par >= 0 && (size_t)par < objtPtr.size() ? par : -1);
            int32_t msk = objt.i32(p + 28);
            o.i32(msk >= 0 && (size_t)msk < sprtPtr.size() ? msk : -1);
            uint32_t nv = objt.u32(p + 64);
            uint32_t ev = p + 80 + nv * 8;   // lista de listas de eventos
            struct E { int32_t type; uint32_t sub; int32_t code; };
            std::vector<E> evs;
            std::vector<uint32_t> types = ptrList(objt, ev);
            for (size_t t = 0; t < types.size(); t++) {
                for (uint32_t e : ptrList(objt, types[t])) {
                    int32_t codeId = -1;
                    for (uint32_t act : ptrList(objt, e + 4)) {
                        int32_t cid = objt.i32(act + 32);
                        if (cid >= 0 && (size_t)cid < codePtr.size()) codeId = cid;
                    }
                    evs.push_back({(int32_t)t, objt.u32(e), codeId});
                }
            }
            o.u32((uint32_t)evs.size());
            for (auto& e : evs) { o.i32(e.type); o.u32(e.sub); o.i32(e.code); }
        }
    }

    // ---- rooms
    if (!dw.load("ROOM", room)) { std::fclose(o.f); err = dw.err; return false; }
    {
        std::vector<uint32_t> rp = ptrList(room, room.base);
        o.u32((uint32_t)rp.size());
        auto codeOf = [&](int32_t cid) { return cid >= 0 && (size_t)cid < codePtr.size() ? cid : -1; };
        for (uint32_t p : rp) {
            o.str(S(room.u32(p))); o.str(S(room.u32(p + 4)));
            o.u32(room.u32(p + 8)); o.u32(room.u32(p + 12)); o.u32(room.u32(p + 16)); o.b(room.u32(p + 20) != 0);
            o.u32(room.u32(p + 24) | 0xFF000000u); o.b(room.u32(p + 28) != 0);
            o.i32(codeOf(room.i32(p + 32))); // (UndertaleModTool anade el alfa 0xFF al color de fondo)
            o.u32(room.u32(p + 36));
            auto bgs = ptrList(room, room.u32(p + 40)); o.u32((uint32_t)bgs.size());
            for (uint32_t b : bgs) {
                o.b(room.u32(b) != 0); o.b(room.u32(b + 4) != 0); int32_t bg = room.i32(b + 8); o.i32(bg >= 0 && (size_t)bg < bgndPtr.size() ? bg : -1);
                o.i32(room.i32(b + 12)); o.i32(room.i32(b + 16)); o.b(room.u32(b + 20) != 0); o.b(room.u32(b + 24) != 0);
                o.i32(room.i32(b + 28)); o.i32(room.i32(b + 32)); o.b(room.u32(b + 36) != 0);
            }
            auto vws = ptrList(room, room.u32(p + 44)); o.u32((uint32_t)vws.size());
            for (uint32_t v : vws) {
                o.b(room.u32(v) != 0);
                for (int k = 1; k <= 8; k++) o.i32(room.i32(v + 4 * k));
                o.u32(room.u32(v + 36)); o.u32(room.u32(v + 40)); o.i32(room.i32(v + 44)); o.i32(room.i32(v + 48));
                int32_t ob = room.i32(v + 52); o.i32(ob >= 0 && (size_t)ob < objtPtr.size() ? ob : -1);
            }
            auto ins2 = ptrList(room, room.u32(p + 48)); o.u32((uint32_t)ins2.size());
            for (uint32_t g : ins2) {
                int32_t ob = room.i32(g + 8);
                o.i32(room.i32(g)); o.i32(room.i32(g + 4)); o.i32(ob >= 0 && (size_t)ob < objtPtr.size() ? ob : -1); o.u32(room.u32(g + 12));
                o.i32(codeOf(room.i32(g + 16))); o.f32(room.f32(g + 20)); o.f32(room.f32(g + 24)); o.u32(room.u32(g + 28)); o.f32(room.f32(g + 32));
            }
            auto tls = ptrList(room, room.u32(p + 52)); o.u32((uint32_t)tls.size());
            for (uint32_t t : tls) {
                int32_t bg = room.i32(t + 8);
                o.i32(room.i32(t)); o.i32(room.i32(t + 4)); o.i32(bg >= 0 && (size_t)bg < bgndPtr.size() ? bg : -1);
                o.i32(room.i32(t + 12)); o.i32(room.i32(t + 16)); o.u32(room.u32(t + 20)); o.u32(room.u32(t + 24));
                o.i32(room.i32(t + 28)); o.u32(room.u32(t + 32)); o.f32(room.f32(t + 36)); o.f32(room.f32(t + 40)); o.u32(room.u32(t + 44));
            }
        }
    }
    o.raw("END!", 4);
    std::fclose(o.f);
    return true;
}


// Extrae cada pagina PNG de TXTR (sin cargar los ~200 MB a la vez) y la guarda como tex/N.vdx
bool convertVsdTextures(const std::string& dataWin, const std::string& texDir, const ConvertProgress& progress, float f0, float f1, std::string& err) {
    Data dw;
    if (!dw.open(dataWin)) { err = dw.err; return false; }
    auto it = dw.chunks.find("TXTR");
    if (it == dw.chunks.end()) { err = "falta el chunk TXTR"; return false; }
    uint32_t base = it->second.first, n = 0;
    if (!dw.readAt(base, &n, 4) || n > 4096) { err = "TXTR corrupto"; return false; }
    std::vector<uint32_t> ptr(n);
    if (n && !dw.readAt(base + 4, ptr.data(), n * 4)) { err = "TXTR corrupto"; return false; }
    struct Blob { uint32_t off, len; };
    std::vector<Blob> blobs(n); uint64_t total = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t rec[2];
        if (!dw.readAt(ptr[i], rec, 8)) { err = "TXTR corrupto"; return false; }
        uint32_t off = rec[1], pos = off + 8;     // tras la firma PNG: chunks len(BE) tipo datos crc
        for (;;) {
            uint8_t h[8];
            if (!dw.readAt(pos, h, 8)) { err = "PNG incrustado truncado"; return false; }
            uint32_t len = ((uint32_t)h[0] << 24) | ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
            pos += 12 + len;
            if (std::memcmp(h + 4, "IEND", 4) == 0) break;
        }
        blobs[i] = {off, pos - off}; total += pos - off;
    }
    uint64_t done = 0; std::vector<uint8_t> buf;
    for (uint32_t i = 0; i < n; i++) {
        std::string out = texDir + "/" + std::to_string(i) + ".vdx";
        std::string label = "Texturas del juego " + std::to_string(i + 1) + "/" + std::to_string(n);
        if (progress) progress(label.c_str(), f0 + (f1 - f0) * (total ? (float)done / (float)total : 1.f));
        struct stat st;
        if (stat(out.c_str(), &st) != 0 || st.st_size < 32) {      // reanudable: se salta lo ya convertido
            buf.resize(blobs[i].len);
            if (!dw.readAt(blobs[i].off, buf.data(), buf.size())) { err = "lectura de textura incompleta"; return false; }
            if (!pngToVdx(buf.data(), buf.size(), out, false, err)) return false;
        }
        done += blobs[i].len;
    }
    return true;
}

} // namespace vs
