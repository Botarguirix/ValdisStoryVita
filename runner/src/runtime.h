// Runtime GMS 1.x minimo para Valdis Story: instancias, eventos, rooms, VM, assets dinamicos.
#pragma once
#include "vsd.h"
#include "value.h"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include "intmap.h"
#include "varstore.h"
#include <unordered_set>
#include <vector>

namespace vs {

void logf(const char* fmt, ...);

enum EvType { EV_CREATE = 0, EV_DESTROY = 1, EV_ALARM = 2, EV_STEP = 3, EV_COLLISION = 4, EV_KEYBOARD = 5,
              EV_MOUSE = 6, EV_OTHER = 7, EV_DRAW = 8, EV_KEYPRESS = 9, EV_KEYRELEASE = 10 };
enum { STEP_NORMAL = 0, STEP_BEGIN = 1, STEP_END = 2 };
enum { OTHER_OUTSIDE = 0, OTHER_BOUNDARY = 1, OTHER_GAMESTART = 2, OTHER_GAMEEND = 3, OTHER_ROOMSTART = 4,
       OTHER_ROOMEND = 5, OTHER_ANIMEND = 7, OTHER_STEAM = 69 };

struct Instance {
    int id = 0, obj = -1;
    bool alive = true;      // false = destruida (pendiente de borrar)
    bool active = true;
    double x = 0, y = 0, xprev = 0, yprev = 0, xstart = 0, ystart = 0;
    double hspeed = 0, vspeed = 0, speed = 0, direction = 0;
    double image_index = 0, image_speed = 1, xscale = 1, yscale = 1, angle = 0, alpha = 1;
    uint32_t blend = 0xFFFFFF;
    int sprite = -1, mask = -1;
    double depth = 0;
    bool visible = true, solid = false, persistent = false;
    bool wasOutside = false;
    double alarm[12];
    VarStore vars;          // variables de instancia (disposicion por objeto, varstore.h)
    // caja de colision cacheada (Game::bbox): valida si mascara, posicion, escala y angulo no han cambiado. Antes era
    // una tabla global de 256 huecos y con ~470 instancias se pisaban unas a otras.
    struct BBoxCache { int m = -2; double x = 0, y = 0, xs = 0, ys = 0, a = 0, l = 0, t = 0, r = 0, b = 0; };
    mutable BBoxCache bbc;
    uint64_t createSerial = 0;
    Instance() { for (double& a : alarm) a = -1; }
    void setSpeedDir() { // speed/direction -> h/v
        double r = direction * M_PI / 180.0;
        hspeed = speed * std::cos(r); vspeed = -speed * std::sin(r);
        if (std::fabs(hspeed) < 1e-9) hspeed = 0;
        if (std::fabs(vspeed) < 1e-9) vspeed = 0;
    }
    void setHV() { // h/v -> speed/direction
        speed = std::sqrt(hspeed * hspeed + vspeed * vspeed);
        if (hspeed != 0 || vspeed != 0) { direction = std::atan2(-vspeed, hspeed) * 180.0 / M_PI; if (direction < 0) direction += 360; }
    }
};

// Frame de sprite listo para dibujar. tex: >=0 pagina de textura del data.win, <= -1000 textura dinamica
struct SFrame { int tex = -1; int texW = 1, texH = 1; int sx = 0, sy = 0, sw = 0, sh = 0; int tx = 0, ty = 0; int bw = 0, bh = 0; };
struct RSprite {
    std::string name; int w = 0, h = 0, ox = 0, oy = 0;
    int bl = 0, br = 0, bt = 0, bb = 0; // bbox (inclusive)
    bool precise = false; int kind = 1;  // kind: 0 precise 1 rect 2 ellipse 3 diamond
    std::vector<SFrame> frames; std::vector<Mask> masks;
    bool valid = true;
    float ext = 0; // distancia maxima del origen a una esquina de la caja (sin escala): descarte rapido de colisiones
};
struct RBackground { std::string name; SFrame f; int w = 0, h = 0; bool valid = true; };

struct Tile {
    int id; int bg; int x, y, sx, sy, w, h; int depth; double xscale = 1, yscale = 1; uint32_t blend = 0xFFFFFF; double alpha = 1; bool visible = true;
};
struct ViewState { bool enabled = false, visible = false; double xview = 0, yview = 0, wview = 0, hview = 0;
    double xport = 0, yport = 0, wport = 0, hport = 0; double angle = 0; int hborder = 0, vborder = 0, hspeed = -1, vspeed = -1; int obj = -1; };
struct BgState { bool visible = false, foreground = false; int index = -1; double x = 0, y = 0; bool htiled = false, vtiled = false;
    double hspeed = 0, vspeed = 0, xscale = 1, yscale = 1; uint32_t blend = 0xFFFFFF; double alpha = 1; bool stretch = false; };

struct PersistentRoom { std::vector<std::unique_ptr<Instance>> insts; std::vector<Tile> tiles; };

struct Ctx { Instance* self = nullptr; Instance* other = nullptr; };
class Game;
using BuiltinFn = Value (*)(Game&, Ctx&, Value* args, int argc);

// Registro global de funciones builtin (rellenado por fn_*.cpp)
std::unordered_map<std::string, BuiltinFn>& builtinRegistry();
struct BuiltinReg { BuiltinReg(const char* n, BuiltinFn f) { builtinRegistry()[n] = f; } };
#define VS_FN(name) static Value fn_##name(Game& g, Ctx& c, Value* a, int n); \
    static BuiltinReg reg_##name(#name, fn_##name); \
    static Value fn_##name([[maybe_unused]] Game& g, [[maybe_unused]] Ctx& c, [[maybe_unused]] Value* a, [[maybe_unused]] int n)

// variables builtin
enum BV : int { BV_NONE = 0,
    BV_X, BV_Y, BV_XPREV, BV_YPREV, BV_HSPEED, BV_VSPEED, BV_SPEED, BV_DIRECTION,
    BV_IMAGE_INDEX, BV_IMAGE_SPEED, BV_XSCALE, BV_YSCALE, BV_ANGLE, BV_ALPHA, BV_BLEND, BV_IMAGE_NUMBER,
    BV_SPRITE_INDEX, BV_MASK_INDEX, BV_DEPTH, BV_VISIBLE, BV_PERSISTENT, BV_OBJECT_INDEX, BV_ID, BV_ALARM,
    BV_BBOX_LEFT, BV_BBOX_RIGHT, BV_BBOX_TOP, BV_BBOX_BOTTOM, BV_SOLID, BV_XSTART, BV_YSTART,
    BV_SPRITE_WIDTH, BV_SPRITE_HEIGHT,
    // globales
    BV_ROOM, BV_ROOM_WIDTH, BV_ROOM_HEIGHT, BV_ROOM_SPEED, BV_ROOM_PERSISTENT,
    BV_VIEW_VISIBLE, BV_VIEW_ENABLED, BV_VIEW_XVIEW, BV_VIEW_YVIEW, BV_VIEW_WVIEW, BV_VIEW_HVIEW,
    BV_VIEW_XPORT, BV_VIEW_YPORT, BV_VIEW_WPORT, BV_VIEW_HPORT, BV_VIEW_HBORDER, BV_VIEW_VBORDER,
    BV_VIEW_HSPEED, BV_VIEW_VSPEED, BV_VIEW_OBJECT, BV_VIEW_CURRENT,
    BV_BACKGROUND_X, BV_BACKGROUND_Y, BV_BACKGROUND_COLOR, BV_BACKGROUND_VISIBLE, BV_BACKGROUND_INDEX,
    BV_KEYBOARD_LASTKEY, BV_KEYBOARD_LASTCHAR, BV_OS_TYPE, BV_ASYNC_LOAD, BV_FPS, BV_CURRENT_TIME,
    BV_ARGUMENT, BV_ARGUMENT_COUNT, BV_ARG0, // ARG0..ARG15 consecutivos
    BV_INSTANCE_COUNT = BV_ARG0 + 16, BV_WORKING_DIRECTORY, BV_INSTANCE_ID,
    BV_COUNT };

struct InputState {
    bool down[256] = {}, prev[256] = {}, pressed[256] = {}, released[256] = {};
    bool phys[256] = {}, virt[256] = {};
    int lastKey = 0; std::string lastChar;
    // gamepad (slot 0)
    bool gpConnected = false; bool gpBtn[32] = {}; float gpAxis[4] = {};
    float gpThreshold = 0.5f;
};

struct ParticleType {
    int sprite = -1; bool animate = false, stretch = false, randomFrame = false;
    double lifeMin = 100, lifeMax = 100;
    double sizeMin = 1, sizeMax = 1, sizeIncr = 0, sizeWiggle = 0;
    double xscale = 1, yscale = 1;
    double speedMin = 0, speedMax = 0, speedIncr = 0, speedWiggle = 0;
    double dirMin = 0, dirMax = 0, dirIncr = 0, dirWiggle = 0;
    double angMin = 0, angMax = 0, angIncr = 0, angWiggle = 0; bool angRelative = false;
    double grav = 0, gravDir = 270;
    int colorMode = 0; uint32_t c[3] = {0xFFFFFF, 0xFFFFFF, 0xFFFFFF};
    int alphaMode = 0; double al[3] = {1, 1, 1};
};
struct Particle { int type; double x, y, speed, dir, ang, size, life, maxLife, frame; uint32_t col; bool fixedCol; };
struct ParticleSystem { bool alive = true, autoDraw = true; double depth = 0; std::vector<Particle> parts; };

struct EvRef { int code = -1; int owner = -1; };

class Game {
public:
    GameData d;
    std::string dataDir;   // carpeta con game.vsd, tex/, snd/
    std::string gameDir;   // carpeta de assets externos (data_N/, music_*.ogg)
    std::string saveDir;   // carpeta de guardado (ini)

    // ---- VM
    std::vector<int> varBuiltin;            // varId -> BV
    std::vector<BuiltinFn> funcs;           // funcId -> builtin (o null)
    std::vector<int> funcScript;            // funcId -> script code (o -1)
    IntMap<Value> globals;
    int varArgument = -1;
    Value call(int codeId, Ctx ctx, Value* args, int argc);
    // ---- semantica compartida por el interprete y el codigo nativo (AOT)
    void vmRead(Ctx& ctx, int inst, int var, bool hasIdx, int row, int col, Value& out);          // no locales
    void vmWrite(Ctx& ctx, int inst, int var, bool hasIdx, int row, int col, const Value& v);     // no locales
    void vmWarnLocal(int var);
    void vmBinary(uint8_t op, uint8_t t1, uint8_t t2, const Value& a, const Value& b, Value& out); // camino general
    bool vmCompare(uint8_t cmp, const Value& a, const Value& b);
    // CALL de bytecode: camino rapido en linea (llamada directa a la funcion integrada); perfiles y avisos en vmCallFnSlow
    inline Value vmCallFn(int fn, Ctx& ctx, Value* av, int argc);
    Value vmCallFnSlow(int fn, Ctx& ctx, Value* av, int argc);
    // codigo nativo: tabla codeId -> funcion (nullptr = interpretar)
    using AotFn = Value (*)(Game&, Ctx&, Value*, int);
    std::vector<AotFn> aot; bool aotEnabled = false;
    Value runScript(int scriptIdx, Ctx& ctx, Value* args, int argc);
    std::vector<std::string> warnedFuncs;
    IntMap<char> warnedVar;              // avisos de variables ya emitidos (codigo*8192+var): sin construir strings
    std::vector<StrObj*> strConst;       // constantes de cadena internadas (no se reservan en cada push)
    bool warnVarOnce(int code, int var) { int k = code * 8192 + var; if (warnedVar.count(k)) return false; warnedVar[k] = 1; return true; }
    void warnOnce(const std::string& what);
    uint64_t insCount = 0;

    // ---- assets en runtime (mutables: sprite_add/assign)
    std::vector<RSprite> sprites;
    std::vector<RBackground> backgrounds;
    int addDynamicTexture(const std::string& path, int& w, int& h); // devuelve handle <= -1000
    int fontCur = -1; std::vector<vs::Font> extraFonts; // font_add_sprite

    // ---- eventos
    std::vector<std::unordered_map<uint64_t, EvRef>> evTable; // por objeto, con herencia (solo al iniciar)
    std::vector<std::vector<std::pair<uint64_t, EvRef>>> evList; // lo mismo en vector: busqueda lineal sin hash
    // acceso directo a los eventos de cada frame (step normal/begin/end, draw 0) y banderas por objeto
    std::vector<std::array<EvRef, 4>> fastEv;
    uint64_t tileSig = 0; std::vector<std::pair<int, std::vector<int>>> tileGroups; // agrupado de tiles para dibujar
    std::vector<uint8_t> hasKeyEv;
    std::vector<std::vector<bool>> isA;                       // isA[obj][ancestro]
    std::vector<std::vector<int>> collisionPairs;             // por objeto: objetos con evento de colision
    static uint64_t evKey(int t, uint32_t s) { return ((uint64_t)t << 32) | s; }
    EvRef findEvent(int obj, int type, uint32_t sub) const;
    bool fireEvent(Instance* inst, int type, uint32_t sub, Instance* other = nullptr);
    bool fireEventOwner(Instance* inst, int owner, int type, uint32_t sub, Instance* other);
    // contexto del evento actual (event_inherited)
    struct EvCtx { int owner, type; uint32_t sub; };
    std::vector<EvCtx> evStack;

    // ---- instancias
    std::vector<std::unique_ptr<Instance>> insts;  // orden de creacion
    IntMap<Instance*> byId;
    // instancias vivas por objeto (incluye hijos), reconstruido cuando cambia el conjunto de instancias
    mutable std::vector<std::vector<Instance*>> byObj; mutable bool byObjDirty = true;
    const std::vector<Instance*>& instancesOf(int obj) const;
    int nextId = 100000; uint64_t serial = 0;
    Instance* createInstance(double x, double y, int obj, bool runCreate = true, int forceId = -1);
    void destroyInstance(Instance* i, bool runEvent = true);
    Instance* findInstance(int id) const { Instance* const* p = byId.get(id); return p && (*p)->alive ? *p : nullptr; }
    void collectTargets(double target, Ctx& ctx, std::vector<Instance*>& out);
    // Recorre los destinos de un objeto/keyword sin reservar memoria; f(Instance*) devuelve true para parar.
    // (No debe crear ni destruir instancias dentro de f.)
    template<class F> bool eachTarget(double target, Ctx& ctx, F&& f) {
        int t = (int)target;
        if (t == -1) return ctx.self && ctx.self->alive && f(ctx.self);
        if (t == -2) return ctx.other && ctx.other->alive && f(ctx.other);
        if (t == -3) { for (auto& p : insts) if (p->alive && f(p.get())) return true; return false; }
        if (t < 0) return false;
        if (t >= 100000) { Instance* i = findInstance(t); return i && f(i); }
        if (t >= (int)isA.size()) return false;
        for (Instance* p : instancesOf(t)) if (p->alive && f(p)) return true;
        return false;
    }
    std::vector<Instance*> collTmp;   // copia reutilizable de destinos para la fase de colisiones
    Instance* firstOf(int obj) const;
    void purgeDead();

    // ---- room
    int room = -1, pendingRoom = -1; bool restartRequested = false, endRequested = false;
    int roomW = 0, roomH = 0, roomSpeed = 60; bool roomPersistent = false;
    uint32_t bgColor = 0; bool drawBgColor = true;
    std::array<ViewState, 8> views; bool viewsEnabled = false; int viewCurrent = 0;
    std::array<BgState, 8> bgs;
    std::vector<Tile> tiles; int nextTileId = 10000000;
    std::unordered_map<int, bool> roomPersistOverride;
    std::unordered_map<int, PersistentRoom> savedRooms;
    void gotoRoom(int r, bool first = false);
    std::vector<Instance*> orderedAlive(); // vivas, en orden de despacho de eventos de GMS1
    void updateViewsFollow();
    void preloadRoomPages();

    // ---- loop
    bool init(const std::string& dataDir, const std::string& gameDir, const std::string& saveDir, std::string& err);
    void step();   // un frame de logica
    void draw();   // un frame de render
    // perfilado: llamar una vez por frame con los ms de logica y render; escribe [perf] en vsrunner.log
    void perfFrame(double stepMs, double drawMs);
    bool perfVerbose = false;
    double audioMs = 0; int audioCalls = 0;   // tiempo en audio_* este frame (apertura de ogg/wav)
    // perfilador de scripts/builtins/fases (--profcode en PC; en Vita, crear save/prof)
    bool prof = false;
    std::vector<double> profFn, profCode; std::vector<uint32_t> profFnN, profCodeN;
    double profPhase[16] = {}; double profChild = 0; int profFrames = 0;
    static double profNow();
    void profDump();
    int frame = 0; double fps = 60; uint64_t startMs = 0;
    // modo determinista (pruebas AOT vs interprete): semilla fija, tiempo = frames, audio sin efecto en la logica
    bool det = false; uint32_t detSeed = 0;
    uint64_t stateHash() const;

    // ---- input
    InputState in;
    void inputBeginFrame();  // llamado por la plataforma antes de step()

    // ---- dibujo
    int drawHalign = 0, drawValign = 0; int blendMode = 0; bool drawEventsEnabled = true;
    bool texInterp = false; bool texRepeat = false;
    struct PrimVert { float x, y, u, v; uint32_t col; float a; };
    int primKind = 0; int primTex = -1; std::vector<PrimVert> prim; SFrame primFrame;

    // ---- ds / json
    std::vector<std::unique_ptr<std::vector<Value>>> dsLists;
    std::vector<std::unique_ptr<std::map<std::string, Value>>> dsMaps;

    // ---- ini
    std::string iniPath; std::map<std::string, std::map<std::string, std::string>> ini; bool iniDirty = false, iniOpen = false;
    std::string savePath(const std::string& name) const;

    // ---- particulas
    std::vector<std::unique_ptr<ParticleType>> partTypes;
    std::vector<std::unique_ptr<ParticleSystem>> partSystems;
    void updateParticles();
    bool particleAutoDraw(size_t k, double& depth) const;
    void drawParticleSystem(size_t k);

    // ---- utilidades
    std::mt19937* rng = nullptr;
    double random(double hi);
    void bbox(const Instance* i, double& l, double& t, double& r, double& b) const;
    bool instancesCollide(const Instance* a, const Instance* b) const;
    bool pointInInstance(const Instance* i, double px, double py) const;
    bool rectInInstance(const Instance* i, double l, double t, double r, double b, bool prec) const;
    int maskOf(const Instance* i) const { return i->mask >= 0 ? i->mask : i->sprite; }
    int imageNumber(const Instance* i) const { return (i->sprite >= 0 && i->sprite < (int)sprites.size()) ? (int)sprites[i->sprite].frames.size() : 0; }

    // ---- builtin vars
    bool getBuiltin(int bv, Instance* self, int arrIdx, Value& out);
    bool setBuiltin(int bv, Instance* self, int arrIdx, const Value& v);
    Value* curArgs = nullptr; int curArgc = 0; int curCode = -1;

    // ---- render helpers (fn_draw.cpp)
    void drawSpriteFrame(int spr, double sub, double x, double y, double xs, double ys, double rot, uint32_t col, double alpha);
    void drawFramePart(const SFrame& f, double left, double top, double w, double h, double x, double y, double xs, double ys, uint32_t col, double alpha);
    void drawText(double x, double y, const std::string& s, double xs, double ys, double ang, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, double alpha, double sep, double w);
    void drawInstanceDefault(Instance* i);

    // ---- audio (fn_audio.cpp)
    void audioInit();
    void audioUpdate();
};

extern Game* gGame;

extern int gSlowFnWatch;            // vm.cpp
extern volatile int gSampFn;        // vm.cpp: funcion integrada en curso (muestreador)
inline Value Game::vmCallFn(int fn, Ctx& ctx, Value* av, int argc) {
    BuiltinFn f = funcs[fn];
    if (f && gSlowFnWatch <= 0 && !prof) {
        int prevFn = gSampFn; gSampFn = fn;
        Ctx cc = ctx;
        Value r = f(*this, cc, av, argc);
        gSampFn = prevFn;
        return r;
    }
    return vmCallFnSlow(fn, ctx, av, argc);
}

} // namespace vs
