#include "vsd.h"
#include <cstdio>
#include <cstring>

namespace vs {

namespace {
struct Reader {
    std::vector<uint8_t> buf; size_t p = 0; bool bad = false;
    template <class T> T rd() {
        T v{};
        if (p + sizeof(T) > buf.size()) { bad = true; return v; }
        std::memcpy(&v, &buf[p], sizeof(T)); p += sizeof(T); return v;
    }
    uint32_t u32() { return rd<uint32_t>(); }
    int32_t i32() { return rd<int32_t>(); }
    uint16_t u16() { return rd<uint16_t>(); }
    int16_t i16() { return rd<int16_t>(); }
    float f32() { return rd<float>(); }
    bool b() { return rd<uint8_t>() != 0; }
    std::string str() {
        uint32_t n = u32();
        if (p + n > buf.size()) { bad = true; return {}; }
        std::string s((const char*)&buf[p], n); p += n; return s;
    }
    void bytes(std::vector<uint8_t>& out, size_t n) {
        if (p + n > buf.size()) { bad = true; return; }
        out.assign(buf.begin() + p, buf.begin() + p + n); p += n;
    }
};
}

bool GameData::load(const std::string& path, std::string& err) {
    Reader r;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "no se puede abrir " + path; return false; }
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    r.buf.resize(n);
    if (std::fread(r.buf.data(), 1, n, f) != (size_t)n) { std::fclose(f); err = "lectura incompleta"; return false; }
    std::fclose(f);
    if (n < 8 || std::memcmp(r.buf.data(), "VSD1", 4) != 0) { err = "no es un archivo VSD1"; return false; }
    r.p = 4;
    if (r.u32() != 1) { err = "version VSD no soportada"; return false; }

    name = r.str(); winW = r.u32(); winH = r.u32();
    roomOrder.resize(r.u32()); for (auto& x : roomOrder) x = r.i32();

    strings.resize(r.u32()); for (auto& s : strings) s = r.str();

    textures.resize(r.u32()); for (auto& t : textures) { t.w = r.i32(); t.h = r.i32(); }

    tpages.resize(r.u32());
    for (auto& t : tpages) {
        t.sx = r.u16(); t.sy = r.u16(); t.sw = r.u16(); t.sh = r.u16();
        t.tx = r.u16(); t.ty = r.u16(); t.tw = r.u16(); t.th = r.u16();
        t.bw = r.u16(); t.bh = r.u16(); t.tex = r.i32();
    }

    sprites.resize(r.u32());
    for (auto& s : sprites) {
        s.name = r.str(); s.w = r.u32(); s.h = r.u32(); s.ox = r.i32(); s.oy = r.i32();
        s.ml = r.i32(); s.mr = r.i32(); s.mt = r.i32(); s.mb = r.i32();
        s.bboxmode = r.u32(); s.sepmasks = r.u32();
        s.frames.resize(r.u32()); for (auto& fr : s.frames) fr = r.i32();
        s.masks.resize(r.u32());
        for (auto& m : s.masks) { m.w = r.u32(); m.h = r.u32(); uint32_t len = r.u32(); r.bytes(m.bits, len); }
    }

    backgrounds.resize(r.u32());
    for (auto& b : backgrounds) { b.name = r.str(); b.tpag = r.i32(); b.transparent = r.b(); b.smooth = r.b(); }

    fonts.resize(r.u32());
    for (auto& fo : fonts) {
        fo.name = r.str(); fo.display = r.str(); fo.size = r.f32(); fo.bold = r.b(); fo.italic = r.b();
        fo.tpag = r.i32(); fo.sx = r.f32(); fo.sy = r.f32();
        fo.glyphs.resize(r.u32());
        for (size_t i = 0; i < fo.glyphs.size(); i++) {
            auto& g = fo.glyphs[i];
            g.ch = r.u16(); g.x = r.u16(); g.y = r.u16(); g.w = r.u16(); g.h = r.u16(); g.shift = r.i16(); g.offset = r.i16();
            fo.byChar[g.ch] = (int)i;
        }
    }

    embeddedAudio = r.u32();
    sounds.resize(r.u32());
    for (auto& s : sounds) {
        s.name = r.str(); s.flags = r.u32(); s.type = r.str(); s.file = r.str();
        s.volume = r.f32(); s.pitch = r.f32(); s.audioId = r.i32(); s.embedded = r.i32();
    }

    functions.resize(r.u32()); for (auto& s : functions) s = r.str();
    doubles.resize(r.u32()); for (auto& d : doubles) d = r.rd<double>();

    codes.resize(r.u32());
    for (auto& c : codes) {
        int32_t nl = r.i32();
        if (r.p + nl > r.buf.size()) { r.bad = true; break; }
        c.name.assign((const char*)&r.buf[r.p], nl); r.p += nl;
        c.argc = r.u32(); c.locals = r.u32();
        c.ins.resize(r.u32());
        size_t bytes = c.ins.size() * sizeof(Ins);
        if (r.p + bytes > r.buf.size()) { r.bad = true; break; }
        std::memcpy(c.ins.data(), &r.buf[r.p], bytes); r.p += bytes;
    }

    varNames.resize(r.u32()); for (auto& s : varNames) s = r.str();

    scripts.resize(r.u32()); for (auto& s : scripts) { s.name = r.str(); s.code = r.i32(); }

    objects.resize(r.u32());
    for (auto& o : objects) {
        o.name = r.str(); o.sprite = r.i32(); o.visible = r.b(); o.solid = r.b(); o.depth = r.i32(); o.persistent = r.b();
        o.parent = r.i32(); o.mask = r.i32();
        o.events.resize(r.u32());
        for (auto& e : o.events) { e.type = r.i32(); e.sub = r.u32(); e.code = r.i32(); }
    }

    rooms.resize(r.u32());
    for (auto& rm : rooms) {
        rm.name = r.str(); rm.caption = r.str();
        rm.w = r.u32(); rm.h = r.u32(); rm.speed = r.u32(); rm.persistent = r.b();
        rm.bgcolor = r.u32(); rm.drawbg = r.b(); rm.code = r.i32(); rm.flags = r.u32();
        rm.bgs.resize(r.u32());
        for (auto& b : rm.bgs) {
            b.enabled = r.b(); b.foreground = r.b(); b.bg = r.i32(); b.x = r.i32(); b.y = r.i32();
            b.htiled = r.b(); b.vtiled = r.b(); b.hspeed = r.i32(); b.vspeed = r.i32(); b.stretch = r.b();
        }
        rm.views.resize(r.u32());
        for (auto& v : rm.views) {
            v.enabled = r.b(); v.vx = r.i32(); v.vy = r.i32(); v.vw = r.i32(); v.vh = r.i32();
            v.px = r.i32(); v.py = r.i32(); v.pw = r.i32(); v.ph = r.i32();
            v.bx = r.u32(); v.by = r.u32(); v.hs = r.i32(); v.vs = r.i32(); v.obj = r.i32();
        }
        rm.insts.resize(r.u32());
        for (auto& i : rm.insts) {
            i.x = r.i32(); i.y = r.i32(); i.obj = r.i32(); i.id = r.u32(); i.code = r.i32();
            i.sx = r.f32(); i.sy = r.f32(); i.color = r.u32(); i.rot = r.f32();
        }
        rm.tiles.resize(r.u32());
        for (auto& t : rm.tiles) {
            t.x = r.i32(); t.y = r.i32(); t.bg = r.i32(); t.sx = r.i32(); t.sy = r.i32(); t.w = r.u32(); t.h = r.u32();
            t.depth = r.i32(); t.id = r.u32(); t.scx = r.f32(); t.scy = r.f32(); t.color = r.u32();
        }
    }
    if (r.bad || r.p + 4 > r.buf.size() || std::memcmp(&r.buf[r.p], "END!", 4) != 0) { err = "VSD truncado o corrupto"; return false; }

    for (size_t i = 0; i < sprites.size(); i++) spriteByName[sprites[i].name] = (int)i;
    for (size_t i = 0; i < backgrounds.size(); i++) bgByName[backgrounds[i].name] = (int)i;
    for (size_t i = 0; i < sounds.size(); i++) soundByName[sounds[i].name] = (int)i;
    for (size_t i = 0; i < objects.size(); i++) objByName[objects[i].name] = (int)i;
    for (size_t i = 0; i < rooms.size(); i++) roomByName[rooms[i].name] = (int)i;
    for (size_t i = 0; i < scripts.size(); i++) scriptByName[scripts[i].name] = (int)i;
    for (size_t i = 0; i < fonts.size(); i++) fontByName[fonts[i].name] = (int)i;
    return true;
}

} // namespace vs
