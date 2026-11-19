// Mutex y semaforo que funcionan de verdad en la Vita.
// OJO: en el binario enlazado estaticamente de la Vita, std::mutex NO bloquea (libstdc++ cree que no hay hilos:
// __gthread_active_p() devuelve 0 porque el simbolo debil __pthread_key_create vale 0) y shared_ptr no usa
// operaciones atomicas. Para datos compartidos entre hilos se usan siempre estas clases.
#pragma once
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
namespace vs {
class SysMutex {
    SceKernelLwMutexWork w_;
public:
    SysMutex() { sceKernelCreateLwMutex(&w_, "vs_mutex", 0, 0, nullptr); }
    ~SysMutex() { sceKernelDeleteLwMutex(&w_); }
    void lock() { sceKernelLockLwMutex(&w_, 1, nullptr); }
    void unlock() { sceKernelUnlockLwMutex(&w_, 1); }
    SysMutex(const SysMutex&) = delete; SysMutex& operator=(const SysMutex&) = delete;
};
class SysSema {
    SceUID id_;
public:
    SysSema() { id_ = sceKernelCreateSema("vs_sema", 0, 0, 0x7fffffff, nullptr); }
    ~SysSema() { sceKernelDeleteSema(id_); }
    void post() { sceKernelSignalSema(id_, 1); }
    void wait() { sceKernelWaitSema(id_, 1, nullptr); }
};
inline void sysSleepMs(int ms) { sceKernelDelayThread((SceUInt)ms * 1000); }
// fija el hilo actual a un nucleo; prueba 'mask' y si no se permite (sin CapUnlocker para el nucleo 3) usa 'fallback'
inline void pinThisThread(int mask, int fallback) {
    if (sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), mask) < 0) sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), fallback);
}
// el nucleo 3 (SYSTEM) solo es usable con CapUnlocker: se prueba con un hilo temporal
inline bool coreAvailable(int mask) {
    SceUID t = sceKernelCreateThread("vs_probe", [](SceSize, void*) -> int { return 0; }, 0x10000100, 0x1000, 0, mask, nullptr);
    if (t < 0) return false;
    sceKernelDeleteThread(t); return true;
}
} // namespace vs
#else
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace vs {
class SysMutex {
    std::mutex m_;
public:
    void lock() { m_.lock(); }
    void unlock() { m_.unlock(); }
};
class SysSema {
    std::mutex m_; std::condition_variable cv_; int n_ = 0;
public:
    void post() { { std::lock_guard<std::mutex> lk(m_); n_++; } cv_.notify_one(); }
    void wait() { std::unique_lock<std::mutex> lk(m_); cv_.wait(lk, [&] { return n_ > 0; }); n_--; }
};
inline void sysSleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void pinThisThread(int, int) {}
} // namespace vs
#endif

namespace vs {
class SysLock {
    SysMutex& m_;
public:
    explicit SysLock(SysMutex& m) : m_(m) { m_.lock(); }
    ~SysLock() { m_.unlock(); }
    SysLock(const SysLock&) = delete; SysLock& operator=(const SysLock&) = delete;
};
} // namespace vs
