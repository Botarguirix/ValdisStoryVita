#include "bgio.h"
#include "sysmutex.h"
#include <cstdio>
#include <deque>
#include <map>
#include <pthread.h>
#include <string>
#include <sys/stat.h>

// Nota: sin std::mutex ni shared_ptr (en la Vita no son seguros entre hilos, ver sysmutex.h).
// Cada tarea lleva su propia copia de los datos.
namespace bgio {

namespace {
struct Entry { bool exists = false; bool hasData = false; std::string data; };
enum class Op { Write, Remove, Rename, Log };
struct Task { Op op; std::string a, b; };

vs::SysMutex& viewMx() { static vs::SysMutex m; return m; }       // vista en memoria (hilo principal)
std::map<std::string, Entry>& view() { static std::map<std::string, Entry> v; return v; }

vs::SysMutex& qMx() { static vs::SysMutex m; return m; }
vs::SysSema& qSema() { static vs::SysSema s; return s; }
std::deque<Task> gQueue;           // protegido por qMx
int gPending = 0;                  // tareas encoladas + en curso (protegido por qMx)
bool gWorker = false;

bool diskExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

bool diskRead(const std::string& p, std::string& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t rd = n > 0 ? std::fread(&out[0], 1, (size_t)n, f) : 0;
    std::fclose(f);
    out.resize(rd);
    return true;
}

void run(const Task& t) {
    static std::map<std::string, FILE*> logs;   // solo lo usa el hilo de escritura
    switch (t.op) {
    case Op::Write: {
        FILE* f = std::fopen(t.a.c_str(), "wb");
        if (f) { std::fwrite(t.b.data(), 1, t.b.size(), f); std::fclose(f); }
        break;
    }
    case Op::Remove: std::remove(t.a.c_str()); break;
    case Op::Rename: std::remove(t.b.c_str()); std::rename(t.a.c_str(), t.b.c_str()); break;
    case Op::Log: {
        FILE*& f = logs[t.a];
        if (!f) f = std::fopen(t.a.c_str(), "w");
        if (f) { std::fputs(t.b.c_str(), f); std::fflush(f); }
        break;
    }
    }
}

void* worker(void*) {
#ifdef __vita__
    vs::pinThisThread(0x80000 /* nucleo 3, CapUnlocker */, 0x40000 /* nucleo 2 */);
#endif
    for (;;) {
        qSema().wait();
        Task t;
        {
            vs::SysLock lk(qMx());
            if (gQueue.empty()) continue;
            t = std::move(gQueue.front()); gQueue.pop_front();
            // juntar lineas de log consecutivas en una sola escritura
            while (t.op == Op::Log && !gQueue.empty() && gQueue.front().op == Op::Log && gQueue.front().a == t.a) {
                t.b += gQueue.front().b; gQueue.pop_front(); gPending--;
            }
        }
        run(t);
        { vs::SysLock lk(qMx()); gPending--; }
    }
    return nullptr;
}

void push(Task t) {
    bool sync = false;
    {
        vs::SysLock lk(qMx());
        if (!gWorker) {
            qSema();   // construir el semaforo antes de que exista el hilo (los static locales no son seguros entre hilos aqui)
            // pila explicita: la de pthread por defecto en la Vita es pequena
            pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 256 * 1024);
            pthread_t th; gWorker = pthread_create(&th, &at, worker, nullptr) == 0;
            pthread_attr_destroy(&at);
            if (gWorker) pthread_detach(th); else sync = true;
        }
        if (!sync) { gQueue.push_back(std::move(t)); gPending++; }
    }
    if (sync) run(t); else qSema().post();
}

// entrada de la vista; si no se conoce, se consulta el disco (y se cachea la existencia). Con viewMx tomado.
Entry& entry(const std::string& p) {
    auto& v = view();
    auto it = v.find(p);
    if (it != v.end()) return it->second;
    Entry& e = v[p];
    e.exists = diskExists(p);
    return e;
}
} // namespace

bool exists(const std::string& path) { vs::SysLock lk(viewMx()); return entry(path).exists; }

bool read(const std::string& path, std::string& out) {
    vs::SysLock lk(viewMx());
    Entry& e = entry(path);
    if (!e.exists) return false;
    if (!e.hasData) {
        // aun no esta en memoria: no puede haber escrituras pendientes de este archivo, se lee de disco
        if (!diskRead(path, e.data)) { e.exists = false; return false; }
        e.hasData = true;
    }
    out = e.data;
    return true;
}

void write(const std::string& path, std::string data) {
    {
        vs::SysLock lk(viewMx());
        Entry& e = view()[path]; e.exists = true; e.hasData = true; e.data = data;
    }
    push({Op::Write, path, std::move(data)});
}

bool remove(const std::string& path) {
    {
        vs::SysLock lk(viewMx());
        Entry& e = entry(path);
        if (!e.exists) return false;
        e.exists = false; e.hasData = false; std::string().swap(e.data);
    }
    push({Op::Remove, path, ""});
    return true;
}

bool rename(const std::string& from, const std::string& to) {
    {
        vs::SysLock lk(viewMx());
        Entry& src = entry(from);
        if (!src.exists) return false;
        std::string data; bool has = src.hasData;
        if (has) data.swap(src.data); else has = diskRead(from, data);
        src.exists = false; src.hasData = false; std::string().swap(src.data);
        Entry& dst = view()[to]; dst.exists = true; dst.hasData = has; dst.data.swap(data);
    }
    push({Op::Rename, from, to});
    return true;
}

void appendLog(const std::string& path, const char* text) { push({Op::Log, path, text}); }

void flush() {
    for (;;) {
        { vs::SysLock lk(qMx()); if (!gWorker || gPending == 0) return; }
        vs::sysSleepMs(2);
    }
}

} // namespace bgio
