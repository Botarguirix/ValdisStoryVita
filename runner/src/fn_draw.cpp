// Dibujo: escena (vistas, fondos, tiles, instancias), sprites, texto (font_add_sprite) y primitivas.
#include "runtime.h"
#include "gfx.h"
#include "debugtour.h"
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vs {
extern void (*gOverlayHook)(Game&);   // capa del runner (menu de trucos), definida abajo
extern void (*gViewDrawHook)(Game&);  // dibujo del runner en coordenadas de la vista (entrada Boss Rush)
extern void (*gPreDrawHook)(Game&);   // ajustes del runner justo antes de dibujar (iconos de botones)

int gWinW = 1024, gWinH = 576;

static inline uint32_t bgr(const Value& v) { return (uint32_t)v.num() & 0xFFFFFF; }

void Game::drawFramePart(const SFrame& f, double left, double top, double w, double h, double x, double y, double xs, double ys, uint32_t col, double alpha) {
    // region visible del frame (recortado por el atlas) en coords del bounding box
    double fx0 = f.tx, fy0 = f.ty, fx1 = f.tx + f.sw, fy1 = f.ty + f.sh;
    double ix0 = std::max(fx0, left), iy0 = std::max(fy0, top), ix1 = std::min(fx1, left + w), iy1 = std::min(fy1, top + h);
    if (ix1 <= ix0 || iy1 <= iy0) return;
    float u0 = (float)(f.sx + (ix0 - fx0)), v0 = (float)(f.sy + (iy0 - fy0));
    float u1 = (float)(f.sx + (ix1 - fx0)), v1 = (float)(f.sy + (iy1 - fy0));
    float X0 = (float)(x + (ix0 - left) * xs), Y0 = (float)(y + (iy0 - top) * ys);
    float X1 = (float)(x + (ix1 - left) * xs), Y1 = (float)(y + (iy1 - top) * ys);
    float xy[8] = {X0, Y0, X1, Y0, X1, Y1, X0, Y1};
    float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
    uint32_t c[4] = {col, col, col, col};
    gfx::hudAnchor((float)x, (float)y);
    gfx::quad(f.tex, xy, uv, c, (float)alpha);
}

static void frameQuad(const SFrame& f, double ox, double oy, double x, double y, double xs, double ys, double rot, const uint32_t c[4], double alpha) {
    double lx0 = f.tx - ox, ly0 = f.ty - oy, lx1 = lx0 + f.sw, ly1 = ly0 + f.sh;
    double a = rot * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
    auto P = [&](double lx, double ly, float& X, float& Y) {
        double px = lx * xs, py = ly * ys;
        X = (float)(x + px * ca + py * sa); Y = (float)(y - px * sa + py * ca);
    };
    float xy[8];
    P(lx0, ly0, xy[0], xy[1]); P(lx1, ly0, xy[2], xy[3]); P(lx1, ly1, xy[4], xy[5]); P(lx0, ly1, xy[6], xy[7]);
    float uv[8] = {(float)f.sx, (float)f.sy, (float)(f.sx + f.sw), (float)f.sy, (float)(f.sx + f.sw), (float)(f.sy + f.sh), (float)f.sx, (float)(f.sy + f.sh)};
    gfx::hudAnchor((float)x, (float)y);
    gfx::quad(f.tex, xy, uv, c, (float)alpha);
}

void Game::drawSpriteFrame(int spr, double sub, double x, double y, double xs, double ys, double rot, uint32_t col, double alpha) {
    if (spr < 0 || spr >= (int)sprites.size()) return;
    const RSprite& s = sprites[spr];
    if (s.frames.empty()) return;
    int n = (int)s.frames.size();
    int fi = (int)std::floor(sub); fi %= n; if (fi < 0) fi += n;
    uint32_t c[4] = {col, col, col, col};
    frameQuad(s.frames[fi], s.ox, s.oy, x, y, xs, ys, rot, c, alpha);
}

void Game::drawInstanceDefault(Instance* i) {
    if (i->sprite >= 0) drawSpriteFrame(i->sprite, i->image_index, i->x, i->y, i->xscale, i->yscale, i->angle, i->blend, i->alpha);
}

// ------------------------------------------------------------------ fuentes (font_add_sprite)
struct RGlyph { SFrame f; double w = 0, offset = 0; bool valid = false; };
struct RFont { int first = 32; std::vector<RGlyph> glyphs; double lineH = 16, spaceW = 8; int spr = -1; double sep = 0; bool prop = true; };
static std::vector<RFont> gFonts;

static const RGlyph* glyphFor(const RFont& f, uint32_t ch) {
    int k = (int)ch - f.first;
    if (k < 0 || k >= (int)f.glyphs.size()) return nullptr;
    return &f.glyphs[k];
}

static uint32_t baseChar(uint32_t cp, int& mark);
// ---- Fuente alternativa (menu de MODS: Fuente = Sistema). La plataforma rasteriza cada glifo (Vita: fuente del
// sistema con scePvf, sin distribuir ningun archivo de fuente; PC: una TTF de Windows, solo para pruebas) y aqui se
// juntan en un atlas con filtro lineal. Tiene acentos, enie y signos de apertura propios.
bool (*gFontOpen)(float px, float& ascent, float& lineH) = nullptr;
bool (*gFontGlyph)(uint32_t cp, std::vector<uint8_t>& alpha, int& w, int& h, int& xoff, int& yoff, float& adv) = nullptr;
struct AltGlyph { float u0, v0, u1, v1, xoff, yoff, w, h, adv; };
static struct { bool tried = false, ok = false; int tex = -1; float px = 48, ascent = 0, lineH = 0; std::unordered_map<uint32_t, AltGlyph> g; } gAlt;
static double fontNowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
// Cache del atlas (1.3: rasterizarlo con scePvf cuesta ~640 ms en el primer frame). La plataforma pone la ruta; se
// genera en la propia consola a partir de su fuente del sistema, igual que el resto de vsdata.
std::string gFontCachePath;
static bool altFromCache(double tA) {
    if (gFontCachePath.empty()) return false;
    FILE* f = std::fopen(gFontCachePath.c_str(), "rb");
    if (!f) return false;
    char mg[4]; uint32_t ver = 0, W = 0, H = 0, n = 0; float px = 0, asc = 0, lh = 0;
    bool ok = std::fread(mg, 1, 4, f) == 4 && !std::memcmp(mg, "VSFC", 4) && std::fread(&ver, 4, 1, f) == 1 && ver == 1 &&
              std::fread(&px, 4, 1, f) == 1 && std::fread(&asc, 4, 1, f) == 1 && std::fread(&lh, 4, 1, f) == 1 &&
              std::fread(&W, 4, 1, f) == 1 && std::fread(&H, 4, 1, f) == 1 && std::fread(&n, 4, 1, f) == 1 &&
              px == gAlt.px && W > 0 && W <= 4096 && H > 0 && H <= 4096 && n > 20 && n < 2000 && std::isfinite(lh) && lh > 0;
    std::unordered_map<uint32_t, AltGlyph> gl;
    for (uint32_t k = 0; ok && k < n; k++) { uint32_t cp; AltGlyph a; ok = std::fread(&cp, 4, 1, f) == 1 && std::fread(&a, sizeof a, 1, f) == 1; gl[cp] = a; }
    std::vector<uint8_t> alpha((size_t)W * H);
    ok = ok && std::fread(alpha.data(), 1, alpha.size(), f) == alpha.size();
    std::fclose(f);
    if (!ok) return false;
    std::vector<uint8_t> rgba((size_t)W * H * 4);
    for (size_t i = 0; i < alpha.size(); i++) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = alpha[i]; }
    gAlt.tex = gfx::createDynamic(rgba.data(), (int)W, (int)H, true);
    if (gAlt.tex == -1) return false;
    gAlt.ascent = asc; gAlt.lineH = lh; gAlt.g = std::move(gl); gAlt.ok = true;
    logf("[fuente] fuente del sistema desde la cache: %d glifos en %.0f ms\n", (int)gAlt.g.size(), fontNowMs() - tA);
    return true;
}
static void altToCache(int W, int H, const std::vector<uint8_t>& rgba) {
    if (gFontCachePath.empty()) return;
    FILE* f = std::fopen(gFontCachePath.c_str(), "wb");
    if (!f) return;
    uint32_t ver = 1, w = W, h = H, n = (uint32_t)gAlt.g.size();
    std::fwrite("VSFC", 1, 4, f); std::fwrite(&ver, 4, 1, f); std::fwrite(&gAlt.px, 4, 1, f); std::fwrite(&gAlt.ascent, 4, 1, f);
    std::fwrite(&gAlt.lineH, 4, 1, f); std::fwrite(&w, 4, 1, f); std::fwrite(&h, 4, 1, f); std::fwrite(&n, 4, 1, f);
    for (auto& [cp, a] : gAlt.g) { std::fwrite(&cp, 4, 1, f); std::fwrite(&a, sizeof a, 1, f); }
    std::vector<uint8_t> alpha((size_t)W * H);
    for (size_t i = 0; i < alpha.size(); i++) alpha[i] = rgba[i * 4 + 3];
    std::fwrite(alpha.data(), 1, alpha.size(), f);
    std::fclose(f);
}
static bool altBuild() {
    gAlt.tried = true;
    double tA = fontNowMs();
    if (altFromCache(tA)) return true;
    if (!gFontOpen || !gFontGlyph || !gFontOpen(gAlt.px, gAlt.ascent, gAlt.lineH) || !std::isfinite(gAlt.lineH) || gAlt.lineH <= 0) { logf("[fuente] sin fuente del sistema\n"); return false; }
    std::vector<uint32_t> set;
    for (uint32_t c = 32; c < 127; c++) set.push_back(c);
    for (uint32_t c = 160; c < 256; c++) set.push_back(c);
    for (uint32_t c : {0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2013u, 0x2014u, 0x2026u}) set.push_back(c);
    const int W = 1024; int H = 0, x = 2, y = 2, rowH = 0;
    struct Raw { uint32_t cp; std::vector<uint8_t> a; int w, h, xo, yo, ax, ay; float adv; };
    std::vector<Raw> raws;
    for (uint32_t c : set) {
        Raw r; r.cp = c;
        if (!gFontGlyph(c, r.a, r.w, r.h, r.xo, r.yo, r.adv)) continue;
        if (r.w + 4 > W) continue;
        if (x + r.w + 2 > W) { x = 2; y += rowH + 2; rowH = 0; }
        r.ax = x; r.ay = y; x += r.w + 2; rowH = std::max(rowH, r.h);
        raws.push_back(std::move(r));
    }
    H = 1; while (H < y + rowH + 2) H *= 2;
    std::vector<uint8_t> rgba((size_t)W * H * 4, 0);
    for (auto& r : raws) {
        for (int j = 0; j < r.h; j++) for (int i = 0; i < r.w; i++) {
            uint8_t* p = &rgba[(((size_t)(r.ay + j)) * W + r.ax + i) * 4];
            p[0] = p[1] = p[2] = 255; p[3] = r.a[(size_t)j * r.w + i];
        }
        gAlt.g[r.cp] = AltGlyph{(float)r.ax, (float)r.ay, (float)(r.ax + r.w), (float)(r.ay + r.h), (float)r.xo, (float)r.yo, (float)r.w, (float)r.h, r.adv};
    }
    gAlt.tex = gfx::createDynamic(rgba.data(), W, H, true);
    int inked = 0; for (auto& r : raws) for (uint8_t v : r.a) if (v) { inked++; break; }
    gAlt.ok = gAlt.tex != -1 && !gAlt.g.empty() && inked > 20;   // sin tinta (fuente rota): se queda la fuente del juego
    if (gAlt.ok) altToCache(W, H, rgba);
    logf("[fuente] fuente del sistema: %d glifos (%d con tinta) en %.0f ms, atlas %dx%d, %.0f px (ascenso %.1f, linea %.1f)%s\n", (int)gAlt.g.size(), inked, fontNowMs() - tA, W, H,
         (double)gAlt.px, (double)gAlt.ascent, (double)gAlt.lineH, gAlt.ok ? "" : " -> NO se usa (queda la fuente del juego)");
    return gAlt.ok;
}
static bool altOn() {
    if (gMods.font != 1) return false;
    if (!gAlt.tried) altBuild();
    return gAlt.ok;
}
static const AltGlyph* altGlyph(uint32_t cp) {
    auto it = gAlt.g.find(cp);
    if (it == gAlt.g.end()) { int mk; uint32_t b = baseChar(cp, mk); it = gAlt.g.find(b); if (it == gAlt.g.end()) return nullptr; }
    return &it->second;
}
static double altScale(const RFont& f) { return f.lineH * 0.82 / gAlt.lineH; }

// ---- Texto del runner: UTF-8, letras acentuadas compuestas e iconos de botones
// Las cadenas del juego son ASCII; las traducciones vienen en UTF-8. Un byte que no forma UTF-8 valido se toma como
// Latin-1. La fuente del juego no tiene a e i o u n con acento ni signos de apertura: se dibuja la letra base y el
// runner anade la marca encima (tilde, virgulilla, dieresis) o gira el glifo (los signos de apertura).
// Codigos 0x11..0x1A = iconos de botones de la Vita (debugtour.cpp cambia los nombres de boton del juego por ellos).
static void decodeUtf8(const std::string& s, std::vector<uint32_t>& out) {
    out.clear();
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n > 0 && i + n >= s.size()) break;   // letra cortada al final (efecto máquina de escribir por bytes): no se dibuja
        bool ok = n >= 0 && i + n < s.size();
        for (int k = 1; ok && k <= n; k++) ok = ((unsigned char)s[i + k] >> 6) == 2;
        if (!ok || n == 0) { out.push_back(c); i++; continue; }
        uint32_t cp = c & (0x3F >> n);
        for (int k = 1; k <= n; k++) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp); i += n + 1;
    }
}
enum { MK_NONE, MK_ACUTE, MK_GRAVE, MK_TILDE, MK_DIAER, MK_CIRC, MK_INV };
static uint32_t baseChar(uint32_t cp, int& mark) {
    mark = MK_NONE;
    switch (cp) {
        case 0xE1: mark = MK_ACUTE; return 'a'; case 0xE9: mark = MK_ACUTE; return 'e'; case 0xED: mark = MK_ACUTE; return 'i';
        case 0xF3: mark = MK_ACUTE; return 'o'; case 0xFA: mark = MK_ACUTE; return 'u';
        case 0xC1: mark = MK_ACUTE; return 'A'; case 0xC9: mark = MK_ACUTE; return 'E'; case 0xCD: mark = MK_ACUTE; return 'I';
        case 0xD3: mark = MK_ACUTE; return 'O'; case 0xDA: mark = MK_ACUTE; return 'U';
        case 0xE0: mark = MK_GRAVE; return 'a'; case 0xE8: mark = MK_GRAVE; return 'e'; case 0xF2: mark = MK_GRAVE; return 'o';
        case 0xF1: mark = MK_TILDE; return 'n'; case 0xD1: mark = MK_TILDE; return 'N';
        case 0xFC: mark = MK_DIAER; return 'u'; case 0xDC: mark = MK_DIAER; return 'U';
        case 0xBF: mark = MK_INV; return '?'; case 0xA1: mark = MK_INV; return '!';
        case 0xE7: return 'c'; case 0xC7: return 'C';
        case 0xAB: case 0xBB: case 0x201C: case 0x201D: return '"';
        case 0x2018: case 0x2019: return '\'';
        case 0x2014: case 0x2013: return '-';
        case 0x2026: return '.';
        default: return cp;
    }
}
static bool isIcon(uint32_t c) { return c >= 0x11 && c <= 0x1A; }
static const char* iconText(uint32_t c) {
    switch (c) { case 0x15: return "L"; case 0x16: return "R"; case 0x17: return "L1"; case 0x18: return "R1";
                 case 0x19: return "START"; case 0x1A: return "SELECT"; default: return ""; }
}
static double asciiWidth(const RFont& f, const char* t) {
    double w = 0; for (; *t; t++) { const RGlyph* g = glyphFor(f, (unsigned char)*t); w += g ? g->w + f.sep : f.spaceW; } return w;
}
static double iconPillTextScale(uint32_t c) { return c >= 0x19 ? 0.5 : 0.72; }
static double iconWidth(const RFont& f, uint32_t c) {
    double L = f.lineH;
    if (c <= 0x14) return L * 0.92;
    return asciiWidth(f, iconText(c)) * iconPillTextScale(c) + L * 0.5;
}
static double charAdvance(const RFont& f, uint32_t cp) {
    if (isIcon(cp)) return iconWidth(f, cp) + f.sep;
    if (altOn()) { const AltGlyph* a = altGlyph(cp); double k = altScale(f); return a ? a->adv * k : f.spaceW; }
    int mk; uint32_t b = baseChar(cp, mk);
    const RGlyph* g = glyphFor(f, b);
    return g ? g->w + f.sep : f.spaceW;
}
static double textLineWidth(const RFont& f, const std::string& s, size_t a, size_t b) {
    static std::vector<uint32_t> cps;
    decodeUtf8(s.substr(a, b - a), cps);
    double w = 0;
    for (uint32_t cp : cps) w += charAdvance(f, cp);
    return w;
}

// separa en lineas: '#' es salto (salvo "\#"), wrap por ancho w (si > 0)
static void splitLines(const RFont& f, const std::string& in, double maxW, std::vector<std::string>& out) {
    std::string cur;
    std::vector<std::string> hard;
    for (size_t i = 0; i < in.size(); i++) {
        char ch = in[i];
        if (ch == '\\' && i + 1 < in.size() && in[i + 1] == '#') { cur += '#'; i++; continue; }
        if (ch == '#' || ch == '\n') { hard.push_back(cur); cur.clear(); continue; }
        if (ch == '\r') continue;
        cur += ch;
    }
    hard.push_back(cur);
    for (auto& line : hard) {
        if (maxW <= 0) { out.push_back(line); continue; }
        std::string acc; size_t pos = 0;
        while (pos <= line.size()) {
            size_t sp = line.find(' ', pos);
            std::string word = line.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos);
            std::string test = acc.empty() ? word : acc + " " + word;
            if (!acc.empty() && textLineWidth(f, test, 0, test.size()) > maxW) { out.push_back(acc); acc = word; }
            else acc = test;
            if (sp == std::string::npos) break;
            pos = sp + 1;
        }
        out.push_back(acc);
    }
}

const std::string* langDrawAlias(const std::string& s);   // lang.cpp
void Game::drawText(double x, double y, const std::string& s0, double xs, double ys, double ang, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, double alpha, double sep, double w) {
    if (fontCur < 0 || fontCur >= (int)gFonts.size()) return;
    gfx::hudAnchor((float)x, (float)y);   // un texto se escala entero hacia la misma ancla (no se parte)
    struct HudLock { HudLock() { gfx::hudLock(true); } ~HudLock() { gfx::hudLock(false); } } hudLockGuard;
    const std::string* al = langDrawAlias(s0);
    const std::string& s = al ? *al : s0;
    const RFont& f = gFonts[fontCur];
    std::vector<std::string> lines;
    splitLines(f, s, w, lines);
    double lh = sep > 0 ? sep : f.lineH;
    double totalH = lh * lines.size();
    double maxW = 0; for (auto& l : lines) maxW = std::max(maxW, textLineWidth(f, l, 0, l.size()));
    double yoff = drawValign == 1 ? -totalH / 2 : drawValign == 2 ? -totalH : 0;
    double a = ang * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
    auto lerpc = [](uint32_t p, uint32_t q, double t) {
        auto ch = [&](int sh) { return (uint32_t)std::lround(((p >> sh) & 255) * (1 - t) + ((q >> sh) & 255) * t) << sh; };
        return ch(0) | ch(8) | ch(16);
    };
    auto P = [&](double lx, double ly, float& X, float& Y) {
        double px = lx * xs, py = ly * ys;
        X = (float)(x + px * ca + py * sa); Y = (float)(y - px * sa + py * ca);
    };
    // poligono solido de 4 vertices en coordenadas locales del texto (un triangulo repite el ultimo vertice)
    auto solid = [&](double ax, double ay, double bx, double by, double cx_, double cy_, double dx, double dy, uint32_t col, double al) {
        float xy[8]; P(ax, ay, xy[0], xy[1]); P(bx, by, xy[2], xy[3]); P(cx_, cy_, xy[4], xy[5]); P(dx, dy, xy[6], xy[7]);
        float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0}; uint32_t cc[4] = {col, col, col, col};
        gfx::quad(-1, xy, uv, cc, (float)al);
    };
    auto bar = [&](double ax, double ay, double bx, double by, double t, uint32_t col, double al) {   // segmento grueso
        double dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy); if (len <= 0) return;
        double nx = -dy / len * t * 0.5, ny = dx / len * t * 0.5;
        solid(ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny, col, al);
    };
    auto disc = [&](double cx_, double cy_, double r, uint32_t col, double al) {
        const int N = 16;
        for (int k = 0; k < N; k += 2) {
            double a0 = k * 2 * M_PI / N, a1 = (k + 1) * 2 * M_PI / N, a2 = (k + 2) * 2 * M_PI / N;
            solid(cx_, cy_, cx_ + r * std::cos(a0), cy_ + r * std::sin(a0), cx_ + r * std::cos(a1), cy_ + r * std::sin(a1),
                  cx_ + r * std::cos(a2), cy_ + r * std::sin(a2), col, al);
        }
    };
    auto ring = [&](double cx_, double cy_, double r, double t, uint32_t col, double al) {
        const int N = 20; double r0 = r - t * 0.5, r1 = r + t * 0.5;
        for (int k = 0; k < N; k++) {
            double a0 = k * 2 * M_PI / N, a1 = (k + 1) * 2 * M_PI / N;
            solid(cx_ + r0 * std::cos(a0), cy_ + r0 * std::sin(a0), cx_ + r1 * std::cos(a0), cy_ + r1 * std::sin(a0),
                  cx_ + r1 * std::cos(a1), cy_ + r1 * std::sin(a1), cx_ + r0 * std::cos(a1), cy_ + r0 * std::sin(a1), col, al);
        }
    };
    auto glyphQuad = [&](const RGlyph* g, double cx, double cy, const uint32_t cc[4], bool rot180) {
        const SFrame& fr = g->f;
        double lx0 = cx + (fr.tx - g->offset), ly0 = cy + fr.ty, lx1 = lx0 + fr.sw, ly1 = ly0 + fr.sh;
        float xy[8]; P(lx0, ly0, xy[0], xy[1]); P(lx1, ly0, xy[2], xy[3]); P(lx1, ly1, xy[4], xy[5]); P(lx0, ly1, xy[6], xy[7]);
        float u0 = (float)fr.sx, v0 = (float)fr.sy, u1 = (float)(fr.sx + fr.sw), v1 = (float)(fr.sy + fr.sh);
        float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
        if (rot180) { float r[8] = {u1, v1, u0, v1, u0, v0, u1, v0}; std::memcpy(uv, r, sizeof uv); }
        gfx::quad(fr.tex, xy, uv, cc, (float)alpha);
    };
    std::vector<uint32_t> cps;
    for (size_t li = 0; li < lines.size(); li++) {
        const std::string& l = lines[li];
        double lw = textLineWidth(f, l, 0, l.size());
        double cx = drawHalign == 1 ? -lw / 2 : drawHalign == 2 ? -lw : 0;
        double cy = yoff + li * lh;
        decodeUtf8(l, cps);
        for (uint32_t cp : cps) {
            double adv = charAdvance(f, cp);
            // colores por posicion (degradado en la caja del texto)
            double tx0 = maxW > 0 ? (cx - (drawHalign == 1 ? -maxW / 2 : drawHalign == 2 ? -maxW : 0)) / maxW : 0;
            double tx1 = maxW > 0 ? tx0 + (adv - f.sep) / maxW : 0;
            double ty0 = totalH > 0 ? (cy - yoff) / totalH : 0, ty1 = totalH > 0 ? (cy - yoff + lh) / totalH : 1;
            uint32_t top0 = lerpc(c1, c2, tx0), top1 = lerpc(c1, c2, tx1), bot0 = lerpc(c4, c3, tx0), bot1 = lerpc(c4, c3, tx1);
            uint32_t cc[4] = {lerpc(top0, bot0, ty0), lerpc(top1, bot1, ty0), lerpc(top1, bot1, ty1), lerpc(top0, bot0, ty1)};
            double L = f.lineH;
            if (isIcon(cp)) {   // iconos de botones de la Vita, dibujados por el runner (sin arte del juego)
                double iw = adv - f.sep, mx = cx + iw / 2, my = cy + L * 0.5, r = L * 0.42;
                if (cp <= 0x14) {
                    disc(mx, my, r, 0x262626, alpha * 0.9);
                    double t = L * 0.085, k = r * 0.5;
                    if (cp == 0x11) { bar(mx - k, my - k, mx + k, my + k, t, 0xE8B27C, alpha); bar(mx - k, my + k, mx + k, my - k, t, 0xE8B27C, alpha); }
                    else if (cp == 0x12) ring(mx, my, r * 0.52, t, 0x6E6EFF, alpha);
                    else if (cp == 0x13) { double q = r * 0.48; bar(mx - q, my - q, mx + q, my - q, t, 0xD07EE6, alpha); bar(mx + q, my - q, mx + q, my + q, t, 0xD07EE6, alpha);
                                           bar(mx + q, my + q, mx - q, my + q, t, 0xD07EE6, alpha); bar(mx - q, my + q, mx - q, my - q, t, 0xD07EE6, alpha); }
                    else { double q = r * 0.58; double ax = mx, ay = my - q, bx = mx + q * 0.87, by = my + q * 0.5, dx = mx - q * 0.87, dy = my + q * 0.5;
                           bar(ax, ay, bx, by, t, 0x9EE04A, alpha); bar(bx, by, dx, dy, t, 0x9EE04A, alpha); bar(dx, dy, ax, ay, t, 0x9EE04A, alpha); }
                } else {   // pastilla con el nombre (L, R, L1, R1, START, SELECT)
                    double h = L * 0.7, y0 = my - h / 2, y1 = my + h / 2, x0 = cx + L * 0.06, x1 = cx + iw - L * 0.06, rr = h / 2;
                    solid(x0 + rr, y0, x1 - rr, y0, x1 - rr, y1, x0 + rr, y1, 0x3A3A3A, alpha * 0.92);
                    disc(x0 + rr, my, rr, 0x3A3A3A, alpha * 0.92); disc(x1 - rr, my, rr, 0x3A3A3A, alpha * 0.92);
                    double ts = iconPillTextScale(cp), tw = asciiWidth(f, iconText(cp)) * ts, tx = mx - tw / 2;
                    for (const char* t = iconText(cp); *t; t++) {
                        const RGlyph* g = glyphFor(f, (unsigned char)*t);
                        if (!g || !g->valid) { tx += f.spaceW * ts; continue; }
                        const SFrame& fr = g->f;
                        double gx0 = tx + (fr.tx - g->offset) * ts, gy0 = my - L * ts * 0.5 + fr.ty * ts;
                        float xy[8]; P(gx0, gy0, xy[0], xy[1]); P(gx0 + fr.sw * ts, gy0, xy[2], xy[3]); P(gx0 + fr.sw * ts, gy0 + fr.sh * ts, xy[4], xy[5]); P(gx0, gy0 + fr.sh * ts, xy[6], xy[7]);
                        float uv[8] = {(float)fr.sx, (float)fr.sy, (float)(fr.sx + fr.sw), (float)fr.sy, (float)(fr.sx + fr.sw), (float)(fr.sy + fr.sh), (float)fr.sx, (float)(fr.sy + fr.sh)};
                        uint32_t wc[4] = {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF};
                        gfx::quad(fr.tex, xy, uv, wc, (float)alpha);
                        tx += (g->w + f.sep) * ts;
                    }
                }
                cx += adv; continue;
            }
            if (altOn()) {   // fuente del sistema: la linea base cae donde la de la fuente del juego (~0,8 de la celda)
                const AltGlyph* ag = altGlyph(cp);
                if (ag && ag->w > 0) {
                    double k = altScale(f), base = cy + L * 0.80;
                    double lx0 = cx + ag->xoff * k, ly0 = base + ag->yoff * k, lx1 = lx0 + ag->w * k, ly1 = ly0 + ag->h * k;
                    float xy[8]; P(lx0, ly0, xy[0], xy[1]); P(lx1, ly0, xy[2], xy[3]); P(lx1, ly1, xy[4], xy[5]); P(lx0, ly1, xy[6], xy[7]);
                    float uv[8] = {ag->u0, ag->v0, ag->u1, ag->v0, ag->u1, ag->v1, ag->u0, ag->v1};
                    gfx::quad(gAlt.tex, xy, uv, cc, (float)alpha);
                }
                cx += adv; continue;
            }
            int mk; uint32_t b = baseChar(cp, mk);
            const RGlyph* g = glyphFor(f, b);
            if (!g || !g->valid) { cx += adv; continue; }
            glyphQuad(g, cx, cy, cc, mk == MK_INV);
            if (mk != MK_NONE && mk != MK_INV) {   // marca encima de la letra, del color de la parte de arriba del texto
                // los glifos de la fuente ocupan la celda entera: la altura de la marca va por proporciones de la celda
                // (mayusculas: arriba del todo; minusculas: altura de la x a ~1/4 de la celda)
                const SFrame& fr = g->f; (void)fr;
                bool upper = b >= 'A' && b <= 'Z';
                double gmx = cx + g->w * 0.5, mh = L * 0.15, top = upper ? cy - L * 0.01 : cy + L * 0.25;
                double t = std::max(1.2, L * 0.06), gap = L * 0.03;
                uint32_t col = cc[0];
                if (mk == MK_ACUTE) bar(gmx - L * 0.05, top - gap, gmx + L * 0.07, top - gap - mh, t, col, alpha);
                else if (mk == MK_GRAVE) bar(gmx + L * 0.05, top - gap, gmx - L * 0.07, top - gap - mh, t, col, alpha);
                else if (mk == MK_DIAER) { double d = t * 1.1; solid(gmx - L * 0.1, top - gap - d, gmx - L * 0.1 + d, top - gap - d, gmx - L * 0.1 + d, top - gap, gmx - L * 0.1, top - gap, col, alpha);
                                           solid(gmx + L * 0.1 - d, top - gap - d, gmx + L * 0.1, top - gap - d, gmx + L * 0.1, top - gap, gmx + L * 0.1 - d, top - gap, col, alpha); }
                else if (mk == MK_TILDE) { double w2 = std::max(g->w * 0.3, L * 0.1), yb = top - gap - mh * 0.3, yt = top - gap - mh;
                                           bar(gmx - w2, yb, gmx - w2 * 0.35, yt, t, col, alpha); bar(gmx - w2 * 0.35, yt, gmx + w2 * 0.35, yb, t, col, alpha);
                                           bar(gmx + w2 * 0.35, yb, gmx + w2, yt, t, col, alpha); }
            }
            cx += adv;
        }
    }
}

VS_FN(font_add_sprite) {
    int spr = a[0].ival(); RFont f; f.first = a[1].ival(); f.prop = a[2].truthy(); f.sep = a[3].num(); f.spr = spr;
    if (spr >= 0 && spr < (int)g.sprites.size()) {
        const RSprite& s = g.sprites[spr];
        f.lineH = s.h; f.spaceW = std::max(1.0, s.w / 3.0);
        for (auto& fr : s.frames) {
            RGlyph gl; gl.f = fr; gl.valid = fr.sw > 0 && fr.sh > 0;
            if (f.prop) { gl.w = gl.valid ? fr.sw : f.spaceW; gl.offset = fr.tx; }
            else { gl.w = s.w; gl.offset = 0; }
            f.glyphs.push_back(gl);
        }
    }
    gFonts.push_back(f);
    return Value((double)(1000 + gFonts.size() - 1));
}

VS_FN(draw_set_font) { int id = a[0].ival(); g.fontCur = id >= 1000 ? id - 1000 : -1; return Value(); }
VS_FN(draw_set_halign) { g.drawHalign = a[0].ival(); return Value(); }

VS_FN(draw_text_color) {
    g.drawText(a[0].num(), a[1].num(), a[2].toString(), 1, 1, 0, bgr(a[3]), bgr(a[4]), bgr(a[5]), bgr(a[6]), a[7].num(), -1, -1);
    return Value();
}
VS_FN(draw_text_transformed_color) {
    g.drawText(a[0].num(), a[1].num(), a[2].toString(), a[3].num(), a[4].num(), a[5].num(), bgr(a[6]), bgr(a[7]), bgr(a[8]), bgr(a[9]), a[10].num(), -1, -1);
    return Value();
}
VS_FN(draw_text_ext_transformed_color) {
    g.drawText(a[0].num(), a[1].num(), a[2].toString(), a[5].num(), a[6].num(), a[7].num(), bgr(a[8]), bgr(a[9]), bgr(a[10]), bgr(a[11]), a[12].num(), a[3].num(), a[4].num());
    return Value();
}

// ------------------------------------------------------------------ sprites
VS_FN(draw_self) { if (c.self) g.drawInstanceDefault(c.self); return Value(); }
VS_FN(draw_sprite) {
    int spr = a[0].ival(); double sub = a[1].num(); if (sub < 0 && c.self) sub = c.self->image_index;
    g.drawSpriteFrame(spr, sub, a[2].num(), a[3].num(), 1, 1, 0, 0xFFFFFF, 1); return Value();
}
VS_FN(draw_sprite_ext) {
    int spr = a[0].ival(); double sub = a[1].num(); if (sub < 0 && c.self) sub = c.self->image_index;
    g.drawSpriteFrame(spr, sub, a[2].num(), a[3].num(), a[4].num(), a[5].num(), a[6].num(), bgr(a[7]), a[8].num()); return Value();
}
VS_FN(draw_sprite_part_ext) {
    int spr = a[0].ival(); if (spr < 0 || spr >= (int)g.sprites.size() || g.sprites[spr].frames.empty()) return Value();
    double sub = a[1].num(); if (sub < 0 && c.self) sub = c.self->image_index;
    auto& s = g.sprites[spr]; int nf = (int)s.frames.size(); int fi = ((int)std::floor(sub) % nf + nf) % nf;
    g.drawFramePart(s.frames[fi], a[2].num(), a[3].num(), a[4].num(), a[5].num(), a[6].num(), a[7].num(), a[8].num(), a[9].num(), bgr(a[10]), a[11].num());
    return Value();
}
VS_FN(sprite_get_texture) {
    int spr = a[0].ival(); if (spr < 0 || spr >= (int)g.sprites.size() || g.sprites[spr].frames.empty()) return Value(-1.0);
    int nf = (int)g.sprites[spr].frames.size(); int fi = ((a[1].ival() % nf) + nf) % nf;
    return Value((double)(spr * 1024 + fi)); // handle: sprite*1024+frame
}
VS_FN(background_get_texture) { return Value((double)(-(a[0].ival() + 1) * 1024)); } // handle negativo = background

// ------------------------------------------------------------------ backgrounds
static const SFrame* bgFrame(Game& g, int b) { if (b < 0 || b >= (int)g.backgrounds.size() || !g.backgrounds[b].valid) return nullptr; return &g.backgrounds[b].f; }

VS_FN(draw_background) {
    if (auto* f = bgFrame(g, a[0].ival())) { uint32_t cc[4] = {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF}; frameQuad(*f, 0, 0, a[1].num(), a[2].num(), 1, 1, 0, cc, 1); }
    return Value();
}
VS_FN(draw_background_ext) {
    if (auto* f = bgFrame(g, a[0].ival())) { uint32_t col = bgr(a[6]); uint32_t cc[4] = {col, col, col, col}; frameQuad(*f, 0, 0, a[1].num(), a[2].num(), a[3].num(), a[4].num(), a[5].num(), cc, a[7].num()); }
    return Value();
}
VS_FN(draw_background_part) {
    if (auto* f = bgFrame(g, a[0].ival())) g.drawFramePart(*f, a[1].num(), a[2].num(), a[3].num(), a[4].num(), a[5].num(), a[6].num(), 1, 1, 0xFFFFFF, 1);
    return Value();
}
int gDrawDebug = 0;   // save/drawdebug: anota las primeras llamadas a draw_background_part_ext (diagnostico)
VS_FN(draw_background_part_ext) {
    if (gDrawDebug > 0) {
        gDrawDebug--;
        auto* f = bgFrame(g, a[0].ival());
        logf("[dibujo] sala %s bg %d pagina %d estado %d parte %.0f,%.0f %.0fx%.0f en %.0f,%.0f escala %.2f,%.2f alfa %.2f%s",
             g.d.rooms[g.room].name.c_str(), a[0].ival(), f ? f->tex : -9, f ? gfx::pageState(f->tex) : -9, a[1].num(), a[2].num(), a[3].num(), a[4].num(),
             a[5].num(), a[6].num(), a[7].num(), a[8].num(), a[10].num(), "\n");
    }
    if (auto* f = bgFrame(g, a[0].ival())) g.drawFramePart(*f, a[1].num(), a[2].num(), a[3].num(), a[4].num(), a[5].num(), a[6].num(), a[7].num(), a[8].num(), bgr(a[9]), a[10].num());
    return Value();
}

static void drawTiled(Game& g, const SFrame& f, int w, int h, double x, double y, double xs, double ys, bool ht, bool vt, uint32_t col, double alpha) {
    double tw = w * xs, th = h * ys;
    if (tw <= 0.5 || th <= 0.5) return;
    double vx = 0, vy = 0, vw = g.roomW, vh = g.roomH;
    if (g.viewsEnabled) { auto& v = g.views[g.viewCurrent]; vx = v.xview; vy = v.yview; vw = v.wview; vh = v.hview; }
    double sx = x, sy = y;
    if (ht) { sx = x - std::ceil((x - vx) / tw) * tw; }
    if (vt) { sy = y - std::ceil((y - vy) / th) * th; }
    uint32_t cc[4] = {col, col, col, col};
    for (double yy = sy; yy < vy + vh; yy += th) {
        for (double xx = sx; xx < vx + vw; xx += tw) {
            frameQuad(f, 0, 0, xx, yy, xs, ys, 0, cc, alpha);
            if (!ht) break;
        }
        if (!vt) break;
    }
}

VS_FN(draw_background_tiled_ext) {
    int b = a[0].ival(); auto* f = bgFrame(g, b); if (!f) return Value();
    drawTiled(g, *f, g.backgrounds[b].w, g.backgrounds[b].h, a[1].num(), a[2].num(), a[3].num(), a[4].num(), true, true, bgr(a[5]), a[6].num());
    return Value();
}

// ------------------------------------------------------------------ primitivas y formas
VS_FN(draw_primitive_begin_texture) { g.primKind = a[0].ival(); g.prim.clear(); g.primTex = a[1].ival(); return Value(); }
VS_FN(draw_vertex_texture_color) {
    g.prim.push_back(Game::PrimVert{(float)a[0].num(), (float)a[1].num(), (float)a[2].num(), (float)a[3].num(), bgr(a[4]), (float)a[5].num()});
    return Value();
}
VS_FN(draw_primitive_end) {
    // resolver textura: sprite*1024+frame o -(bg+1)*1024
    SFrame f; bool ok = false;
    int h = g.primTex;
    if (h >= 0) { int spr = h / 1024, fi = h % 1024; if (spr < (int)g.sprites.size() && fi < (int)g.sprites[spr].frames.size()) { f = g.sprites[spr].frames[fi]; ok = true; } }
    else if (h <= -1024) { int b = -h / 1024 - 1; if (auto* bf = bgFrame(g, b)) { f = *bf; ok = true; } }
    // reutilizados entre llamadas (el agua de sewer_11 hace ~50 primitivas por frame: 4 reservas de memoria cada una)
    static std::vector<float> xy, uv; static std::vector<uint32_t> col; static std::vector<float> al;
    xy.clear(); uv.clear(); col.clear(); al.clear();
    auto emit = [&](const Game::PrimVert& v) {
        xy.push_back(v.x); xy.push_back(v.y);
        // uv 0..1 sobre el bounding box del sprite -> pixeles del atlas
        double bw = ok ? (f.bw ? f.bw : f.sw) : 1, bh = ok ? (f.bh ? f.bh : f.sh) : 1;
        double px = v.u * bw - f.tx, py = v.v * bh - f.ty;
        px = std::max(0.0, std::min((double)f.sw, px)); py = std::max(0.0, std::min((double)f.sh, py));
        uv.push_back((float)(f.sx + px)); uv.push_back((float)(f.sy + py));
        col.push_back(v.col); al.push_back(v.a);
    };
    auto& P = g.prim;
    if (g.primKind == 5) { for (size_t i = 0; i + 2 < P.size(); i++) { if (i & 1) { emit(P[i + 1]); emit(P[i]); emit(P[i + 2]); } else { emit(P[i]); emit(P[i + 1]); emit(P[i + 2]); } } } // pr_trianglestrip
    else if (g.primKind == 6) { for (size_t i = 1; i + 1 < P.size(); i++) { emit(P[0]); emit(P[i]); emit(P[i + 1]); } } // pr_trianglefan
    else if (g.primKind == 4) { for (size_t i = 0; i + 2 < P.size(); i += 3) { emit(P[i]); emit(P[i + 1]); emit(P[i + 2]); } } // pr_trianglelist
    else g.warnOnce("primitiva no soportada " + std::to_string(g.primKind));
    if (!xy.empty()) gfx::hudAnchor(xy[0], xy[1]);
    if (!col.empty()) gfx::tris(ok ? f.tex : -1, (int)col.size(), xy.data(), uv.data(), col.data(), al.data());
    P.clear();
    return Value();
}

VS_FN(draw_rectangle_color) {
    float x1 = (float)a[0].num(), y1 = (float)a[1].num(), x2 = (float)a[2].num(), y2 = (float)a[3].num();
    uint32_t c1 = bgr(a[4]), c2 = bgr(a[5]), c3 = bgr(a[6]), c4 = bgr(a[7]);
    gfx::hudAnchor(x1, y1);
    if (a[8].truthy()) {
        gfx::line(x1, y1, x2, y1, c1, c2, 1, 1); gfx::line(x2, y1, x2, y2, c2, c3, 1, 1);
        gfx::line(x2, y2, x1, y2, c3, c4, 1, 1); gfx::line(x1, y2, x1, y1, c4, c1, 1, 1);
    } else {
        float xy[8] = {x1, y1, x2 + 1, y1, x2 + 1, y2 + 1, x1, y2 + 1};
        float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        uint32_t cc[4] = {c1, c2, c3, c4};
        gfx::quad(-1, xy, uv, cc, 1);
    }
    return Value();
}
VS_FN(draw_line_color) { gfx::hudAnchor((float)a[0].num(), (float)a[1].num()); gfx::line((float)a[0].num(), (float)a[1].num(), (float)a[2].num(), (float)a[3].num(), bgr(a[4]), bgr(a[5]), 1, 1); return Value(); }

VS_FN(draw_set_blend_mode) { g.blendMode = a[0].ival(); gfx::setBlend(g.blendMode); return Value(); }
VS_FN(draw_set_blend_mode_ext) { gfx::setBlendExt(a[0].ival(), a[1].ival()); return Value(); }
VS_FN(draw_texture_flush) { gfx::flush(); return Value(); }
VS_FN(draw_enable_drawevent) { g.drawEventsEnabled = a[0].truthy(); return Value(); }
VS_FN(texture_set_interpolation) { g.texInterp = a[0].truthy(); gfx::setFilter(g.texInterp); return Value(); }
VS_FN(texture_set_repeat) { g.texRepeat = a[0].truthy(); return Value(); }
VS_FN(screen_save) { g.warnOnce("screen_save ignorado"); return Value(); }

// ------------------------------------------------------------------ escena
void Game::draw() {
    if (gPreDrawHook) gPreDrawHook(*this);
    gfx::beginFrame();
    gfx::setBlend(0); blendMode = 0;
    struct Item { double depth; int kind; void* p; uint64_t order; };
    static std::vector<Item> items;   // reutilizado entre frames (~450 elementos en sewer_11)
    items.clear();
    for (auto& p : insts) if (p->alive && p->visible) items.push_back({p->depth, 0, p.get(), p->createSerial});
    // tiles agrupados por profundidad. El agrupado se reutiliza entre frames: solo se rehace si cambia la firma
    // (cuantos hay, profundidades y visibilidad). Antes se reconstruia un std::map con ~1500 tiles en cada frame.
    {
        uint64_t sig = tiles.size() * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uintptr_t)tiles.data();
        for (auto& t : tiles) sig = (sig ^ ((uint64_t)(uint32_t)t.depth << 1 | (t.visible ? 1 : 0))) * 0x100000001B3ull;
        if (sig != tileSig) {
            tileSig = sig;
            std::map<int, std::vector<int>, std::greater<int>> byDepth;
            for (size_t k = 0; k < tiles.size(); k++) if (tiles[k].visible) byDepth[tiles[k].depth].push_back((int)k);
            tileGroups.assign(byDepth.begin(), byDepth.end());
        }
    }
    for (auto& g : tileGroups) items.push_back({(double)g.first, 1, &g.second, 0});
    for (size_t k = 0; k < partSystems.size(); k++) { double dep; if (particleAutoDraw(k, dep)) items.push_back({dep, 2, (void*)(uintptr_t)k, 0}); }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });

    auto drawBgs = [&](bool fore) {
        for (auto& b : bgs) {
            if (!b.visible || b.foreground != fore || b.index < 0 || b.index >= (int)backgrounds.size()) continue;
            const RBackground& rb = backgrounds[b.index];
            if (b.stretch) { uint32_t cc[4] = {b.blend, b.blend, b.blend, b.blend}; frameQuad(rb.f, 0, 0, 0, 0, rb.w ? (double)roomW / rb.w : 1, rb.h ? (double)roomH / rb.h : 1, 0, cc, b.alpha); }
            else drawTiled(*this, rb.f, rb.w, rb.h, b.x, b.y, b.xscale, b.yscale, b.htiled, b.vtiled, b.blend, b.alpha);
        }
    };
    bool cull = false; double cl = 0, ct = 0, cr = 0, cb = 0;   // rectangulo visible de la vista actual
    auto drawScene = [&]() {
        if (drawBgColor) gfx::clear(bgColor);
        drawBgs(false);
        for (auto& it : items) {
            if (it.kind == 0) {
                Instance* i = (Instance*)it.p;
                if (!i->alive || !i->visible) continue;
                if (drawEventsEnabled && findEvent(i->obj, EV_DRAW, 0).code >= 0) fireEvent(i, EV_DRAW, 0);
                else drawInstanceDefault(i);
            } else if (it.kind == 1) {
                for (int k : *(std::vector<int>*)it.p) {
                    Tile* t = &tiles[k];
                    if (t->bg < 0 || t->bg >= (int)backgrounds.size()) continue;
                    // fuera de la vista: no se dibuja (no tiene efectos secundarios)
                    if (cull) {
                        double x0 = t->x, x1 = t->x + t->w * t->xscale, y0 = t->y, y1 = t->y + t->h * t->yscale;
                        if (x0 > x1) std::swap(x0, x1);
                        if (y0 > y1) std::swap(y0, y1);
                        if (x1 < cl || x0 > cr || y1 < ct || y0 > cb) continue;
                    }
                    drawFramePart(backgrounds[t->bg].f, t->sx, t->sy, t->w, t->h, t->x, t->y, t->xscale, t->yscale, t->blend, t->alpha);
                }
            } else {
                drawParticleSystem((size_t)(uintptr_t)it.p);
            }
        }
        drawBgs(true);
        if (gViewDrawHook && viewCurrent == 0) gViewDrawHook(*this);   // capa del runner dentro de la vista del juego
    };

    if (viewsEnabled) {
        for (int v = 0; v < 8; v++) {
            auto& vw = views[v];
            if (!vw.visible) continue;
            viewCurrent = v;
            gfx::setCamera(vw.xview, vw.yview, vw.wview, vw.hview, vw.xport, vw.yport, vw.wport, vw.hport, vw.angle);
            cull = vw.angle == 0; cl = vw.xview - 2; ct = vw.yview - 2; cr = vw.xview + vw.wview + 2; cb = vw.yview + vw.hview + 2;
            drawScene();
            cull = false;
        }
        viewCurrent = 0;
    } else {
        gfx::setCamera(0, 0, roomW, roomH, 0, 0, gWinW, gWinH, 0);
        drawScene();
    }
    if (gOverlayHook) gOverlayHook(*this);
    gfx::endFrame();
}

// ---- capa propia del runner por encima del juego (menu de trucos): coordenadas de ventana, fuente del juego
void (*gOverlayHook)(Game&) = nullptr;
void (*gPreDrawHook)(Game&) = nullptr;
void (*gViewDrawHook)(Game&) = nullptr;
void overlayBegin(Game& g) { (void)g; gfx::setCamera(0, 0, gWinW, gWinH, 0, 0, gWinW, gWinH, 0); gfx::setBlend(0); }
void overlaySize(int& w, int& h) { w = gWinW; h = gWinH; }
void overlayRect(float x1, float y1, float x2, float y2, uint32_t bgr, float alpha) {
    float xy[8] = {x1, y1, x2, y1, x2, y2, x1, y2}; float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t cc[4] = {bgr, bgr, bgr, bgr};
    gfx::quad(-1, xy, uv, cc, alpha);
}
void overlayText(Game& g, double x, double y, const std::string& s, double scale, uint32_t bgr, double alpha) {
    if (gFonts.empty()) return;
    int f = g.fontCur, ha = g.drawHalign, va = g.drawValign;
    g.fontCur = 0; g.drawHalign = 0; g.drawValign = 0;
    g.drawText(x, y, s, scale, scale, 0, bgr, bgr, bgr, bgr, alpha, -1, -1);
    g.fontCur = f; g.drawHalign = ha; g.drawValign = va;
}

} // namespace vs
