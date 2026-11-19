#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <pthread.h>
#include <memory>
#include "sysmutex.h"
#include <vector>
#include <vorbis/vorbisfile.h>

namespace audio {

struct Pcm { int rate = 44100, ch = 2; std::vector<int16_t> data; size_t frames() const { return data.size() / ch; } };

struct Voice {
    int id = 0, tag = -1; bool loop = false, done = false;
    float gain = 1, target = 1, gainStep = 0, pitch = 1;
    // memoria
    const Pcm* pcm = nullptr; double pos = 0;   // propiedad de gCache (nunca se libera)
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
static int gNextId = 1;
static float gMaster = 1;

void init() {}
void setMasterGain(float g) { gMaster = g; }

// lee y decodifica un wav del disco (sin tocar estructuras compartidas)
static Pcm* readWav(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    std::vector<uint8_t> b; std::fseek(f, 0, SEEK_END); b.resize(std::ftell(f)); std::fseek(f, 0, SEEK_SET);
    size_t rd = std::fread(b.data(), 1, b.size(), f); std::fclose(f);
    if (rd != b.size() || b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return nullptr;
    std::unique_ptr<Pcm> p(new Pcm());
    int bits = 16; size_t o = 12;
    while (o + 8 <= b.size()) {
        uint32_t sz; std::memcpy(&sz, &b[o + 4], 4);
        if (!std::memcmp(&b[o], "fmt ", 4)) { uint16_t c, bt; uint32_t r; std::memcpy(&c, &b[o + 10], 2); std::memcpy(&r, &b[o + 12], 4); std::memcpy(&bt, &b[o + 22], 2); p->ch = c; p->rate = r; bits = bt; }
        else if (!std::memcmp(&b[o], "data", 4)) {
            size_t n = std::min<size_t>(sz, b.size() - o - 8);
            if (bits == 16) { p->data.resize(n / 2); std::memcpy(p->data.data(), &b[o + 8], (n / 2) * 2); }
            else if (bits == 8) { p->data.resize(n); for (size_t i = 0; i < n; i++) p->data[i] = (int16_t)((b[o + 8 + i] - 128) << 8); }
            break;
        }
        o += 8 + sz + (sz & 1);
    }
    if (p->ch < 1 || p->ch > 2) return nullptr;
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
    if (it != gCache.end()) return it->second;
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
    if (!p) {   // duracion pedida antes de cargarlo: lectura directa (sin cachear, el cargador puede estar con el)
        Pcm* tmp = readWav(path);
        float s = tmp ? (float)tmp->frames() / tmp->rate : 0;
        delete tmp;
        return s;
    }
    return (float)p->frames() / p->rate;
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
