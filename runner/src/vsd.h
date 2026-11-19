// Lector del formato VSD (exportado desde data.win por tools/export_vsdata.csx).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace vs {

struct TPage { uint16_t sx, sy, sw, sh, tx, ty, tw, th, bw, bh; int tex; };
struct Mask { uint32_t w = 0, h = 0; std::vector<uint8_t> bits; // filas de (w+7)/8 bytes, MSB primero
    bool get(int x, int y) const {
        if (x < 0 || y < 0 || (uint32_t)x >= w || (uint32_t)y >= h) return false;
        uint32_t stride = (w + 7) / 8;
        return (bits[y * stride + (x >> 3)] >> (7 - (x & 7))) & 1;
    }
};
struct Sprite {
    std::string name; uint32_t w = 0, h = 0; int ox = 0, oy = 0;
    int ml = 0, mr = 0, mt = 0, mb = 0; uint32_t bboxmode = 0, sepmasks = 0;
    std::vector<int> frames; std::vector<Mask> masks;
};
struct Background { std::string name; int tpag = -1; bool transparent = false, smooth = false; };
struct Glyph { uint16_t ch, x, y, w, h; int16_t shift, offset; };
struct Font {
    std::string name, display; float size = 0; bool bold = false, italic = false;
    int tpag = -1; float sx = 1, sy = 1; std::vector<Glyph> glyphs;
    std::unordered_map<uint32_t, int> byChar;
};
struct Sound { std::string name, type, file; uint32_t flags = 0; float volume = 1, pitch = 1; int audioId = -1, embedded = -1; };

#pragma pack(push, 1)
struct Ins { uint8_t op, t1, t2, cmp; int16_t inst; uint8_t extra, flags; int32_t a, b; };
#pragma pack(pop)
static_assert(sizeof(Ins) == 16, "Ins");

struct Code { std::string name; uint32_t argc = 0, locals = 0; std::vector<Ins> ins; };
struct Script { std::string name; int code = -1; };
struct Event { int type; uint32_t sub; int code; };
struct Object {
    std::string name; int sprite = -1; bool visible = true, solid = false; int depth = 0;
    bool persistent = false; int parent = -1, mask = -1; std::vector<Event> events;
};
struct RoomBg { bool enabled, foreground; int bg; int x, y; bool htiled, vtiled; int hspeed, vspeed; bool stretch; };
struct RoomView { bool enabled; int vx, vy, vw, vh, px, py, pw, ph; uint32_t bx, by; int hs, vs; int obj; };
struct RoomInst { int x, y, obj; uint32_t id; int code; float sx, sy; uint32_t color; float rot; };
struct RoomTile { int x, y, bg, sx, sy; uint32_t w, h; int depth; uint32_t id; float scx, scy; uint32_t color; };
struct Room {
    std::string name, caption; uint32_t w = 0, h = 0, speed = 30; bool persistent = false;
    uint32_t bgcolor = 0; bool drawbg = true; int code = -1; uint32_t flags = 0;
    std::vector<RoomBg> bgs; std::vector<RoomView> views; std::vector<RoomInst> insts; std::vector<RoomTile> tiles;
};
struct TexInfo { int w, h; };

struct GameData {
    std::string name; uint32_t winW = 0, winH = 0;
    std::vector<int> roomOrder;
    std::vector<std::string> strings;
    std::vector<TexInfo> textures;
    std::vector<TPage> tpages;
    std::vector<Sprite> sprites;
    std::vector<Background> backgrounds;
    std::vector<Font> fonts;
    uint32_t embeddedAudio = 0;
    std::vector<Sound> sounds;
    std::vector<std::string> functions;
    std::vector<double> doubles;
    std::vector<Code> codes;
    std::vector<std::string> varNames;
    std::vector<Script> scripts;
    std::vector<Object> objects;
    std::vector<Room> rooms;

    // indices por nombre (assets)
    std::unordered_map<std::string, int> spriteByName, bgByName, soundByName, objByName, roomByName, scriptByName, fontByName;

    bool load(const std::string& path, std::string& err);
};

} // namespace vs
