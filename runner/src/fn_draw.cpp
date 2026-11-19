// Dibujo: escena (vistas, fondos, tiles, instancias), sprites, texto (font_add_sprite) y primitivas.
#include "runtime.h"
#include "gfx.h"
#include <algorithm>
#include <cstring>

namespace vs {

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

static double textLineWidth(const RFont& f, const std::string& s, size_t a, size_t b) {
    double w = 0;
    for (size_t i = a; i < b; i++) {
        const RGlyph* g = glyphFor(f, (unsigned char)s[i]);
        if (!g) { w += f.spaceW; continue; }
        w += g->w + f.sep;
    }
    return w;
}

// separa en lineas: '#' es salto (salvo "\#"), wrap por ancho w (si > 0)
static void splitLines(const RFont& f, const std::string& in, double maxW, std::vector<std::string>& out) {
    std::string cur;
    std::vector<std::string> hard;
    for (size_t i = 0; i < in.size(); i++) {
        char ch = in[i];
        if (ch == '\\' && i + 1 < in.size() && in[i + 1] == '#') { cur += '#'; i++; continue; }
        if (ch == '#' || ch == '\n') { hard.push_back(cur); cur.clear(); if (ch == '\n' && false) {} continue; }
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

void Game::drawText(double x, double y, const std::string& s, double xs, double ys, double ang, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, double alpha, double sep, double w) {
    if (fontCur < 0 || fontCur >= (int)gFonts.size()) return;
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
    for (size_t li = 0; li < lines.size(); li++) {
        const std::string& l = lines[li];
        double lw = textLineWidth(f, l, 0, l.size());
        double cx = drawHalign == 1 ? -lw / 2 : drawHalign == 2 ? -lw : 0;
        double cy = yoff + li * lh;
        for (unsigned char ch : l) {
            const RGlyph* g = glyphFor(f, ch);
            if (!g || !g->valid) { cx += g ? g->w + f.sep : f.spaceW; continue; }
            // colores por posicion (degradado en la caja del texto)
            double tx0 = maxW > 0 ? (cx - (drawHalign == 1 ? -maxW / 2 : drawHalign == 2 ? -maxW : 0)) / maxW : 0;
            double tx1 = maxW > 0 ? tx0 + g->w / maxW : 0;
            double ty0 = totalH > 0 ? (cy - yoff) / totalH : 0, ty1 = totalH > 0 ? (cy - yoff + lh) / totalH : 1;
            uint32_t top0 = lerpc(c1, c2, tx0), top1 = lerpc(c1, c2, tx1), bot0 = lerpc(c4, c3, tx0), bot1 = lerpc(c4, c3, tx1);
            uint32_t cc[4] = {lerpc(top0, bot0, ty0), lerpc(top1, bot1, ty0), lerpc(top1, bot1, ty1), lerpc(top0, bot0, ty1)};
            // el glifo se dibuja con su frame (offset tx propio ya incluido en g->offset)
            const SFrame& fr = g->f;
            double lx0 = cx + (fr.tx - g->offset), ly0 = cy + fr.ty, lx1 = lx0 + fr.sw, ly1 = ly0 + fr.sh;
            auto P = [&](double lx, double ly, float& X, float& Y) {
                double px = lx * xs, py = ly * ys;
                X = (float)(x + px * ca + py * sa); Y = (float)(y - px * sa + py * ca);
            };
            float xy[8]; P(lx0, ly0, xy[0], xy[1]); P(lx1, ly0, xy[2], xy[3]); P(lx1, ly1, xy[4], xy[5]); P(lx0, ly1, xy[6], xy[7]);
            float uv[8] = {(float)fr.sx, (float)fr.sy, (float)(fr.sx + fr.sw), (float)fr.sy, (float)(fr.sx + fr.sw), (float)(fr.sy + fr.sh), (float)fr.sx, (float)(fr.sy + fr.sh)};
            gfx::quad(fr.tex, xy, uv, cc, (float)alpha);
            cx += g->w + f.sep;
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
    if (!col.empty()) gfx::tris(ok ? f.tex : -1, (int)col.size(), xy.data(), uv.data(), col.data(), al.data());
    P.clear();
    return Value();
}

VS_FN(draw_rectangle_color) {
    float x1 = (float)a[0].num(), y1 = (float)a[1].num(), x2 = (float)a[2].num(), y2 = (float)a[3].num();
    uint32_t c1 = bgr(a[4]), c2 = bgr(a[5]), c3 = bgr(a[6]), c4 = bgr(a[7]);
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
VS_FN(draw_line_color) { gfx::line((float)a[0].num(), (float)a[1].num(), (float)a[2].num(), (float)a[3].num(), bgr(a[4]), bgr(a[5]), 1, 1); return Value(); }

VS_FN(draw_set_blend_mode) { g.blendMode = a[0].ival(); gfx::setBlend(g.blendMode); return Value(); }
VS_FN(draw_set_blend_mode_ext) { gfx::setBlendExt(a[0].ival(), a[1].ival()); return Value(); }
VS_FN(draw_texture_flush) { gfx::flush(); return Value(); }
VS_FN(draw_enable_drawevent) { g.drawEventsEnabled = a[0].truthy(); return Value(); }
VS_FN(texture_set_interpolation) { g.texInterp = a[0].truthy(); gfx::setFilter(g.texInterp); return Value(); }
VS_FN(texture_set_repeat) { g.texRepeat = a[0].truthy(); return Value(); }
VS_FN(screen_save) { g.warnOnce("screen_save ignorado"); return Value(); }

// ------------------------------------------------------------------ escena
void Game::draw() {
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
    gfx::endFrame();
}

} // namespace vs
