// Volcado de un game.vsd: resumen, nombres de variables, funciones, uso de eventos.
#include "vsd.h"
#include <cstdio>
#include <map>
#include <set>

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("uso: vsdinfo game.vsd [vars|funcs|events|code NAME|rooms]\n"); return 1; }
    vs::GameData g; std::string err;
    if (!g.load(argv[1], err)) { std::printf("error: %s\n", err.c_str()); return 1; }
    std::string mode = argc > 2 ? argv[2] : "";
    if (mode.empty()) {
        std::printf("%s %ux%u rooms=%zu objs=%zu sprites=%zu bgs=%zu codes=%zu vars=%zu funcs=%zu scripts=%zu sounds=%zu tpag=%zu tex=%zu\n",
            g.name.c_str(), g.winW, g.winH, g.rooms.size(), g.objects.size(), g.sprites.size(), g.backgrounds.size(),
            g.codes.size(), g.varNames.size(), g.functions.size(), g.scripts.size(), g.sounds.size(), g.tpages.size(), g.textures.size());
        size_t ins = 0; for (auto& c : g.codes) ins += c.ins.size();
        std::printf("instrucciones=%zu  primera room=%s\n", ins, g.rooms[g.roomOrder[0]].name.c_str());
    } else if (mode == "sprframes" && argc > 3) {   // rectangulos de cada frame de un sprite (pagina, origen, tamano)
        for (auto& sp : g.sprites) if (sp.name == argv[3])
            for (int f : sp.frames) if (f >= 0 && f < (int)g.tpages.size()) { auto& t = g.tpages[f]; std::printf("p%d %u %u %u %u\n", t.tex, t.sx, t.sy, t.sw, t.sh); }
    } else if (mode == "roombgs" && argc > 3) {   // fondos de una sala
        for (auto& r : g.rooms) if (r.name == argv[3])
            for (auto& b : r.bgs) if (b.bg >= 0) std::printf("bg %d %s visible=%d fore=%d pos %d,%d tiles %d,%d\n", b.bg,
                b.bg < (int)g.backgrounds.size() ? g.backgrounds[b.bg].name.c_str() : "?", b.enabled, b.foreground, b.x, b.y, b.htiled, b.vtiled);
    } else if (mode == "bg" && argc > 3) {   // fondo N: pagina y rectangulo en el atlas
        int b = std::atoi(argv[3]);
        if (b >= 0 && b < (int)g.backgrounds.size()) {
            auto& bg = g.backgrounds[b];
            std::printf("%d %s tpag=%d\n", b, bg.name.c_str(), bg.tpag);
            if (bg.tpag >= 0 && bg.tpag < (int)g.tpages.size()) {
                auto& t = g.tpages[bg.tpag];
                std::printf("  pagina %d (%dx%d) origen %u,%u tam %ux%u destino %u,%u %ux%u caja %ux%u\n", t.tex, g.textures[t.tex].w, g.textures[t.tex].h,
                            t.sx, t.sy, t.sw, t.sh, t.tx, t.ty, t.tw, t.th, t.bw, t.bh);
            }
        }
    } else if (mode == "collisions") {
        for (auto& o : g.objects) for (auto& e : o.events) if (e.type == 4)
            std::printf("%s -> %s\n", o.name.c_str(), e.sub < g.objects.size() ? g.objects[e.sub].name.c_str() : "?");
    } else if (mode == "strings") {
        for (size_t i = 0; i < g.strings.size(); i++) std::printf("%zu %s\n", i, g.strings[i].c_str());
    } else if (mode == "vars") {
        for (auto& v : g.varNames) std::printf("%s\n", v.c_str());
    } else if (mode == "funcs") {
        for (auto& v : g.functions) std::printf("%s\n", v.c_str());
    } else if (mode == "events") {
        std::map<std::pair<int, unsigned>, int> cnt;
        for (auto& o : g.objects) for (auto& e : o.events) cnt[{e.type, e.sub}]++;
        for (auto& [k, n] : cnt) std::printf("type=%d sub=%u : %d\n", k.first, k.second, n);
    } else if (mode == "objects") {
        for (size_t i = 0; i < g.objects.size(); i++) std::printf("%zu %s parent=%d\n", i, g.objects[i].name.c_str(), g.objects[i].parent);
    } else if (mode == "rooms") {
        for (size_t i = 0; i < g.roomOrder.size(); i++) {
            auto& r = g.rooms[g.roomOrder[i]];
            int nv = 0; for (auto& v : r.views) nv += v.enabled;
            std::printf("%zu %s %ux%u spd=%u inst=%zu tiles=%zu views=%d pers=%d\n", i, r.name.c_str(), r.w, r.h, r.speed, r.insts.size(), r.tiles.size(), nv, r.persistent);
        }
    } else if (mode == "spritepages" && argc > 3) {
        for (auto& sp : g.sprites) if (sp.name.find(argv[3]) != std::string::npos) {
            std::map<int, int> pages;
            for (int f : sp.frames) if (f >= 0 && f < (int)g.tpages.size()) pages[g.tpages[f].tex]++;
            std::printf("%s:", sp.name.c_str());
            for (auto& kv : pages) std::printf(" p%d(%dx%d)x%d", kv.first, g.textures[kv.first].w, g.textures[kv.first].h, kv.second);
            std::printf("\n");
        }
    } else if (mode == "sounds") {
        for (size_t i = 0; i < g.sounds.size(); i++) { auto& s = g.sounds[i]; std::printf("%zu %s flags=0x%X type=%s file=%s aid=%d emb=%d vol=%g pitch=%g\n", i, s.name.c_str(), s.flags, s.type.c_str(), s.file.c_str(), s.audioId, s.embedded, s.volume, s.pitch); }
    } else if (mode == "roominst" && argc > 3) {
        for (auto& r : g.rooms) if (r.name == argv[3]) {
            std::map<std::string, int> cnt;
            for (auto& in : r.insts) cnt[in.obj >= 0 ? g.objects[in.obj].name : "?"]++;
            for (auto& kv : cnt) std::printf("%4d %s\n", kv.second, kv.first.c_str());
        }
    } else if (mode == "findobj" && argc > 3) {   // salas donde esta colocado cada objeto pedido
        for (int a = 3; a < argc; a++) {
            std::printf("%s:", argv[a]);
            for (auto& o : g.objects) if (o.name == argv[a]) std::printf(" [depth %d persistent %d visible %d parent %d]", o.depth, (int)o.persistent, (int)o.visible, o.parent);
            for (auto& r : g.rooms) { int n = 0; for (auto& in : r.insts) if (in.obj >= 0 && g.objects[in.obj].name == argv[a]) n++; if (n) std::printf(" %s(%d)", r.name.c_str(), n); }
            std::printf("\n");
        }
    } else if (mode == "roompos" && argc > 3) {
        for (auto& r : g.rooms) if (r.name == argv[3])
            for (auto& in : r.insts) if (in.obj >= 0) std::printf("%s %d %d id=%u\n", g.objects[in.obj].name.c_str(), in.x, in.y, in.id);
    } else if (mode == "code" && argc > 3) {
        for (auto& c : g.codes) if (c.name == argv[3]) {
            for (size_t i = 0; i < c.ins.size(); i++) {
                auto& x = c.ins[i];
                std::printf("%4zu op=%3u t=%u,%u cmp=%u inst=%d ex=%u fl=%u a=%d b=%d", i, x.op, x.t1, x.t2, x.cmp, x.inst, x.extra, x.flags, x.a, x.b);
                if ((x.op == 192 || x.op == 69) && x.t1 == 5) std::printf("  ; %s", g.varNames[x.a].c_str());
                if (x.op == 69 && x.t1 != 15) std::printf("  ; %s", g.varNames[x.a].c_str());
                if (x.op == 192 && x.t1 == 6) std::printf("  ; \"%s\"", g.strings[x.a].c_str());
                if (x.op == 217) std::printf("  ; %s", g.functions[x.a].c_str());
                std::printf("\n");
            }
        }
    }
    return 0;
}
