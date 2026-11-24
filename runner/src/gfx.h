// Renderer 2D: GL de funcion fija con arrays de cliente (desktop GL via SDL2 en PC, vitaGL en Vita).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gfx {

void init(const std::string& texDir, size_t budgetBytes);
void setScreen(int pixelW, int pixelH, int logicalW, int logicalH);
void beginFrame();
void clear(uint32_t bgrColor);
// view (mundo) -> port (coordenadas logicas de ventana)
void setCamera(double vx, double vy, double vw, double vh, double px, double py, double pw, double ph, double angle);
void setBlend(int mode);            // bm_normal 0, bm_add 1, bm_max 2, bm_subtract 3
void setBlendExt(int src, int dst); // bm_zero 1 .. bm_src_alpha_sat 11 (GMS)
void setFilter(bool linear);
void setRepeat(bool repeat);
// tex: >= 0 pagina, <= -1000 dinamica, -1 blanco. uv en pixeles de textura.
void quad(int tex, const float xy[8], const float uvPix[8], const uint32_t bgr[4], float alpha);
void tris(int tex, int n, const float* xy, const float* uvPix, const uint32_t* bgr, const float* alpha);
void line(float x1, float y1, float x2, float y2, uint32_t c1, uint32_t c2, float alpha, float width);
void flush();
void endFrame();
// Escala del HUD (9.4): mientras esta activa, lo que se dibuja se encoge hacia una esquina o el centro de la vista.
// hudAnchor elige el ancla por llamada de dibujo (segun su punto de origen); hudLock la fija durante un texto entero.
// Los quads que cubren mas del 60 % de la vista a lo ancho y a lo alto (velos de pantalla completa) no se escalan.
struct HudState { bool on = false; float s = 1, l = 0, t = 0, w = 1, h = 1, ax = 0, ay = 0; bool lock = false; };
HudState hudGet();
void hudSet(const HudState& st);
void hudAnchor(float x, float y);
void hudLock(bool on);

bool texSize(int tex, int& w, int& h);
int pageState(int tex);   // diagnostico: 1 cargada, 0 no, -1 fallida, -2 esperando reintento
int loadDynamic(const std::string& path, int& w, int& h); // -1 si falla
void freeDynamic(int handle);
int createDynamic(const unsigned char* rgba, int w, int h, bool linear);   // textura RGBA propia del runner
int maxTextureSize();
bool loadDynamicStrip(const std::string& path, int frames, std::vector<int>& handles, std::vector<int>& offs, int& w, int& h, int& chunkW);
void setPageSize(int page, int w, int h);
void stats(int& pagesLoaded, size_t& bytes);
void preload(int page);              // pide la pagina al hilo cargador (cambio de room, objetos nuevos)
void roomStart();                    // el siguiente frame carga ya lo visible (entrada a sala sin parpadeo)

struct Perf { int loads = 0, evictions = 0, draws = 0, uploadFails = 0, asyncLoads = 0, missing = 0; double waitMs = 0, loadMs = 0, readMs = 0, inflateMs = 0, uploadMs = 0, swapMs = 0; int gpuFreeMB = -1; };
Perf& perf();                        // contadores acumulados; el llamador los resetea
int gpuFreeMB();                     // memoria libre de vitaGL en MB (PC: -1)
void setDiscard(bool d);   // frame sin GPU: logica y eventos Draw si, dibujo no (ver gfx.cpp)
bool discarding();
extern bool gUploadDebug;   // anota cada subida de pagina (save/drawdebug)
void debugEvict(int tex);   // diagnostico: descarga una pagina
void flushLog();   // vuelca los mensajes guardados antes de tener gLogHook
extern void (*gLogHook)(const char*); // destino de los avisos de gfx (el runtime lo apunta a logf)

} // namespace gfx
