#include "gfx.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <zlib.h>
#include <vector>

#ifdef __vita__
#include <vitaGL.h>
#else
#include <SDL_opengl.h>
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_FUNC_REVERSE_SUBTRACT
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#endif
#ifndef GL_FUNC_ADD
#define GL_FUNC_ADD 0x8006
#endif
#ifndef GL_MAX
#define GL_MAX 0x8008
#endif

#include "../third_party/stb_image.h"   // implementacion en stbimpl.cpp
#include "sysmutex.h"
#include "lz4lite.h"
#include <deque>
#include <pthread.h>

#if !defined(__vita__)
typedef void (*PFN_BlendEquation)(GLenum);
static PFN_BlendEquation pglBlendEquation = nullptr;
#include <SDL.h>
#endif

namespace gfx {

struct Tex {
    GLuint id = 0; int w = 1, h = 1; size_t bytes = 0; uint64_t lastUse = 0; bool loaded = false; bool failed = false;
    uint64_t retryAt = 0; // si no hubo memoria de GPU, no reintentar hasta este frame
    bool pending = false; // pedida al hilo cargador
    int filt = -1, wrap = -1; // estado GL actual de la textura (evita glTexParameteri redundantes)
};
struct Vtx { float x, y, u, v; uint8_t r, g, b, a; };

static std::string gTexDir;
static size_t gBudget = 0, gBytes = 0;
static int gMaxTex = 4096;   // lado maximo de textura que se usa (min(GL_MAX_TEXTURE_SIZE, 4096))
static std::vector<Tex> gPages;
static std::unordered_map<int, Tex> gDyn;
static int gNextDyn = -1000;
static GLuint gWhite = 0;
static uint64_t gFrame = 0;
static int gPixW = 960, gPixH = 544, gLogW = 960, gLogH = 544;
static std::vector<Vtx> gBatch;
static int gBatchTex = -2;
static bool gLinear = false, gRepeat = false;
static int gBlendSrc = GL_SRC_ALPHA, gBlendDst = GL_ONE_MINUS_SRC_ALPHA, gBlendEq = 0;

#if !defined(__vita__)
typedef void (*PFN_CompTex)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
static PFN_CompTex pglCompressedTexImage2D = nullptr;
#define glCompressedTexImage2D pglCompressedTexImage2D
#endif
static Perf gPerf;
static std::vector<std::pair<uint64_t, std::vector<unsigned char>>> gUploadKeep; // datos subidos aun "en vuelo"
Perf& perf() { return gPerf; }
static size_t gpuFree();
int gpuFreeMB() {
#ifdef __vita__
    return (int)(gpuFree() >> 20);
#else
    return -1;
#endif
}
// el log del runner (vs::logf) vive en runtime.cpp; gfx no depende de el: se usa un puntero que fija el runtime
void (*gLogHook)(const char*) = nullptr;
static void gfxLog(const char* fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (gLogHook) gLogHook(b); else std::fputs(b, stdout);
}

static double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static void texParams(GLuint id) {
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// Archivo .vdx (generado por tools/build_vsdata.py): "VDX3" (o "VDX2"), w, h (textura, multiplos de 4), ancho/alto
// reales, formato GL (DXT1/DXT5), tamano DXT, tamano comprimido (0 = sin comprimir), datos. Los bloques S3TC se suben
// tal cual: sin decodificar PNG y 4-8x menos memoria. VDX3 usa RLE por bloques (descompresion ~memcpy); VDX2 usa
// zlib, que en la Vita costaba 200-300 ms por pagina de 2048x2048.
static bool unrle(const unsigned char* src, size_t srcLen, unsigned char* dst, size_t dstLen, size_t bs) {
    const unsigned char* end = src + srcLen; size_t o = 0;
    while (src + 4 <= end) {
        uint32_t tok; std::memcpy(&tok, src, 4); src += 4;
        size_t n = tok & 0x7fffffff;
        if (tok & 0x80000000u) {
            if (src + bs > end || o + n * bs > dstLen) return false;
            for (size_t k = 0; k < n; k++, o += bs) std::memcpy(dst + o, src, bs);
            src += bs;
        } else {
            if (src + n * bs > end || o + n * bs > dstLen) return false;
            std::memcpy(dst + o, src, n * bs); src += n * bs; o += n * bs;
        }
    }
    return o == dstLen;
}

static void evictBytes(size_t want, const Tex* keep);
// Memoria de GPU: antes se subia sin mirar y, al agotarse, vitaGL dejaba la textura vacia (sprites invisibles).
static size_t gpuFree() {
#ifdef __vita__
    return vglMemFree(VGL_MEM_ALL);
#else
    return (size_t)1 << 40;
#endif
}
static constexpr size_t kGpuReserve = 24u * 1024 * 1024; // para buffers de vertices, framebuffers, fuentes...
// deja sitio para 'need' bytes expulsando paginas viejas (vitaGL libera de forma diferida: se cuenta lo expulsado)
static void makeRoom(size_t need, const Tex* keep) {
    size_t f = gpuFree();
    if (f >= need + kGpuReserve) return;
    evictBytes(need + kGpuReserve - f, keep);
}
static bool uploadFailed() {
    GLenum e = glGetError(), last = GL_NO_ERROR;
    while (e != GL_NO_ERROR) { last = e; e = glGetError(); }
    return last == GL_OUT_OF_MEMORY;
}

// Lectura + descompresion de un .vdx (sin GL: se usa tambien desde el hilo cargador). Devuelve false si no existe o
// esta danado. Bufer PROPIO por textura, liberado unos frames despues de subirlo: vitaGL puede copiar los datos de
// forma diferida, y al reutilizar un unico bufer la pagina siguiente sobrescribia a la anterior antes de llegar a la
// GPU (en la Vita salian trozos de otra textura: sprites "invisibles" o cambiados).
// Lectura de la tarjeta: cabecera + datos tal cual estan en el archivo (comprimidos o no)
static bool readVdxRaw(const std::string& path, uint32_t hdr[8], std::vector<unsigned char>& payload, double& readMs) {
    double t0 = nowMs();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::setvbuf(f, nullptr, _IONBF, 0); // lectura directa en bloques grandes (sin el buffer pequeno de newlib)
    if (std::fread(hdr, 4, 8, f) != 8) { std::fclose(f); return false; }
    bool known = !std::memcmp(hdr, "VDX4", 4) || !std::memcmp(hdr, "VDX3", 4) || !std::memcmp(hdr, "VDX2", 4);
    if (!known) { std::fclose(f); return false; }
    size_t n = hdr[7] ? hdr[7] : hdr[6];
    if (!hdr[7]) payload.reserve(n + lz4lite::kSlack);
    payload.resize(n);
    bool ok = std::fread(payload.data(), 1, n, f) == n;
    std::fclose(f);
    readMs += nowMs() - t0;
    return ok;
}

// Descompresion a bloques DXT (sin E/S: en el hilo descompresor, en paralelo con la lectura de la pagina siguiente)
static bool decodeVdx(const uint32_t hdr[8], std::vector<unsigned char>& payload, std::vector<unsigned char>& buf, double& inflMs) {
    if (!hdr[7]) { buf = std::move(payload); return buf.size() == hdr[6]; }
    double t1 = nowMs();
    bool ok;
    buf.reserve(hdr[6] + lz4lite::kSlack);   // VDX4: margen para las copias de 8 bytes, sin realojar
    buf.resize(hdr[6]);
    if (!std::memcmp(hdr, "VDX4", 4)) {   // VDX4: LZ4 (lz4lite.h), ~45 % menos bytes que VDX3
        buf.resize(hdr[6] + lz4lite::kSlack);
        ok = lz4lite::decompressFast(payload.data(), payload.size(), buf.data(), hdr[6]);
        buf.resize(hdr[6]);
    } else if (!std::memcmp(hdr, "VDX3", 4)) {
        size_t bs = hdr[5] == 0x83F0 || hdr[5] == 0x83F1 ? 8 : 16; // DXT1 : DXT5
        ok = unrle(payload.data(), payload.size(), buf.data(), buf.size(), bs);
    } else {
        uLongf len = (uLongf)buf.size();
        ok = uncompress(buf.data(), &len, payload.data(), (uLong)payload.size()) == Z_OK && len == buf.size();
    }
    inflMs += nowMs() - t1;
    std::vector<unsigned char>().swap(payload);
    return ok;
}

static bool readVdx(const std::string& path, uint32_t hdr[8], std::vector<unsigned char>& buf, double& readMs, double& inflMs) {
    std::vector<unsigned char> payload;
    return readVdxRaw(path, hdr, payload, readMs) && decodeVdx(hdr, payload, buf, inflMs);
}

// sube a GL un .vdx ya leido (hilo principal)
static bool uploadVdx(const std::string& path, const uint32_t hdr[8], std::vector<unsigned char>& buf, Tex& t, int& realW, int& realH) {
#if !defined(__vita__)
    if (!glCompressedTexImage2D) return false;
#endif
    double t2 = nowMs();
    makeRoom(buf.size(), &t);
    uploadFailed(); // limpiar errores previos
    GLuint id; glGenTextures(1, &id); texParams(id);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, (GLenum)hdr[5], (GLsizei)hdr[1], (GLsizei)hdr[2], 0, (GLsizei)hdr[6], buf.data());
    gPerf.uploadMs += nowMs() - t2;
    if (uploadFailed()) {
        glDeleteTextures(1, &id); gPerf.uploadFails++;
        gfxLog("[gfx] sin memoria de GPU para %s (%u KB, libre %u KB)\n", path.c_str(), (unsigned)(buf.size() >> 10), (unsigned)(gpuFree() >> 10));
        t.loaded = false; t.retryAt = gFrame + 30;
        return false;
    }
    t.id = id; t.w = (int)hdr[1]; t.h = (int)hdr[2]; t.bytes = buf.size(); t.loaded = true; t.filt = GL_NEAREST; t.wrap = GL_CLAMP_TO_EDGE;
    gUploadKeep.push_back({gFrame, std::move(buf)});
    realW = (int)hdr[3]; realH = (int)hdr[4];
    return true;
}

static bool loadVdx(const std::string& path, Tex& t, int& realW, int& realH) {
    uint32_t hdr[8]; std::vector<unsigned char> buf;
    if (!readVdx(path, hdr, buf, gPerf.readMs, gPerf.inflateMs)) return false;
    return uploadVdx(path, hdr, buf, t, realW, realH);
}

// ---------------------------------------------------------------- hilo cargador de paginas
// Leer una pagina de 2048x2048 de la tarjeta cuesta 70-90 ms en la Vita: hecho en el hilo principal eran tirones de
// 5 frames cada vez que aparecia un sprite nuevo. El hilo (nucleo 2) lee y descomprime; el principal solo sube a GL.
// Mientras la pagina no esta lista, lo que la usa no se dibuja (un par de frames).
struct LoadRes { int tex; uint32_t hdr[8]; std::vector<unsigned char> buf, payload; bool ok; double readMs, inflMs; };
static vs::SysMutex* gLMx = nullptr;
static vs::SysSema* gLSema = nullptr;
static std::deque<int> gLJobs;          // paginas pedidas (con gLMx)
static std::vector<LoadRes> gLDone;     // leidas, pendientes de subir (con gLMx)
static int gLBusy = -1;                 // pagina que el hilo esta leyendo ahora (con gLMx)
// Lectura y descompresion en hilos distintos (nucleo 2 lee, nucleo 3 descomprime): mientras se descomprime una pagina
// ya se esta leyendo la siguiente. En la Vita descomprimir una pagina VDX4 cuesta ~34 ms y leerla ~90 ms.
static std::deque<LoadRes> gDecQ;       // leidas, pendientes de descomprimir (con gLMx)
static int gDecBusy = -1;               // pagina que se esta descomprimiendo (con gLMx)
static vs::SysSema* gDecSema = nullptr;
static bool gDecThread = false;
static bool gAsync = false;
static bool gSyncDraw = false;
static size_t gPendBytes = 0;          // bytes de paginas pedidas aun no subidas          // primer frame de una sala: lo visible se carga ya (sin parpadeo)

static std::string pagePath(int tex) { char p[512]; std::snprintf(p, sizeof p, "%s/%d.vdx", gTexDir.c_str(), tex); return p; }

static void* loaderMain(void*) {
    vs::pinThisThread(0x40000 /* nucleo 2 */, 0x80000 /* nucleo 3 */);
    for (;;) {
        gLSema->wait();
        int tex;
        { vs::SysLock lk(*gLMx); if (gLJobs.empty()) continue; tex = gLJobs.front(); gLJobs.pop_front(); gLBusy = tex; }
        LoadRes r; r.tex = tex; r.readMs = r.inflMs = 0;
        r.ok = readVdxRaw(pagePath(tex), r.hdr, r.payload, r.readMs);
        if (gDecThread && r.ok) { { vs::SysLock lk(*gLMx); gDecQ.push_back(std::move(r)); gLBusy = -1; } gDecSema->post(); continue; }
        if (r.ok) r.ok = decodeVdx(r.hdr, r.payload, r.buf, r.inflMs);
        { vs::SysLock lk(*gLMx); gLDone.push_back(std::move(r)); gLBusy = -1; }
    }
    return nullptr;
}

static void* decoderMain(void*) {
    vs::pinThisThread(0x80000 /* nucleo 3 */, 0x20000 /* nucleo 1 */);
    for (;;) {
        gDecSema->wait();
        LoadRes r;
        { vs::SysLock lk(*gLMx); if (gDecQ.empty()) continue; r = std::move(gDecQ.front()); gDecQ.pop_front(); gDecBusy = r.tex; }
        r.ok = decodeVdx(r.hdr, r.payload, r.buf, r.inflMs);
        { vs::SysLock lk(*gLMx); gLDone.push_back(std::move(r)); gDecBusy = -1; }
    }
    return nullptr;
}

static void startLoader() {
    if (std::getenv("VS_SYNCTEX")) { gfxLog("[gfx] VS_SYNCTEX: carga de texturas en el hilo principal\n"); return; }
    gLMx = new vs::SysMutex(); gLSema = new vs::SysSema(); gDecSema = new vs::SysSema();
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 128 * 1024);
    // Descompresion en un hilo aparte: DESACTIVADA. Medido en la Vita (outer_slum_7 en frio): leer y descomprimir a la vez
    // en dos nucleos compite por la memoria (lectura 740 -> 1100 ms, descompresion 300 -> 400 ms, espera 1108 -> 1169 ms).
    constexpr bool kDecodeThread = false;
    pthread_t th2; gDecThread = kDecodeThread && pthread_create(&th2, &at, decoderMain, nullptr) == 0;
    if (gDecThread) pthread_detach(th2);
    pthread_t th; gAsync = pthread_create(&th, &at, loaderMain, nullptr) == 0;
    pthread_attr_destroy(&at);
    if (gAsync) pthread_detach(th);
    gfxLog("[gfx] cargador de texturas en segundo plano: %s (descompresion en otro hilo: %s)\n", gAsync ? "activo" : "NO", gDecThread ? "si" : "no");
}

static GLuint upload(const unsigned char* rgba, int w, int h) {
    GLuint id; glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return id;
}

void init(const std::string& texDir, size_t budget) {
    gTexDir = texDir; gBudget = budget;
    unsigned char w[4] = {255, 255, 255, 255};
    gWhite = upload(w, 1, 1);
    glDisable(GL_LIGHTING); glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
#if !defined(__vita__)
    pglBlendEquation = (PFN_BlendEquation)SDL_GL_GetProcAddress("glBlendEquation");
    pglCompressedTexImage2D = (PFN_CompTex)SDL_GL_GetProcAddress("glCompressedTexImage2D");
#endif
    gBatch.reserve(6 * 4096);
    { GLint mt = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mt); gMaxTex = mt > 0 ? std::min(4096, (int)mt) : 4096;
      gfxLog("[gfx] GL_MAX_TEXTURE_SIZE %d (se usa %d)\n", (int)mt, gMaxTex); }
    startLoader();
}

void setPageSize(int page, int w, int h) {
    if (page >= (int)gPages.size()) gPages.resize(page + 1);
    gPages[page].w = w; gPages[page].h = h;
}

// LRU: nunca expulsa paginas usadas en este frame o el anterior, ni la que se acaba de cargar (antes la recien
// cargada podia expulsarse a si misma al pasar el limite y se recargaba en cada draw -> 1 fps en dialogos).
// expulsa paginas no usadas en este frame (las mas antiguas primero) hasta liberar 'want' bytes
static void evictBytes(size_t want, const Tex* keep) {
    size_t freed = 0;
    while (freed < want) {
        Tex* oldest = nullptr;
        for (auto& t : gPages) if (t.loaded && &t != keep && t.lastUse < gFrame && (!oldest || t.lastUse < oldest->lastUse)) oldest = &t;
        if (!oldest) break;
        glDeleteTextures(1, &oldest->id); oldest->id = 0; oldest->loaded = false; oldest->filt = oldest->wrap = -1;
        gBytes -= oldest->bytes; freed += oldest->bytes;
        gPerf.evictions++;
    }
}

static void evict(const Tex* keep) {
    while (gBytes > gBudget) {
        Tex* oldest = nullptr;
        for (auto& t : gPages) if (t.loaded && &t != keep && t.lastUse + 1 < gFrame && (!oldest || t.lastUse < oldest->lastUse)) oldest = &t;
        if (!oldest) break;
        glDeleteTextures(1, &oldest->id); oldest->id = 0; oldest->loaded = false; oldest->filt = oldest->wrap = -1; gBytes -= oldest->bytes;
        gPerf.evictions++;
    }
}

static void loadPage(int tex, Tex& t) {
    double t0 = nowMs();
    char path[512];
    int rw, rh;
    std::snprintf(path, sizeof path, "%s/%d.vdx", gTexDir.c_str(), tex);
    if (!loadVdx(path, t, rw, rh)) {
        if (t.retryAt > gFrame) return;   // falta de memoria de GPU: se reintentara mas tarde
        std::snprintf(path, sizeof path, "%s/%d.png", gTexDir.c_str(), tex);
        int cw, ch, comp;
        unsigned char* px = stbi_load(path, &cw, &ch, &comp, 4);
        if (!px) { std::printf("[gfx] no se pudo cargar %s\n", path); t.failed = true; return; }
        t.id = upload(px, cw, ch); t.w = cw; t.h = ch; t.bytes = (size_t)cw * ch * 4; t.loaded = true;
        t.filt = GL_NEAREST; t.wrap = GL_CLAMP_TO_EDGE;
        stbi_image_free(px);
    }
    gBytes += t.bytes;
    t.lastUse = gFrame;
    evict(&t);
    gPerf.loads++; gPerf.loadMs += nowMs() - t0;
}

// sube una pagina leida por el hilo cargador
static void finishLoad(LoadRes& r) {
    if (r.tex < 0 || r.tex >= (int)gPages.size()) return;
    Tex& t = gPages[r.tex];
    if (t.pending) { t.pending = false; gPendBytes -= std::min(gPendBytes, (size_t)t.w * t.h); }
    if (t.loaded || t.failed) return;
    double t0 = nowMs();
    gPerf.readMs += r.readMs; gPerf.inflateMs += r.inflMs;   // tiempo del hilo cargador (no del principal)
    int rw, rh;
    if (!r.ok || !uploadVdx(pagePath(r.tex), r.hdr, r.buf, t, rw, rh)) {
        if (t.retryAt <= gFrame) loadPage(r.tex, t);   // sin .vdx: PNG en el hilo principal (solo datos antiguos)
        return;
    }
    gBytes += t.bytes;
    t.lastUse = gFrame;
    evict(&t);
    gPerf.loads++; gPerf.asyncLoads++; gPerf.loadMs += nowMs() - t0;
}

// recoge las paginas que el hilo ya leyo
static void pump() {
    if (!gAsync) return;
    std::vector<LoadRes> done;
    { vs::SysLock lk(*gLMx); done.swap(gLDone); }
    for (auto& r : done) finishLoad(r);
}

// urgent: se necesita para dibujar ahora (va delante de las precargas)
static void request(int tex, Tex& t, bool urgent = false) {
    if (t.pending) {
        if (!urgent) return;
        vs::SysLock lk(*gLMx);
        for (auto it = gLJobs.begin(); it != gLJobs.end(); ++it)
            if (*it == tex) { if (it != gLJobs.begin()) { gLJobs.erase(it); gLJobs.push_front(tex); } break; }
        return;
    }
    t.pending = true; gPendBytes += (size_t)t.w * t.h;
    { vs::SysLock lk(*gLMx); if (urgent) gLJobs.push_front(tex); else gLJobs.push_back(tex); }
    gLSema->post();
}

// Carga ya (lo visible al entrar en una sala). La pagina pasa al principio de la cola del hilo cargador y se espera:
// leerla aqui a la vez que el hilo lee otras hacia competir a los dos por la tarjeta (~245 ms por pagina en la Vita,
// frente a 70-90 ms leyendo de una en una).
static void loadNow(int tex, Tex& t) {
    {
        vs::SysLock lk(*gLMx);
        if (t.pending) { for (auto it = gLJobs.begin(); it != gLJobs.end(); ++it) if (*it == tex) { gLJobs.erase(it); break; } }
        else { t.pending = true; gPendBytes += (size_t)t.w * t.h; }
        bool read = gLBusy == tex || gDecBusy == tex;   // ya la esta leyendo/descomprimiendo, o falta subirla
        for (auto& r : gLDone) read |= r.tex == tex;
        for (auto& r : gDecQ) read |= r.tex == tex;
        if (!read) gLJobs.push_front(tex);
    }
    gLSema->post();
    double t0 = nowMs();
    while (t.pending && nowMs() - t0 < 3000) { vs::sysSleepMs(1); pump(); }
    gPerf.waitMs += nowMs() - t0;
    if (t.loaded) return;
    if (t.pending) { t.pending = false; gPendBytes -= std::min(gPendBytes, (size_t)t.w * t.h); }
    if (!t.failed && t.retryAt <= gFrame) loadPage(tex, t);
}

static Tex* find(int tex) {
    if (tex >= 0) {
        if (tex >= (int)gPages.size()) gPages.resize(tex + 1);
        Tex* t = &gPages[tex];
        if (!t->loaded && !t->failed && t->retryAt <= gFrame) {
            if (gAsync && !gSyncDraw) { request(tex, *t, true); gPerf.missing++; }
            else if (gAsync) loadNow(tex, *t);
            else loadPage(tex, *t);
        }
        return t->loaded ? t : nullptr;
    }
    auto it = gDyn.find(tex);
    return it == gDyn.end() ? nullptr : &it->second;
}

// 0 = la pagina aun no esta (no dibujar: antes se dibujaba con la textura blanca -> rectangulos blancos)
static GLuint resolve(int tex, int& w, int& h) {
    if (tex == -1) { w = h = 1; return gWhite; }
    Tex* t = find(tex);
    if (!t) { w = h = 1; return 0; }
    t->lastUse = gFrame; w = t->w; h = t->h;
    return t->id;
}

// Pide la pagina al hilo cargador (cambio de room, objetos creados): para cuando se vea, ya estara.
// No se pide si se pasaria del 85% del presupuesto o falta memoria de GPU: no se expulsa nada por adelantado.
void preload(int tex) {
    if (tex < 0) return;
    if (tex >= (int)gPages.size()) {   // las paginas reales se registran con setPageSize
        static bool warned = false;
        if (!warned) { warned = true; gfxLog("[gfx] preload: pagina %d fuera de rango (%d paginas)\n", tex, (int)gPages.size()); }
        return;
    }
    Tex& t = gPages[tex];
    if (t.loaded) { t.lastUse = gFrame; return; }
    if (t.failed || t.pending || t.retryAt > gFrame) return;
    size_t need = (size_t)t.w * t.h;
    // vglMemFree recorre todo el monton de vitaGL: llamado por cada tile al precargar las salas vecinas costaba 200-360 ms
    // de golpe (visto en el recorrido de la Vita). Se mide una vez por frame; gPendBytes ya descuenta lo pedido despues.
    static uint64_t freeFrame = ~0ull; static size_t freeCached = 0;
    if (freeFrame != gFrame) { freeFrame = gFrame; freeCached = gpuFree(); }
    if (freeCached < need + gPendBytes + 2 * kGpuReserve) return;
    if (gBytes + gPendBytes + need > gBudget / 100 * 85) return;
    if (gAsync) request(tex, t);
    else if (gSyncDraw) loadPage(tex, t);   // sin hilo: solo al cambiar de sala (como antes), nunca en mitad del juego
}

void roomStart() { gSyncDraw = true; }

int pageState(int tex) {
    if (tex < 0 || tex >= (int)gPages.size()) return 0;
    const Tex& t = gPages[tex];
    return t.loaded ? 1 : t.failed ? -1 : t.retryAt > gFrame ? -2 : 0;
}

bool texSize(int tex, int& w, int& h) {
    if (tex >= 0 && tex < (int)gPages.size()) { w = gPages[tex].w; h = gPages[tex].h; return true; }
    auto it = gDyn.find(tex);
    if (it != gDyn.end()) { w = it->second.w; h = it->second.h; return true; }
    w = h = 1; return false;
}

int loadDynamic(const std::string& path, int& w, int& h) {
    double t0 = nowMs();
    Tex t;
    if (!loadVdx(path + ".vdx", t, w, h)) {
        int comp; unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
        if (!px) return -1;
        t.id = upload(px, w, h); t.w = w; t.h = h; t.bytes = (size_t)w * h * 4; t.loaded = true;
        t.filt = GL_NEAREST; t.wrap = GL_CLAMP_TO_EDGE;
        stbi_image_free(px);
    }
    gPerf.loads++; gPerf.loadMs += nowMs() - t0;
    int handle = gNextDyn--;
    gDyn[handle] = t;
    return handle;
}

// Tira de animacion mas ancha que el maximo de la GPU (anim_kudir 6220 px, fx_life_blade 4235 px; la Vita admite
// 4096): se parte en trozos de frames enteros, cada uno su textura RGBA. handles[k] = textura del frame k, offs[k] = x
// del frame dentro de ella. Devuelve false si no hace falta partirla (o no se puede leer el PNG).

int maxTextureSize() { return gMaxTex; }
bool loadDynamicStrip(const std::string& path, int frames, std::vector<int>& handles, std::vector<int>& offs, int& w, int& h, int& chunkW) {
    int comp;
    if (!stbi_info(path.c_str(), &w, &h, &comp) || w <= gMaxTex || frames < 2) return false;
    int fw = w / frames, per = std::max(1, gMaxTex / std::max(1, fw));
    if (fw > gMaxTex) return false;
    double t0 = nowMs();
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!px) return false;
    handles.assign(frames, -1); offs.assign(frames, 0);
    chunkW = per * fw;
    std::vector<unsigned char> sub;
    for (int f0 = 0; f0 < frames; f0 += per) {
        int n = std::min(per, frames - f0), cw = n * fw;
        sub.resize((size_t)cw * h * 4);
        for (int y = 0; y < h; y++) std::memcpy(&sub[(size_t)y * cw * 4], px + ((size_t)y * w + (size_t)f0 * fw) * 4, (size_t)cw * 4);
        Tex t; t.id = upload(sub.data(), cw, h); t.w = cw; t.h = h; t.bytes = (size_t)cw * h * 4; t.loaded = true;
        t.filt = GL_NEAREST; t.wrap = GL_CLAMP_TO_EDGE;
        int handle = gNextDyn--;
        gDyn[handle] = t;
        for (int k = 0; k < n; k++) { handles[f0 + k] = handle; offs[f0 + k] = k * fw; }
    }
    stbi_image_free(px);
    gPerf.loads++; gPerf.loadMs += nowMs() - t0;
    gfxLog("[gfx] tira %s (%dx%d, %d frames) partida en trozos de %d px (maximo GPU %d)\n", path.c_str(), w, h, frames, chunkW, gMaxTex);
    return true;
}

void freeDynamic(int handle) {
    auto it = gDyn.find(handle);
    if (it == gDyn.end()) return;
    flush();
    glDeleteTextures(1, &it->second.id);
    gDyn.erase(it);
}

void stats(int& n, size_t& bytes) { n = 0; for (auto& t : gPages) n += t.loaded; bytes = gBytes; }

void setScreen(int pw, int ph, int lw, int lh) { gPixW = pw; gPixH = ph; gLogW = lw; gLogH = lh; }

void beginFrame() {
    gFrame++;
    // los datos de texturas subidas hace mas de 4 frames ya estan en la GPU
    while (!gUploadKeep.empty() && gUploadKeep.front().first + 4 < gFrame) gUploadKeep.erase(gUploadKeep.begin());
    pump();
    glViewport(0, 0, gPixW, gPixH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void clear(uint32_t c) {
    flush();
    glClearColor((c & 255) / 255.f, ((c >> 8) & 255) / 255.f, ((c >> 16) & 255) / 255.f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void setCamera(double vx, double vy, double vw, double vh, double px, double py, double pw, double ph, double angle) {
    flush();
    double sx = (double)gPixW / gLogW, sy = (double)gPixH / gLogH;
    int x0 = (int)std::lround(px * sx), y0 = (int)std::lround(py * sy);
    int x1 = (int)std::lround((px + pw) * sx), y1 = (int)std::lround((py + ph) * sy);
    glViewport(x0, gPixH - y1, x1 - x0, y1 - y0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0, gPixH - y1, x1 - x0, y1 - y0);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(vx, vx + vw, vy + vh, vy, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    if (angle != 0) {
        glTranslatef((float)(vx + vw / 2), (float)(vy + vh / 2), 0);
        glRotatef((float)angle, 0, 0, 1);
        glTranslatef((float)-(vx + vw / 2), (float)-(vy + vh / 2), 0);
    }
}

static void applyBlend() {
    glBlendFunc(gBlendSrc, gBlendDst);
#ifdef __vita__
    glBlendEquation(gBlendEq == 2 ? GL_MAX : gBlendEq == 3 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
#else
    if (pglBlendEquation) pglBlendEquation(gBlendEq == 2 ? GL_MAX : gBlendEq == 3 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
#endif
}

void setBlend(int mode) {
    flush();
    gBlendEq = 0;
    switch (mode) {
        case 1: gBlendSrc = GL_SRC_ALPHA; gBlendDst = GL_ONE; break;                  // bm_add
        case 2: gBlendSrc = GL_SRC_ALPHA; gBlendDst = GL_ONE_MINUS_SRC_COLOR; break;  // bm_max (GMS1)
        case 3: gBlendSrc = GL_ZERO; gBlendDst = GL_ONE_MINUS_SRC_COLOR; break;       // bm_subtract
        default: gBlendSrc = GL_SRC_ALPHA; gBlendDst = GL_ONE_MINUS_SRC_ALPHA; break;
    }
    applyBlend();
}

static GLenum gmBlend(int b) {
    switch (b) {
        case 1: return GL_ZERO; case 2: return GL_ONE; case 3: return GL_SRC_COLOR; case 4: return GL_ONE_MINUS_SRC_COLOR;
        case 5: return GL_SRC_ALPHA; case 6: return GL_ONE_MINUS_SRC_ALPHA; case 7: return GL_DST_ALPHA; case 8: return GL_ONE_MINUS_DST_ALPHA;
        case 9: return GL_DST_COLOR; case 10: return GL_ONE_MINUS_DST_COLOR; case 11: return GL_SRC_ALPHA_SATURATE;
        default: return GL_ONE;
    }
}

void setBlendExt(int s, int d) { flush(); gBlendEq = 0; gBlendSrc = gmBlend(s); gBlendDst = gmBlend(d); applyBlend(); }
void setFilter(bool linear) { if (linear != gLinear) { flush(); gLinear = linear; } }
void setRepeat(bool r) { if (r != gRepeat) { flush(); gRepeat = r; } }

void flush() {
    if (gBatch.empty()) return;
    int w, h; GLuint id = resolve(gBatchTex, w, h);
    Tex* t = gBatchTex == -1 ? nullptr : find(gBatchTex);
    glBindTexture(GL_TEXTURE_2D, id);
    GLint f = gLinear ? GL_LINEAR : GL_NEAREST;
    GLint wrap = gRepeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    if (!t || t->filt != f) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
        if (t) t->filt = f;
    }
    if (!t || t->wrap != wrap) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        if (t) t->wrap = wrap;
    }
    gPerf.draws++;
    glVertexPointer(2, GL_FLOAT, sizeof(Vtx), &gBatch[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vtx), &gBatch[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vtx), &gBatch[0].r);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)gBatch.size());
    gBatch.clear();
}

static inline Vtx mk(float x, float y, float u, float v, uint32_t c, float a) {
    a = std::max(0.f, std::min(1.f, a));
    return Vtx{x, y, u, v, (uint8_t)(c & 255), (uint8_t)((c >> 8) & 255), (uint8_t)((c >> 16) & 255), (uint8_t)std::lround(a * 255)};
}

static void bindBatch(int tex, int verts) {
    if (tex != gBatchTex || gBatch.size() + verts > 6 * 4096) { flush(); gBatchTex = tex; }
}

void quad(int tex, const float xy[8], const float uvPix[8], const uint32_t c[4], float alpha) {
    int w, h; if (!resolve(tex, w, h)) return;
    bindBatch(tex, 6);
    float iw = 1.f / w, ih = 1.f / h;
    Vtx v[4];
    for (int k = 0; k < 4; k++) v[k] = mk(xy[k * 2], xy[k * 2 + 1], uvPix[k * 2] * iw, uvPix[k * 2 + 1] * ih, c[k], alpha);
    gBatch.push_back(v[0]); gBatch.push_back(v[1]); gBatch.push_back(v[2]);
    gBatch.push_back(v[0]); gBatch.push_back(v[2]); gBatch.push_back(v[3]);
}

void tris(int tex, int n, const float* xy, const float* uv, const uint32_t* c, const float* a) {
    int w, h; if (!resolve(tex, w, h)) return;
    bindBatch(tex, n);
    float iw = 1.f / w, ih = 1.f / h;
    for (int k = 0; k < n; k++) gBatch.push_back(mk(xy[k * 2], xy[k * 2 + 1], uv ? uv[k * 2] * iw : 0, uv ? uv[k * 2 + 1] * ih : 0, c[k], a[k]));
}

void line(float x1, float y1, float x2, float y2, uint32_t c1, uint32_t c2, float alpha, float width) {
    float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    float xy[8] = {x1 + nx, y1 + ny, x2 + nx, y2 + ny, x2 - nx, y2 - ny, x1 - nx, y1 - ny};
    float uv[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t c[4] = {c1, c2, c2, c1};
    quad(-1, xy, uv, c, alpha);
}

void endFrame() {
    flush();
    gSyncDraw = false;
    glDisable(GL_SCISSOR_TEST);
}

} // namespace gfx

// Relleno: vita-elf-create.exe (vitasdk) se cae con 0xC0000374 con ciertas distribuciones de direcciones del binario
// (no depende del codigo). Si vuelve a fallar al convertir a vsvita.velf, cambia el tamano de este array.
extern "C" __attribute__((used)) const char vs_pad_probe[8192] = {1};
