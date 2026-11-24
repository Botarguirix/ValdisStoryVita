#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <map>
#include <pthread.h>
#include <memory>
#include "sysmutex.h"
#include <vector>
#include <vorbis/vorbisfile.h>

namespace audio {

// Muestras en memoria propia (malloc): si no hay memoria, el sonido se salta en vez de cerrar el juego (con
// -fno-exceptions un std::vector sin memoria aborta). Ver el volcado del 22-11: el hilo cargador se cayo leyendo un wav.
struct Pcm {
    int rate = 44100, ch = 2; int16_t* data = nullptr; size_t samples = 0; uint64_t lastUse = 0;
    ~Pcm() { std::free(data); }
    size_t frames() const { return samples / ch; }
    size_t bytes() const { return samples * 2; }
};

struct Voice {
    int id = 0, tag = -1; bool loop = false, done = false;
    float gain = 1, target = 1, gainStep = 0, pitch = 1;
    // memoria
    const Pcm* pcm = nullptr; double pos = 0;   // propiedad de gCache (no se libera mientras una voz lo use)
    bool pending = false;                       // wav aun cargandose en segundo plano (suena en silencio hasta entonces)
    bool opening = false;                       // ogg abriendose en el hilo cargador: no se libera hasta que acabe
    // streaming
    OggVorbis_File vf; bool ogg = false; int oggCh = 2, oggRate = 44100;
    std::vector<int16_t> buf; size_t bufPos = 0; double frac = 0;
    std::string path;
};

// gMx protege gVoices (hilo de audio + hilo principal). Sin std::mutex/shared_ptr: en la Vita no son seguros (sysmutex.h)
static vs::SysMutex gMx;
static std::vector<std::unique_ptr<Voice>> gVoices;
static std::map<std::string, Pcm*> gCache;   // protegido por gMx (lo rellena el hilo cargador); nullptr = fallo
// Tope de la cache de efectos: los 137 wav suman ~37 MB y antes no se liberaba ninguno (en una sesion larga se juntaban
// todos y, con el heap a 108 MB, faltaba memoria). Se libera el menos usado que no este sonando.
static const size_t kWavBudget = 16u * 1024 * 1024;
static uint64_t gUseTick = 0;   // con gMx
static int gNextId = 1;
static float gMaster = 1;

void init() {}
void setMasterGain(float g) { gMaster = g; }

// lee y decodifica un wav del disco (sin tocar estructuras compartidas). Lee los datos directamente a su sitio:
// antes se cargaba el archivo entero y luego se copiaba (el doble de memoria durante la carga).
static Pcm* readWav(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    uint8_t h[12];
    if (std::fread(h, 1, 12, f) != 12 || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVE", 4)) { std::fclose(f); return nullptr; }
    std::unique_ptr<Pcm> p(new (std::nothrow) Pcm());
    if (!p) { std::fclose(f); return nullptr; }
    int bits = 16; bool ok = false; uint8_t c[8];
    while (std::fread(c, 1, 8, f) == 8) {
        uint32_t sz; std::memcpy(&sz, c + 4, 4);
        if (!std::memcmp(c, "fmt ", 4)) {
            uint8_t fm[16] = {}; size_t got = std::fread(fm, 1, std::min<uint32_t>(sz, 16), f);
            if (got >= 16) { uint16_t ch, bt; uint32_t r; std::memcpy(&ch, fm + 2, 2); std::memcpy(&r, fm + 4, 4); std::memcpy(&bt, fm + 14, 2); p->ch = ch; p->rate = (int)r; bits = bt; }
            if (sz > got) std::fseek(f, (long)(sz - got + (sz & 1)), SEEK_CUR); else if (sz & 1) std::fseek(f, 1, SEEK_CUR);
        } else if (!std::memcmp(c, "data", 4)) {
            size_t n = bits == 16 ? sz / 2 : sz;   // muestras
            p->data = (int16_t*)std::malloc(n * 2 + 2);
            if (!p->data) { std::fclose(f); std::printf("[audio] sin memoria para %s (%u KB)\n", path.c_str(), (unsigned)(n * 2 / 1024)); return nullptr; }
            if (bits == 16) p->samples = std::fread(p->data, 2, n, f);
            else if (bits == 8) {
                uint8_t tmp[4096]; size_t done = 0;
                while (done < n) {
                    size_t k = std::fread(tmp, 1, std::min<size_t>(sizeof tmp, n - done), f);
                    if (!k) break;
                    for (size_t i = 0; i < k; i++) p->data[done + i] = (int16_t)((tmp[i] - 128) << 8);
                    done += k;
                }
                p->samples = done;
            }
            ok = true; break;
        } else std::fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
    }
    std::fclose(f);
    if (!ok || p->ch < 1 || p->ch > 2) return nullptr;
    return p.release();
}

// Cargador en segundo plano: leer un wav de la tarjeta costaba 30-250 ms y congelaba el juego la primera vez que
// sonaba cada efecto. Ahora play() no espera: la voz empieza en cuanto el archivo esta en memoria.
static vs::SysMutex& loadMx() { static vs::SysMutex m; return m; }
static vs::SysSema& loadSema() { static vs::SysSema s; return s; }
struct LoadJob { std::string path; Voice* ogg = nullptr; };   // ogg != nullptr: abrir ese ogg (la voz sigue viva: opening)
static std::vector<LoadJob> gLoadQueue;  // protegido por loadMx
static bool openOgg(Voice& v);
static bool gLoader = false;

static void* loaderThread(void*) {
#ifdef __vita__
    vs::pinThisThread(0x80000 /* nucleo 3, CapUnlocker */, 0x40000 /* nucleo 2 */);
#endif
    for (;;) {
        loadSema().wait();
        LoadJob job;
        { vs::SysLock lk(loadMx()); if (gLoadQueue.empty()) continue; job = gLoadQueue.front(); gLoadQueue.erase(gLoadQueue.begin()); }
        if (job.ogg) {
            // el mezclador no toca la voz mientras pending/opening (nextFrame devuelve silencio)
            bool ok = openOgg(*job.ogg);
            vs::SysLock lk(gMx);
            job.ogg->pending = false; job.ogg->opening = false;
            if (!ok) { job.ogg->done = true; std::printf("[audio] no se pudo abrir %s\n", job.ogg->path.c_str()); }
            continue;
        }
        const std::string& path = job.path;
        Pcm* p = readWav(path);
        vs::SysLock lk(gMx);
        gCache[path] = p;
        if (p) {
            p->lastUse = ++gUseTick;
            size_t total = 0; for (auto& kv : gCache) if (kv.second) total += kv.second->bytes();
            while (total > kWavBudget) {
                auto victim = gCache.end();
                for (auto it = gCache.begin(); it != gCache.end(); ++it) {
                    Pcm* q = it->second; if (!q || q == p) continue;
                    bool inUse = false; for (auto& v : gVoices) if (v->pcm == q) { inUse = true; break; }
                    if (!inUse && (victim == gCache.end() || q->lastUse < victim->second->lastUse)) victim = it;
                }
                if (victim == gCache.end()) break;
                total -= victim->second->bytes(); delete victim->second; gCache.erase(victim);
            }
        }
        for (auto& v : gVoices) if (v->pending && v->path == path) { v->pending = false; v->pcm = p; if (!p) v->done = true; }
        if (!p) std::printf("[audio] wav invalido %s\n", path.c_str());
    }
    return nullptr;
}

// con gMx tomado: devuelve el pcm si ya esta, o encola la carga (true en 'queued')
// con loadMx tomado
static bool startLoader() {
    if (!gLoader) {
        loadSema();   // construir antes de que exista el hilo
        pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 128 * 1024);
        pthread_t th; gLoader = pthread_create(&th, &at, loaderThread, nullptr) == 0;
        pthread_attr_destroy(&at);
        if (gLoader) pthread_detach(th);
    }
    return gLoader;
}

static const Pcm* requestWav(const std::string& path, bool& queued) {
    queued = false;
    auto it = gCache.find(path);
    if (it != gCache.end()) { if (it->second) it->second->lastUse = ++gUseTick; return it->second; }
    {
        vs::SysLock lk(loadMx());
        if (!startLoader()) { Pcm* p = readWav(path); gCache[path] = p; return p; }   // sin hilo: sincrono
        bool dup = false;
        for (auto& j : gLoadQueue) dup |= !j.ogg && j.path == path;
        if (!dup) gLoadQueue.push_back({path, nullptr});
    }
    loadSema().post();
    queued = true;
    return nullptr;
}

// ogg como flujo NO buscable: ov_fopen recorria el archivo para medirlo (~1 s por musica en la tarjeta de la Vita).
// Para repetir en bucle se vuelve a abrir desde el principio.
static size_t oggRead(void* p, size_t s, size_t n, void* ds) { return std::fread(p, s, n, (FILE*)ds); }
static int oggClose(void* ds) { return std::fclose((FILE*)ds); }
static bool openOgg(Voice& v) {
    FILE* f = std::fopen(v.path.c_str(), "rb");
    if (!f) return false;
    std::setvbuf(f, nullptr, _IOFBF, 64 * 1024);
    ov_callbacks cb{oggRead, nullptr, oggClose, nullptr};
    if (ov_open_callbacks(f, &v.vf, nullptr, 0, cb) != 0) { std::fclose(f); return false; }
    vorbis_info* vi = ov_info(&v.vf, -1); v.oggCh = vi->channels; v.oggRate = (int)vi->rate; v.ogg = true;
    return true;
}

// Limite de voces (como los canales de GameMaker): si un efecto se repite sin parar (o hay muchisimos a la vez) la
// mezcla crece sin limite. Se corta el efecto mas antiguo que no este en bucle. Con gMx tomado.
static const int kMaxVoices = 64;
static void capVoices() {
    int n = 0; for (auto& v : gVoices) n += !v->done;
    if (n < kMaxVoices) return;
    for (auto& v : gVoices) if (!v->done && !v->loop && !v->opening) { v->done = true; break; }
}

int play(const std::string& path, bool isOgg, bool loop, float gain, float pitch, int tag) {
    auto v = std::make_unique<Voice>();
    v->loop = loop; v->gain = v->target = gain; v->pitch = pitch; v->tag = tag; v->path = path;
    // ogg: abrirlo (leer cabeceras de la tarjeta, 20-60 ms en la Vita) en el hilo cargador; suena en cuanto este
    if (isOgg) {
        bool started; { vs::SysLock lk(loadMx()); started = startLoader(); }
        if (started) {
            v->pending = v->opening = true;
            Voice* raw = v.get();
            int id;
            { vs::SysLock lk(gMx); capVoices(); id = v->id = gNextId++; gVoices.push_back(std::move(v)); }
            { vs::SysLock lk(loadMx()); gLoadQueue.push_back({path, raw}); }
            loadSema().post();
            return id;
        }
        if (!openOgg(*v)) { std::printf("[audio] no se pudo abrir %s\n", path.c_str()); return 0; }
    }
    vs::SysLock lk(gMx);
    if (!isOgg) {
        bool queued;
        v->pcm = requestWav(path, queued);
        if (queued) v->pending = true;
        else if (!v->pcm) return 0;   // fallo ya conocido
    }
    capVoices();
    v->id = gNextId++;
    int id = v->id;
    gVoices.push_back(std::move(v));
    return id;
}

static Voice* find(int id) { for (auto& v : gVoices) if (v->id == id && !v->done) return v.get(); return nullptr; }
void stop(int id) { vs::SysLock lk(gMx); if (Voice* v = find(id)) v->done = true; }
void stopTag(int tag) { vs::SysLock lk(gMx); for (auto& v : gVoices) if (v->tag == tag) v->done = true; }
void stopAll() { vs::SysLock lk(gMx); for (auto& v : gVoices) v->done = true; }
bool playing(int id) { vs::SysLock lk(gMx); return find(id) != nullptr; }
bool playingTag(int tag) { vs::SysLock lk(gMx); for (auto& v : gVoices) if (v->tag == tag && !v->done) return true; return false; }
void setGain(int id, float g, int ms) {
    vs::SysLock lk(gMx);
    if (Voice* v = find(id)) { v->target = g; if (ms <= 0) { v->gain = g; v->gainStep = 0; } else v->gainStep = (g - v->gain) / (ms * kRate / 1000.0f); }
}
void setPitch(int id, float p) { vs::SysLock lk(gMx); if (Voice* v = find(id)) v->pitch = std::max(0.01f, p); }

float lengthSeconds(const std::string& path, bool isOgg) {
    static std::map<std::string, float> lenCache;   // solo hilo principal; ov_fopen recorre el ogg entero (~1 s en la Vita)
    auto lc = lenCache.find(path);
    if (lc != lenCache.end()) return lc->second;
    if (isOgg) { OggVorbis_File vf; if (ov_fopen(path.c_str(), &vf) != 0) return 0; double t = ov_time_total(&vf, -1); ov_clear(&vf); return lenCache[path] = (float)t; }
    const Pcm* p = nullptr;
    {
        vs::SysLock lk(gMx);
        auto it = gCache.find(path);
        if (it != gCache.end()) p = it->second;
    }
    if (!p) {   // duracion pedida antes de cargarlo: solo la cabecera (leer el wav entero costaba 0,5-0,8 s en la Vita)
        float s = 0;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            uint8_t h[12]; int ch = 0, bits = 16; uint32_t rate = 0;
            if (std::fread(h, 1, 12, f) == 12 && !std::memcmp(h, "RIFF", 4) && !std::memcmp(h + 8, "WAVE", 4)) {
                uint8_t c[8];
                while (std::fread(c, 1, 8, f) == 8) {
                    uint32_t sz; std::memcpy(&sz, c + 4, 4);
                    if (!std::memcmp(c, "fmt ", 4)) {
                        uint8_t fm[16] = {};
                        size_t got = std::fread(fm, 1, std::min<uint32_t>(sz, 16), f);
                        uint16_t cc, bt; std::memcpy(&cc, fm + 2, 2); std::memcpy(&rate, fm + 4, 4); std::memcpy(&bt, fm + 14, 2);
                        ch = cc; bits = bt;
                        std::fseek(f, (long)(sz - got + (sz & 1)), SEEK_CUR);
                    } else if (!std::memcmp(c, "data", 4)) {
                        if (ch >= 1 && rate > 0) s = (float)((double)sz / (ch * (bits == 8 ? 1 : 2)) / rate);
                        break;
                    } else std::fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
                }
            }
            std::fclose(f);
        }
        return lenCache[path] = s;
    }
    return lenCache[path] = (float)p->frames() / p->rate;
}

// lee un frame (L,R) de una voz; false si termino
static bool nextFrame(Voice& v, float& l, float& r) {
    if (v.pending) { l = r = 0; return true; }   // esperando al cargador
    if (!v.ogg) {
        const Pcm& p = *v.pcm; size_t n = p.frames();
        if (n == 0) return false;
        if (v.pos >= n) { if (!v.loop) return false; v.pos = std::fmod(v.pos, (double)n); }
        size_t i = (size_t)v.pos;
        if (p.ch == 1) { l = r = p.data[i] / 32768.f; } else { l = p.data[i * 2] / 32768.f; r = p.data[i * 2 + 1] / 32768.f; }
        v.pos += (double)p.rate / kRate * v.pitch;
        return true;
    }
    // ogg: buffer decodificado + avance fraccional
    while (v.bufPos + v.oggCh > v.buf.size()) {
        v.buf.resize(4096 * v.oggCh);
        int bs; long got = ov_read(&v.vf, (char*)v.buf.data(), (int)(v.buf.size() * 2), 0, 2, 1, &bs);
        if (got <= 0) {
            if (v.loop) { ov_clear(&v.vf); v.ogg = false; if (openOgg(v)) { v.buf.clear(); v.bufPos = 0; continue; } }
            return false;
        }
        v.buf.resize(got / 2); v.bufPos = 0;
    }
    if (v.oggCh == 1) { l = r = v.buf[v.bufPos] / 32768.f; } else { l = v.buf[v.bufPos] / 32768.f; r = v.buf[v.bufPos + 1] / 32768.f; }
    v.frac += (double)v.oggRate / kRate * v.pitch;
    while (v.frac >= 1.0) { v.frac -= 1.0; v.bufPos += v.oggCh; if (v.bufPos + v.oggCh > v.buf.size()) break; }
    return true;
}

void mix(int16_t* out, int frames) {
    static std::vector<float> acc; acc.assign(frames * 2, 0.f);
    // El candado solo se toma un momento para copiar la lista de voces y al final para limpiar. Antes se mezclaba
    // (y se decodificaba el ogg) con el candado tomado: cada play()/playing()/setGain del hilo principal esperaba a
    // que terminara la mezcla; con muchas voces eran 22-35 ms por llamada (visto en la Vita).
    // Seguro porque solo este hilo borra voces, y las que estan pending/opening (las toca el cargador) no se mezclan.
    static std::vector<Voice*> snap;
    {
        vs::SysLock lk(gMx);
        snap.clear();
        for (auto& vp : gVoices) if (!vp->done && !vp->pending) snap.push_back(vp.get());
    }
    for (Voice* vp : snap) {
        Voice& v = *vp;
        for (int i = 0; i < frames; i++) {
            if (v.done) break;   // stop() desde el hilo principal
            float l, r;
            if (!nextFrame(v, l, r)) { v.done = true; break; }
            if (v.gainStep != 0) { v.gain += v.gainStep; if ((v.gainStep > 0 && v.gain >= v.target) || (v.gainStep < 0 && v.gain <= v.target)) { v.gain = v.target; v.gainStep = 0; } }
            acc[i * 2] += l * v.gain; acc[i * 2 + 1] += r * v.gain;
        }
    }
    {
        vs::SysLock lk(gMx);
        // limpiar voces terminadas
        for (auto it = gVoices.begin(); it != gVoices.end();) {
            if ((*it)->done && !(*it)->opening) { if ((*it)->ogg) ov_clear(&(*it)->vf); it = gVoices.erase(it); } else ++it;
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        float s = acc[i] * gMaster;
        s = std::max(-1.f, std::min(1.f, s));
        out[i] = (int16_t)(s * 32767);
    }
}

} // namespace audio
