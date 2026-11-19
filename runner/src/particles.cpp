// Sistema de particulas GMS1 (subconjunto usado por Valdis Story).
#include "runtime.h"
#include "gfx.h"
#include <algorithm>

namespace vs {


static ParticleType* ptype(Game& g, const Value& v) { int i = v.ival(); return (i >= 0 && i < (int)g.partTypes.size() && g.partTypes[i]) ? g.partTypes[i].get() : nullptr; }
static ParticleSystem* psys(Game& g, const Value& v) { int i = v.ival(); return (i >= 0 && i < (int)g.partSystems.size() && g.partSystems[i] && g.partSystems[i]->alive) ? g.partSystems[i].get() : nullptr; }

static double rr(Game& g, double a, double b) { return a + g.random(b - a); }
static uint32_t mixc(uint32_t p, uint32_t q, double t) {
    auto ch = [&](int sh) { return (uint32_t)std::lround(((p >> sh) & 255) * (1 - t) + ((q >> sh) & 255) * t) << sh; };
    return ch(0) | ch(8) | ch(16);
}

VS_FN(part_type_create) { g.partTypes.push_back(std::make_unique<ParticleType>()); return Value((double)(g.partTypes.size() - 1)); }
VS_FN(part_type_sprite) { if (auto* t = ptype(g, a[0])) { t->sprite = a[1].ival(); t->animate = a[2].truthy(); t->stretch = a[3].truthy(); t->randomFrame = a[4].truthy(); } return Value(); }
VS_FN(part_type_life) { if (auto* t = ptype(g, a[0])) { t->lifeMin = a[1].num(); t->lifeMax = a[2].num(); } return Value(); }
VS_FN(part_type_size) { if (auto* t = ptype(g, a[0])) { t->sizeMin = a[1].num(); t->sizeMax = a[2].num(); t->sizeIncr = a[3].num(); t->sizeWiggle = a[4].num(); } return Value(); }
VS_FN(part_type_scale) { if (auto* t = ptype(g, a[0])) { t->xscale = a[1].num(); t->yscale = a[2].num(); } return Value(); }
VS_FN(part_type_speed) { if (auto* t = ptype(g, a[0])) { t->speedMin = a[1].num(); t->speedMax = a[2].num(); t->speedIncr = a[3].num(); t->speedWiggle = a[4].num(); } return Value(); }
VS_FN(part_type_direction) { if (auto* t = ptype(g, a[0])) { t->dirMin = a[1].num(); t->dirMax = a[2].num(); t->dirIncr = a[3].num(); t->dirWiggle = a[4].num(); } return Value(); }
VS_FN(part_type_orientation) { if (auto* t = ptype(g, a[0])) { t->angMin = a[1].num(); t->angMax = a[2].num(); t->angIncr = a[3].num(); t->angWiggle = a[4].num(); t->angRelative = a[5].truthy(); } return Value(); }
VS_FN(part_type_gravity) { if (auto* t = ptype(g, a[0])) { t->grav = a[1].num(); t->gravDir = a[2].num(); } return Value(); }
VS_FN(part_type_color2) { if (auto* t = ptype(g, a[0])) { t->colorMode = 2; t->c[0] = (uint32_t)a[1].num(); t->c[1] = (uint32_t)a[2].num(); } return Value(); }
VS_FN(part_type_color3) { if (auto* t = ptype(g, a[0])) { t->colorMode = 3; t->c[0] = (uint32_t)a[1].num(); t->c[1] = (uint32_t)a[2].num(); t->c[2] = (uint32_t)a[3].num(); } return Value(); }
VS_FN(part_type_alpha2) { if (auto* t = ptype(g, a[0])) { t->alphaMode = 2; t->al[0] = a[1].num(); t->al[1] = a[2].num(); } return Value(); }
VS_FN(part_type_alpha3) { if (auto* t = ptype(g, a[0])) { t->alphaMode = 3; t->al[0] = a[1].num(); t->al[1] = a[2].num(); t->al[2] = a[3].num(); } return Value(); }

VS_FN(part_system_create) { g.partSystems.push_back(std::make_unique<ParticleSystem>()); return Value((double)(g.partSystems.size() - 1)); }
VS_FN(part_system_destroy) { if (auto* s = psys(g, a[0])) { s->alive = false; s->parts.clear(); } return Value(); }
VS_FN(part_system_exists) { return Value(psys(g, a[0]) ? 1.0 : 0.0); }
VS_FN(part_system_depth) { if (auto* s = psys(g, a[0])) s->depth = a[1].num(); return Value(); }
VS_FN(part_system_automatic_draw) { if (auto* s = psys(g, a[0])) s->autoDraw = a[1].truthy(); return Value(); }
VS_FN(part_system_drawit) { int k = a[0].ival(); if (psys(g, a[0])) g.drawParticleSystem((size_t)k); return Value(); }
VS_FN(part_particles_clear) { if (auto* s = psys(g, a[0])) s->parts.clear(); return Value(); }
VS_FN(part_particles_count) { auto* s = psys(g, a[0]); return Value(s ? (double)s->parts.size() : 0.0); }

static void spawn(Game& g, ParticleSystem* s, int ti, double x, double y, int count, bool fixed, uint32_t col) {
    ParticleType* t = (ti >= 0 && ti < (int)g.partTypes.size()) ? g.partTypes[ti].get() : nullptr;
    if (!t || !s) return;
    for (int k = 0; k < count; k++) {
        Particle p; p.type = ti; p.x = x; p.y = y;
        p.speed = rr(g, t->speedMin, t->speedMax); p.dir = rr(g, t->dirMin, t->dirMax);
        p.ang = rr(g, t->angMin, t->angMax); p.size = rr(g, t->sizeMin, t->sizeMax);
        p.maxLife = p.life = std::max(1.0, std::round(rr(g, t->lifeMin, t->lifeMax)));
        int nf = (t->sprite >= 0 && t->sprite < (int)g.sprites.size()) ? (int)g.sprites[t->sprite].frames.size() : 1;
        p.frame = t->randomFrame ? (int)g.random(nf) : 0;
        p.fixedCol = fixed; p.col = fixed ? col : t->c[0];
        s->parts.push_back(p);
    }
}
VS_FN(part_particles_create) { spawn(g, psys(g, a[0]), a[3].ival(), a[1].num(), a[2].num(), a[4].ival(), false, 0); return Value(); }
VS_FN(part_particles_create_color) { spawn(g, psys(g, a[0]), a[3].ival(), a[1].num(), a[2].num(), a[5].ival(), true, (uint32_t)a[4].num() & 0xFFFFFF); return Value(); }

void Game::updateParticles() {
    for (auto& sp : partSystems) {
        if (!sp || !sp->alive) continue;
        auto& v = sp->parts; size_t w = 0;
        for (size_t i = 0; i < v.size(); i++) {
            Particle& p = v[i];
            ParticleType* t = (p.type >= 0 && p.type < (int)partTypes.size()) ? partTypes[p.type].get() : nullptr;
            if (!t || --p.life <= 0) continue;
            double r = p.dir * M_PI / 180.0;
            double hx = std::cos(r) * p.speed, vy = -std::sin(r) * p.speed;
            if (t->grav != 0) { double gr = t->gravDir * M_PI / 180.0; hx += std::cos(gr) * t->grav; vy -= std::sin(gr) * t->grav; }
            p.speed = std::sqrt(hx * hx + vy * vy); if (hx != 0 || vy != 0) p.dir = std::atan2(-vy, hx) * 180.0 / M_PI;
            p.x += hx; p.y += vy;
            p.speed = std::max(0.0, p.speed + t->speedIncr);
            p.dir += t->dirIncr; p.ang += t->angIncr; p.size = std::max(0.0, p.size + t->sizeIncr);
            if (t->animate) p.frame += 1;
            v[w++] = p;
        }
        v.resize(w);
    }
}

bool Game::particleAutoDraw(size_t k, double& depth) const {
    if (k >= partSystems.size() || !partSystems[k] || !partSystems[k]->alive || !partSystems[k]->autoDraw) return false;
    depth = partSystems[k]->depth; return true;
}

void Game::drawParticleSystem(size_t k) {
    if (k >= partSystems.size() || !partSystems[k] || !partSystems[k]->alive) return;
    for (auto& p : partSystems[k]->parts) {
        ParticleType* t = (p.type >= 0 && p.type < (int)partTypes.size()) ? partTypes[p.type].get() : nullptr;
        if (!t || t->sprite < 0) continue;
        double age = 1.0 - p.life / p.maxLife; // 0..1
        uint32_t col = p.col;
        if (!p.fixedCol) {
            if (t->colorMode == 2) col = mixc(t->c[0], t->c[1], age);
            else if (t->colorMode == 3) col = age < 0.5 ? mixc(t->c[0], t->c[1], age * 2) : mixc(t->c[1], t->c[2], (age - 0.5) * 2);
        }
        double al = t->al[0];
        if (t->alphaMode == 2) al = t->al[0] + (t->al[1] - t->al[0]) * age;
        else if (t->alphaMode == 3) al = age < 0.5 ? t->al[0] + (t->al[1] - t->al[0]) * age * 2 : t->al[1] + (t->al[2] - t->al[1]) * (age - 0.5) * 2;
        double frame = p.frame;
        if (t->stretch) { int nf = (int)sprites[t->sprite].frames.size(); frame = age * nf; }
        double ang = p.ang + (t->angRelative ? p.dir : 0);
        drawSpriteFrame(t->sprite, frame, p.x, p.y, p.size * t->xscale, p.size * t->yscale, ang, col & 0xFFFFFF, al);
    }
}

} // namespace vs
