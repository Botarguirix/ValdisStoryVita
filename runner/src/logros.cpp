// Logros locales (9.10). El juego solo da logros si Steam esta activo (steam_start_script: steam_initialised() y
// steam_stats_ready() -> global.steam_api = 1) y entonces llama a steam_get/set_achievement y steam_get/set_stat_int.
// Aqui esas funciones guardan en save/logros.ini, en la propia consola. La nube, las tablas de puntuacion y las
// capturas siguen desactivadas (las otras funciones steam_* de fn_core.cpp devuelven "sin datos").
// Se guarda con bgio: durante el Boss Rush (modo aislado) un logro se ve pero no se escribe en la tarjeta.
#include "runtime.h"
#include "bgio.h"
#include <cstdlib>
#include <map>
#include <vector>
#include <string>

namespace vs {
extern std::string gSaveDir;
void (*gLogroToast)(const std::string&) = nullptr;   // debugtour.cpp: aviso en pantalla

namespace {
struct LogroDef { const char* id; const char* nombre; };
// Nombres propios del port (descriptivos), en espanol latino
const LogroDef kLogros[] = {
    {"vsac_ach_azu", "Vencer a Azudor"}, {"vsac_ach_yeti", "Vencer al Yeti"}, {"vsac_ach_tagu", "Vencer a Tagu"},
    {"vsac_ach_ruck", "Vencer a Ruckjahl"}, {"vsac_ach_shu", "Vencer a Shu"}, {"vsac_ach_dist", "Vencer a Dhistahn"},
    {"vsac_ach_lozzo", "Vencer a Lozzo"}, {"vsac_ach_raven", "Vencer al Cuervo"}, {"vsac_ach_magus", "Vencer a Magus"},
    {"vsac_ach_manawar", "Vencer al Manawar"}, {"vsac_ach_jaz", "Vencer a Jahzracht"}, {"vsac_ach_evil", "Vencer a Dhistahn poseído"},
    {"vsac_ach_mines", "Superar las minas"}, {"vsac_ach_treu", "Vencer a Treu e Itahn"}, {"vsac_ach_gernot", "Vencer a Gernot"},
    {"vsac_ach_abby", "Vencer a Abbigail"}, {"vsac_ach_alagath", "Vencer al Ojo de Alagath"}, {"vsac_ach_myrgato", "Vencer al Ojo de Myrgato"},
    {"vsac_ach_anemone", "Vencer a Anemone"}, {"vsac_ach_trueanemone", "Vencer a la verdadera Anemone"}, {"vsac_ach_ragano", "Vencer a Ragano"},
    {"vsac_ach_leilodin", "Vencer a Leilodin"}, {"vsac_ach_abom", "Vencer a la Abominación"}, {"vsac_ach_stigandr", "Vencer a Stigandr"},
    {"vsac_ach_king", "Vencer al rey"}, {"vsac_ach_queen", "Vencer a la reina"}, {"vsac_ach_merc", "Vencer al mercenario"},
    {"vsac_ach_end_wyatt", "Final de Wyatt"}, {"vsac_ach_end_reina", "Final de Reina"}, {"vsac_ach_end_vladyn", "Final de Vladyn"},
    {"vsac_ach_end_gilda", "Final de Gilda"}, {"vsac_ach_gs_wyatt", "Wyatt en Matadioses"}, {"vsac_ach_gs_reina", "Reina en Matadioses"},
    {"vsac_ach_gs_vladyn", "Vladyn en Matadioses"}, {"vsac_ach_gs_gilda", "Gilda en Matadioses"}, {"vsac_ach_godslayer", "Matadioses"},
    {"vsac_ach_goibniu", "Modo Goibniu"}, {"vsac_ach_gladiator", "Gladiador de la Arena"}, {"vsac_ach_diver", "Piso 30 de la Arena"},
    {"vsac_ach_nosave", "Sin guardar"}, {"vsac_ach_speed", "Partida rápida"}, {"vsac_ach_perfect", "Combate perfecto"},
    {"vsac_ach_hplow", "Al borde de la muerte"}, {"vsac_ach_retry40", "40 reintentos"}, {"vsac_ach_thirsty", "Sediento"},
    {"vsac_ach_combo10", "Combo de 10"}, {"vsac_ach_combo20", "Combo de 20"}, {"vsac_ach_combo35", "Combo de 35"},
    {"vsac_ach_combo50", "Combo de 50"}, {"vsac_ach_combo100", "Combo de 100"}, {"vsac_ach_dmg125", "125 de daño"},
    {"vsac_ach_dmg250", "250 de daño"}, {"vsac_ach_dmg500", "500 de daño"}, {"vsac_ach_dmg1000", "1000 de daño"},
    {"vsac_ach_str20", "Fuerza 20"}, {"vsac_ach_int20", "Inteligencia 20"}, {"vsac_ach_agi20", "Agilidad 20"},
    {"vsac_ach_luck20", "Suerte 20"}, {"vsac_ach_focus", "Concentración"}, {"vsac_ach_fin", "Remate"},
    {"vsac_ach_wep", "Todas las armas"}, {"vsac_ach_item100", "Todos los objetos"}, {"vsac_ach_map100", "Mapa completo"},
    {"vsac_ach_crew100", "Toda la tripulación"}, {"vsac_ach_civ100", "Todos los civiles"}, {"vsac_ach_books", "Todos los libros"},
    {"vsac_ach_doll", "La muñeca"}, {"vsac_ach_pos", "Posesión"},
};
const int kNumLogros = sizeof(kLogros) / sizeof(kLogros[0]);
std::map<std::string, int> gHave, gStats;
bool gLoaded = false;
std::string path() { return gSaveDir + "/logros.ini"; }
void load() {
    if (gLoaded) return;
    gLoaded = true;
    std::string text; if (!bgio::read(path(), text)) return;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p); std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
        p = e == std::string::npos ? text.size() : e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t q = line.find('='); if (q == std::string::npos) continue;
        std::string k = line.substr(0, q); int v = std::atoi(line.c_str() + q + 1);
        if (k.compare(0, 5, "stat.") == 0) gStats[k.substr(5)] = v; else gHave[k] = v;
    }
}
void save() {
    std::string o = "; Logros locales de Valdis Story PSVITA (9.10)\n";
    for (auto& [k, v] : gHave) if (v) o += k + "=1\n";
    for (auto& [k, v] : gStats) o += "stat." + k + "=" + std::to_string(v) + "\n";
    bgio::write(path(), o);
}
const char* nombre(const std::string& id) { for (auto& l : kLogros) if (id == l.id) return l.nombre; return nullptr; }
} // namespace

// Aviso "¡Logro desbloqueado!" arriba al centro durante 4 s (como en DDVita); varios seguidos hacen cola
void overlayBegin(Game& g);
void overlaySize(int& w, int& h);
void overlayRect(float x1, float y1, float x2, float y2, uint32_t bgr, float alpha);
void overlayText(Game& g, double x, double y, const std::string& s, double scale, uint32_t bgr, double alpha);
static std::vector<std::string> gToastQueue; static int gToastFrames = 0;
void logrosToastPush(const std::string& name) { gToastQueue.push_back(name); }
bool logrosOverlay(Game& g) {
    if (gToastFrames <= 0) { if (gToastQueue.empty()) return false; gToastFrames = 240; }
    gToastFrames--;
    const std::string name = gToastQueue.front();
    if (gToastFrames == 0) gToastQueue.erase(gToastQueue.begin());
    overlayBegin(g);
    int W, H; overlaySize(W, H);
    float a = gToastFrames > 220 ? (240 - gToastFrames) / 20.f : gToastFrames < 20 ? gToastFrames / 20.f : 1.f;
    float x0 = W * 0.5f - 230, x1 = W * 0.5f + 230, y0 = 10, y1 = 66;
    overlayRect(x0, y0, x1, y1, 0x201010, 0.9f * a);
    overlayRect(x0, y0, x1, y0 + 3, 0x00CCFF, a);
    overlayText(g, x0 + 16, y0 + 8, "¡Logro desbloqueado!", 0.4, 0x00CCFF, a);
    overlayText(g, x0 + 16, y0 + 30, name, 0.45, 0xFFFFFF, a);
    return true;
}

int logrosTotal() { return kNumLogros; }
int logrosHechos() { load(); int n = 0; for (auto& l : kLogros) if (gHave.count(l.id) && gHave[l.id]) n++; return n; }
bool logroInfo(int k, std::string& name) { load(); if (k < 0 || k >= kNumLogros) return false; name = kLogros[k].nombre; return gHave.count(kLogros[k].id) && gHave[kLogros[k].id]; }

VS_FN(steam_initialised) { static const bool off = std::getenv("VS_NOSTEAM") != nullptr; return Value(off ? 0.0 : 1.0); }   // VS_NOSTEAM: pruebas
VS_FN(steam_stats_ready) { return Value(1.0); }
VS_FN(steam_get_achievement) { load(); auto it = gHave.find(a[0].toString()); return Value(it != gHave.end() && it->second ? 1.0 : 0.0); }
VS_FN(steam_set_achievement) {
    load();
    std::string id = a[0].toString();
    if (gHave[id]) return Value();
    gHave[id] = 1; save();
    const char* nm = nombre(id);
    std::string msg = std::string("Logro desbloqueado: ") + (nm ? nm : id.c_str());
    logf("[logro] %s (%s)\n", msg.c_str(), id.c_str());
    logrosToastPush(nm ? nm : id);
    return Value();
}
VS_FN(steam_get_stat_int) { load(); auto it = gStats.find(a[0].toString()); return Value(it != gStats.end() ? (double)it->second : 0.0); }
VS_FN(steam_set_stat_int) {
    load();
    std::string k = a[0].toString(); int v = a[1].ival();
    if (gStats[k] != v) { gStats[k] = v; save(); }
    return Value();
}
} // namespace vs
