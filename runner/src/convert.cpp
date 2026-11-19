// Conversion de texturas: PNG -> DXT1/DXT5 (+RLE por bloques) en formato .vdx, y orquestacion del primer arranque.
// El compresor es propio (sin dependencias): ajuste de rango por eje principal + refinado por minimos cuadrados.
#include "convert.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <vector>
#include "../third_party/stb_image.h"
#include "lz4lite.h"
#include <zlib.h>

namespace vs {

namespace {

constexpr uint32_t GL_DXT1_RGB = 0x83F0, GL_DXT5 = 0x83F3;

inline int expand5(int v) { return (v << 3) | (v >> 2); }
inline int expand6(int v) { return (v << 2) | (v >> 4); }
inline uint16_t pack565(int r, int g, int b) {
    r = std::min(255, std::max(0, r)); g = std::min(255, std::max(0, g)); b = std::min(255, std::max(0, b));
    return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
}
inline void unpack565(uint16_t c, int& r, int& g, int& b) { r = expand5(c >> 11); g = expand6((c >> 5) & 63); b = expand5(c & 31); }

// bloque de color de 8 bytes. px: 16 pixeles RGB; active: mascara de los que cuentan (los transparentes no ensucian el color)
void encodeColor(uint8_t* out, const int px[16][3], uint32_t active) {
    if (!active) { std::memset(out, 0, 8); return; }
    // media y covarianza de los pixeles activos
    int n = 0; double mean[3] = {0, 0, 0};
    int mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0};
    for (int i = 0; i < 16; i++) if (active >> i & 1) {
        n++;
        for (int c = 0; c < 3; c++) { mean[c] += px[i][c]; mn[c] = std::min(mn[c], px[i][c]); mx[c] = std::max(mx[c], px[i][c]); }
    }
    for (int c = 0; c < 3; c++) mean[c] /= n;
    if (mn[0] == mx[0] && mn[1] == mx[1] && mn[2] == mx[2]) {   // color unico
        uint16_t q = pack565(mn[0], mn[1], mn[2]);
        // elegir el mejor par (q, q') para aproximar mejor: basta c0=q, todos los indices 0
        out[0] = q & 255; out[1] = q >> 8; out[2] = q & 255; out[3] = q >> 8; std::memset(out + 4, 0, 4);
        return;
    }
    double cov[6] = {0, 0, 0, 0, 0, 0}; // rr rg rb gg gb bb
    for (int i = 0; i < 16; i++) if (active >> i & 1) {
        double d0 = px[i][0] - mean[0], d1 = px[i][1] - mean[1], d2 = px[i][2] - mean[2];
        cov[0] += d0 * d0; cov[1] += d0 * d1; cov[2] += d0 * d2; cov[3] += d1 * d1; cov[4] += d1 * d2; cov[5] += d2 * d2;
    }
    // eje principal (iteracion de potencias), arrancando por la diagonal de la caja
    double v[3] = {(double)(mx[0] - mn[0]), (double)(mx[1] - mn[1]), (double)(mx[2] - mn[2])};
    for (int it = 0; it < 4; it++) {
        double x = cov[0] * v[0] + cov[1] * v[1] + cov[2] * v[2];
        double y = cov[1] * v[0] + cov[3] * v[1] + cov[4] * v[2];
        double z = cov[2] * v[0] + cov[4] * v[1] + cov[5] * v[2];
        double m = std::max(std::fabs(x), std::max(std::fabs(y), std::fabs(z)));
        if (m < 1e-9) break;
        v[0] = x / m; v[1] = y / m; v[2] = z / m;
    }
    int lo = -1, hi = -1; double tlo = 1e30, thi = -1e30;
    for (int i = 0; i < 16; i++) if (active >> i & 1) {
        double t = (px[i][0] - mean[0]) * v[0] + (px[i][1] - mean[1]) * v[1] + (px[i][2] - mean[2]) * v[2];
        if (t < tlo) { tlo = t; lo = i; }
        if (t > thi) { thi = t; hi = i; }
    }
    uint16_t e0 = pack565(px[hi][0], px[hi][1], px[hi][2]), e1 = pack565(px[lo][0], px[lo][1], px[lo][2]);
    uint8_t idx[16];
    for (int pass = 0; pass < 3; pass++) {
        if (e0 < e1) std::swap(e0, e1);
        int pal[4][3];
        unpack565(e0, pal[0][0], pal[0][1], pal[0][2]); unpack565(e1, pal[1][0], pal[1][1], pal[1][2]);
        for (int c = 0; c < 3; c++) { pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3; pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3; }
        static const double T[4] = {1.0, 0.0, 2.0 / 3.0, 1.0 / 3.0};
        double a = 0, b = 0, cc = 0, P[3] = {0, 0, 0}, Q[3] = {0, 0, 0};
        for (int i = 0; i < 16; i++) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 4; k++) {
                int d0 = px[i][0] - pal[k][0], d1 = px[i][1] - pal[k][1], d2 = px[i][2] - pal[k][2];
                int d = d0 * d0 + d1 * d1 + d2 * d2;
                if (d < bd) { bd = d; best = k; }
            }
            idx[i] = (uint8_t)best;
            if (active >> i & 1) {
                double t = T[best], s = 1.0 - t;
                a += t * t; b += t * s; cc += s * s;
                for (int c = 0; c < 3; c++) { P[c] += t * px[i][c]; Q[c] += s * px[i][c]; }
            }
        }
        if (pass == 2) break;
        double det = a * cc - b * b;
        if (std::fabs(det) < 1e-6) break;
        double r0[3], r1[3];
        for (int c = 0; c < 3; c++) { r0[c] = (cc * P[c] - b * Q[c]) / det; r1[c] = (a * Q[c] - b * P[c]) / det; }
        uint16_t n0 = pack565((int)std::lround(r0[0]), (int)std::lround(r0[1]), (int)std::lround(r0[2]));
        uint16_t n1 = pack565((int)std::lround(r1[0]), (int)std::lround(r1[1]), (int)std::lround(r1[2]));
        if (n0 == e0 && n1 == e1) break;
        e0 = n0; e1 = n1;
    }
    // el modo de 4 colores exige c0 > c1; si son iguales todos los indices deben ser 0
    if (e0 == e1) std::memset(idx, 0, 16);
    else if (e0 < e1) { std::swap(e0, e1); for (int i = 0; i < 16; i++) idx[i] = idx[i] < 2 ? (uint8_t)(idx[i] ^ 1) : (uint8_t)(idx[i] ^ 1); }
    out[0] = e0 & 255; out[1] = e0 >> 8; out[2] = e1 & 255; out[3] = e1 >> 8;
    uint32_t bits = 0;
    for (int i = 0; i < 16; i++) bits |= (uint32_t)idx[i] << (2 * i);
    std::memcpy(out + 4, &bits, 4);
}

// bloque de alfa DXT5 de 8 bytes
void encodeAlpha(uint8_t* out, const int a[16]) {
    int mn = 255, mx = 0;
    for (int i = 0; i < 16; i++) { mn = std::min(mn, a[i]); mx = std::max(mx, a[i]); }
    if (mn == mx) { out[0] = out[1] = (uint8_t)mx; std::memset(out + 2, 0, 6); return; }
    int pal[8]; pal[0] = mx; pal[1] = mn;
    for (int k = 1; k <= 6; k++) pal[1 + k] = ((7 - k) * mx + k * mn) / 7;
    uint64_t bits = 0;
    for (int i = 0; i < 16; i++) {
        int best = 0, bd = 1 << 30;
        for (int k = 0; k < 8; k++) { int d = std::abs(a[i] - pal[k]); if (d < bd) { bd = d; best = k; } }
        bits |= (uint64_t)best << (3 * i);
    }
    out[0] = (uint8_t)mx; out[1] = (uint8_t)mn;
    for (int i = 0; i < 6; i++) out[2 + i] = (uint8_t)(bits >> (8 * i));
}

// RLE por bloques: token u32; bit31 = repetir N veces el bloque siguiente, si no N bloques literales
void rleBlocks(const std::vector<uint8_t>& data, size_t bs, std::vector<uint8_t>& out) {
    size_t n = data.size() / bs, litStart = 0, i = 0;
    auto put32 = [&](uint32_t v) { uint8_t b[4]; std::memcpy(b, &v, 4); out.insert(out.end(), b, b + 4); };
    while (i < n) {
        size_t j = i + 1;
        while (j < n && std::memcmp(&data[j * bs], &data[i * bs], bs) == 0) j++;
        if (j - i >= 3) {
            if (i > litStart) { put32((uint32_t)(i - litStart)); out.insert(out.end(), data.begin() + litStart * bs, data.begin() + i * bs); }
            put32(0x80000000u | (uint32_t)(j - i)); out.insert(out.end(), data.begin() + i * bs, data.begin() + (i + 1) * bs);
            litStart = j;
        }
        i = j;
    }
    if (n > litStart) { put32((uint32_t)(n - litStart)); out.insert(out.end(), data.begin() + litStart * bs, data.begin() + n * bs); }
}

bool writeFileAtomic(const std::string& path, const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, std::string& err) {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { err = "no se puede escribir " + tmp; return false; }
    bool ok = std::fwrite(a.data(), 1, a.size(), f) == a.size() && std::fwrite(b.data(), 1, b.size(), f) == b.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) { err = "escritura incompleta en " + tmp + " (tarjeta llena?)"; std::remove(tmp.c_str()); return false; }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { err = "no se puede renombrar " + tmp; return false; }
    return true;
}

bool fileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
bool makeDir(const std::string& p) {
#ifdef _WIN32
    return mkdir(p.c_str()) == 0 || fileExists(p);
#else
    return mkdir(p.c_str(), 0777) == 0 || fileExists(p);
#endif
}
uint64_t fileSize(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0; }

} // namespace

bool pngToVdx(const unsigned char* png, size_t pngLen, const std::string& outPath, bool pot, std::string& err) {
    int rw, rh, comp;
    unsigned char* px = stbi_load_from_memory(png, (int)pngLen, &rw, &rh, &comp, 4);
    if (!px) { err = "PNG no valido"; return false; }
    int w, h;
    if (pot) { w = 4; while (w < rw) w <<= 1; h = 4; while (h < rh) h <<= 1; }
    else { w = (rw + 3) & ~3; h = (rh + 3) & ~3; }
    // imagen completa del tamano de la textura; el relleno es transparente
    bool opaque = w == rw && h == rh;
    if (opaque) for (size_t i = 3; i < (size_t)rw * rh * 4; i += 4) if (px[i] != 255) { opaque = false; break; }
    const size_t bs = opaque ? 8 : 16;
    std::vector<uint8_t> blocks((size_t)(w / 4) * (h / 4) * bs);
    auto at = [&](int x, int y, int c) -> int { return (x < rw && y < rh) ? px[((size_t)y * rw + x) * 4 + c] : 0; };
    for (int by = 0; by < h / 4; by++) {
        for (int bx = 0; bx < w / 4; bx++) {
            uint8_t* dst = &blocks[((size_t)by * (w / 4) + bx) * bs];
            int rgb[16][3], al[16]; uint32_t active = 0, any = 0;
            for (int k = 0; k < 16; k++) {
                int x = bx * 4 + (k & 3), y = by * 4 + (k >> 2);
                al[k] = at(x, y, 3);
                for (int c = 0; c < 3; c++) rgb[k][c] = at(x, y, c);
                if (al[k] > 0) active |= 1u << k;
                any |= (uint32_t)al[k];
            }
            if (opaque) encodeColor(dst, rgb, 0xFFFF);
            else if (!any) std::memset(dst, 0, 16);
            else { encodeAlpha(dst, al); encodeColor(dst + 8, rgb, active); }
        }
    }
    stbi_image_free(px);
    uint32_t hdr[8] = {0, (uint32_t)w, (uint32_t)h, (uint32_t)rw, (uint32_t)rh, opaque ? GL_DXT1_RGB : GL_DXT5, 0, 0};
    return writeVdx4(outPath, hdr, blocks, err);
}

// VDX4: misma cabecera que VDX3 pero los bloques DXT van comprimidos con LZ4 (lz4lite.h): ~45 % menos bytes que el
// RLE de VDX3 (93,6 -> 53,4 MB las 74 paginas), y la tarjeta de la Vita lee a ~20 MB/s. hdr[7] = 0: sin comprimir.
bool writeVdx4(const std::string& outPath, uint32_t hdr[8], const std::vector<uint8_t>& blocks, std::string& err) {
    std::vector<uint8_t> z;
    lz4lite::compress(blocks.data(), blocks.size(), z);
    bool packed = z.size() < blocks.size();
    hdr[0] = 0x34584456u; /* "VDX4" */ hdr[6] = (uint32_t)blocks.size(); hdr[7] = packed ? (uint32_t)z.size() : 0;
    std::vector<uint8_t> head((uint8_t*)hdr, (uint8_t*)hdr + 32);
    return writeFileAtomic(outPath, head, packed ? z : blocks, err);
}

namespace {
// lee un .vdx de cualquier version; 'blocks' = datos DXT sin comprimir. ver = 2, 3 o 4 (0 = no es vdx)
int readAnyVdx(const std::string& path, uint32_t hdr[8], std::vector<uint8_t>& blocks) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    std::vector<uint8_t> file((size_t)fileSize(path));
    bool ok = std::fread(file.data(), 1, file.size(), f) == file.size(); std::fclose(f);
    if (!ok || file.size() < 32) return 0;
    std::memcpy(hdr, file.data(), 32);
    int ver = !std::memcmp(hdr, "VDX4", 4) ? 4 : !std::memcmp(hdr, "VDX3", 4) ? 3 : !std::memcmp(hdr, "VDX2", 4) ? 2 : 0;
    if (!ver) return 0;
    blocks.resize(hdr[6]);
    const uint8_t* p = file.data() + 32; size_t n = file.size() - 32;
    if (!hdr[7]) { if (n < blocks.size()) return 0; std::memcpy(blocks.data(), p, blocks.size()); return ver; }
    if (n < hdr[7]) return 0;
    if (ver == 4) return lz4lite::decompress(p, hdr[7], blocks.data(), blocks.size()) ? ver : 0;
    if (ver == 2) { uLongf len = (uLongf)blocks.size(); return uncompress(blocks.data(), &len, p, hdr[7]) == Z_OK && len == blocks.size() ? ver : 0; }
    // VDX3: RLE por bloques
    size_t bs = hdr[5] == 0x83F0 || hdr[5] == 0x83F1 ? 8 : 16, o = 0; const uint8_t* end = p + hdr[7];
    while (p + 4 <= end) {
        uint32_t tok; std::memcpy(&tok, p, 4); p += 4; size_t c = tok & 0x7fffffff;
        if (tok & 0x80000000u) { if (p + bs > end || o + c * bs > blocks.size()) return 0; for (size_t k = 0; k < c; k++, o += bs) std::memcpy(&blocks[o], p, bs); p += bs; }
        else { if (p + c * bs > end || o + c * bs > blocks.size()) return 0; std::memcpy(&blocks[o], p, c * bs); p += c * bs; o += c * bs; }
    }
    return o == blocks.size() ? ver : 0;
}
void listVdx(const std::string& dir, std::vector<std::string>& out, int depth) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string p = dir + "/" + n;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".vdx") == 0) out.push_back(p);
        else if (depth > 0 && n.find('.') == std::string::npos) listVdx(p, out, depth - 1);   // data_1, data_2...
    }
    closedir(d);
}
} // namespace

bool vdx4Ready(const std::string& vsdataDir) { return fileExists(vsdataDir + "/vdx4.ok"); }

bool upgradeVdx(const std::string& vsdataDir, const std::string& gameDir, const ConvertProgress& progress, std::string& err) {
    std::vector<std::string> files;
    listVdx(vsdataDir + "/tex", files, 0);
    listVdx(gameDir, files, 1);
    std::sort(files.begin(), files.end());
    size_t done = 0, upgraded = 0;
    for (const std::string& p : files) {
        if (progress) {
            std::string label = "Comprimiendo texturas " + std::to_string(done + 1) + "/" + std::to_string(files.size());
            progress(label.c_str(), files.empty() ? 1.f : (float)done / (float)files.size());
        }
        done++;
        uint32_t hdr[8]; std::vector<uint8_t> blocks;
        FILE* f = std::fopen(p.c_str(), "rb"); char magic[4] = {0};
        if (f) { size_t r = std::fread(magic, 1, 4, f); std::fclose(f); if (r != 4) continue; } else continue;
        if (!std::memcmp(magic, "VDX4", 4)) continue;
        if (!readAnyVdx(p, hdr, blocks)) continue;   // danado o desconocido: se deja como esta (gfx lo ignora o lo recarga)
        if (!writeVdx4(p, hdr, blocks, err)) return false;
        upgraded++;
    }
    if (FILE* m = std::fopen((vsdataDir + "/vdx4.ok").c_str(), "wb")) { std::fprintf(m, "%u\n", (unsigned)upgraded); std::fclose(m); }
    if (progress) progress("Listo", 1.f);
    return true;
}

// ------------------------------------------------------------------ primer arranque
bool convertVsdTextures(const std::string& dataWin, const std::string& texDir, const ConvertProgress& prog, float f0, float f1, std::string& err);

bool convertGame(const std::string& dataWin, const std::string& srcGameDir, const std::string& outDir,
                 const ConvertProgress& progress, std::string& err) {
    auto prog = [&](const char* what, float f) { if (progress) progress(what, f); };
    std::string vsdata = outDir + "/vsdata";
    makeDir(outDir); makeDir(vsdata); makeDir(vsdata + "/tex"); makeDir(vsdata + "/snd");
    // marca de conversion en curso: si el programa se cierra a mitad, el siguiente arranque la reanuda
    if (FILE* m = std::fopen((vsdata + "/convirtiendo").c_str(), "wb")) { std::fputs("1", m); std::fclose(m); }

    prog("Leyendo data.win (codigo, sprites, rooms...)", 0.0f);
    if (!convertVsd(dataWin, vsdata, err)) return false;
    prog("Datos del juego listos", 0.03f);

    if (!convertVsdTextures(dataWin, vsdata + "/tex", progress, 0.03f, 0.75f, err)) return false;

    // imagenes externas data_*/ *.png -> *.png.vdx junto al original (tamano potencia de dos)
    std::vector<std::string> pngs; uint64_t total = 0;
    if (DIR* d = opendir(srcGameDir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.compare(0, 5, "data_") != 0) continue;
            std::string sub = srcGameDir + "/" + n;
            if (DIR* d2 = opendir(sub.c_str())) {
                while (dirent* e2 = readdir(d2)) {
                    std::string f = e2->d_name;
                    if (f.size() > 4 && (f.compare(f.size() - 4, 4, ".png") == 0 || f.compare(f.size() - 4, 4, ".PNG") == 0)) { pngs.push_back(sub + "/" + f); total += fileSize(sub + "/" + f); }
                }
                closedir(d2);
            }
        }
        closedir(d);
    }
    std::sort(pngs.begin(), pngs.end());
    uint64_t done = 0;
    for (size_t i = 0; i < pngs.size(); i++) {
        const std::string& src = pngs[i];
        std::string dst = src + ".vdx";
        std::string label = "Imagenes del juego " + std::to_string(i + 1) + "/" + std::to_string(pngs.size());
        prog(label.c_str(), 0.75f + 0.25f * (total ? (float)done / (float)total : 1.f));
        if (!fileExists(dst)) {
            FILE* f = std::fopen(src.c_str(), "rb");
            if (!f) { err = "no se puede leer " + src; return false; }
            std::vector<uint8_t> buf((size_t)fileSize(src));
            bool ok = std::fread(buf.data(), 1, buf.size(), f) == buf.size(); std::fclose(f);
            if (!ok || !pngToVdx(buf.data(), buf.size(), dst, true, err)) { if (err.empty()) err = "lectura incompleta de " + src; return false; }
        }
        done += fileSize(src);
    }
    // marca de fin: solo si todo termino bien (permite reanudar si se interrumpe)
    if (FILE* m = std::fopen((vsdata + "/vdx4.ok").c_str(), "wb")) { std::fputs("0\n", m); std::fclose(m); }   // ya escrito en VDX4
    std::remove((vsdata + "/convirtiendo").c_str());
    prog("Listo", 1.0f);
    return true;
}

} // namespace vs
