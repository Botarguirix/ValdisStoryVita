// Traduccion de los textos del juego (menu de MODS: Idioma). El juego original solo esta en ingles y sus textos son
// constantes de cadena del codigo GML (tabla de cadenas del data.win). Aqui se sustituyen en esa tabla, antes de que se
// ejecute nada y cada vez que el juego vuelve al menu principal (game_restart), asi el cambio de idioma se aplica al
// salir al menu sin cerrar el juego.
//
// Archivo lang/es.txt (UTF-8): una linea por texto, "huella<TAB>traduccion". La huella es FNV-1a de 64 bits del texto
// original (16 cifras hex), asi el archivo no contiene el texto original. "\n" en la traduccion es un salto de linea
// real; '#' es el salto de linea de GMS, como en el original. Lineas vacias o que empiezan por "//" se ignoran.
// tools/lang_extract.py saca de tu copia la lista de textos visibles con sus huellas (los identificadores internos,
// claves de ini y cadenas comparadas en el codigo nunca se traducen).
#include "runtime.h"
#include "debugtour.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vs {

std::vector<std::string> gLangDirs;   // carpetas donde buscar lang/ (las pone cada plataforma)
static std::vector<std::string> gOrigStrings;
static int gLangApplied = -1;

static uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ull; }
    return h;
}

static int readModsLang(const std::string& saveDir) {
    int lang = 0;
    if (FILE* f = std::fopen((saveDir + "/mods.ini").c_str(), "r")) {
        char k[64]; int v;
        while (std::fscanf(f, " %63[^=]=%d", k, &v) == 2) if (std::string(k) == "lang") lang = v;
        std::fclose(f);
    }
    return lang;
}

static bool loadTable(const char* code, std::unordered_map<uint64_t, std::string>& out, std::string& from) {
    for (const std::string& d : gLangDirs) {
        std::string p = d + "/" + code + ".txt";
        FILE* f = std::fopen(p.c_str(), "rb");
        if (!f) continue;
        std::string line; int c;
        auto take = [&]() {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t tab = line.find('\t');
            if (line.size() > 17 && tab == 16 && line.compare(0, 2, "//") != 0) {
                uint64_t h = std::strtoull(line.substr(0, 16).c_str(), nullptr, 16);
                std::string t = line.substr(17), o; o.reserve(t.size());
                for (size_t i = 0; i < t.size(); i++) {
                    if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == 'n') { o += '\n'; i++; }
                    else if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == 't') { o += '\t'; i++; }
                    else o += t[i];
                }
                out[h] = o;
            }
            line.clear();
        };
        while ((c = std::fgetc(f)) != EOF) { if (c == '\n') take(); else line += (char)c; }
        take();
        std::fclose(f);
        from = p;
        return true;
    }
    return false;
}

// Textos que la partida guarda como texto (p. ej. el objetivo actual, global.mission, con ini_write_string): al leerlos
// con ini_read_string se pasan al idioma activo, para que una partida guardada en un idioma se vea bien en el otro.
static std::unordered_map<std::string, std::string> gToEs, gToEn;
// lang/es_draw.txt: textos que el juego tambien compara (zonas...): solo se traducen al dibujarlos
static std::unordered_map<uint64_t, std::string> gDrawTr;
static std::unordered_set<uint64_t> gEsOut;      // huellas de todo el texto ya en espanol (para el registro de abajo)
static std::unordered_set<uint64_t> gLoggedEn;   // textos en ingles ya anotados en el registro

// lang: 0 ingles (original), 1 espanol. Se llama al arrancar y en game_restart.
void langApply(Game& g, int lang) {
    if (gOrigStrings.empty()) gOrigStrings = g.d.strings;
    if (lang == gLangApplied) return;
    g.d.strings = gOrigStrings;
    std::unordered_map<uint64_t, std::string> tr; std::string from;
    bool have = loadTable("es", tr, from);
    gToEs.clear(); gToEn.clear();
    if (have) for (auto& o : gOrigStrings) { auto it = tr.find(fnv1a64(o)); if (it != tr.end()) { gToEs[o] = it->second; gToEn[it->second] = o; } }
    gDrawTr.clear(); gEsOut.clear();
    if (lang == 1) {
        std::string fromDraw;
        if (loadTable("es_draw", gDrawTr, fromDraw)) logf("[idioma] %d textos que se traducen al dibujarlos (%s)\n", (int)gDrawTr.size(), fromDraw.c_str());
        for (auto& kv : tr) gEsOut.insert(fnv1a64(kv.second));
        for (auto& kv : gDrawTr) gEsOut.insert(fnv1a64(kv.second));
    }
    int hits = 0;
    if (lang == 1) {
        if (!have) logf("[idioma] no se encontro lang/es.txt: el juego sigue en ingles\n");
        else {
            for (auto& s : g.d.strings) { auto it = tr.find(fnv1a64(s)); if (it != tr.end()) { s = it->second; hits++; } }
            logf("[idioma] espanol: %d de %d textos traducidos (%s)\n", hits, (int)tr.size(), from.c_str());
        }
    }
    // constantes internadas: se reconstruyen con el idioma nuevo (las viejas pueden seguir vivas en variables)
    g.strConst.clear();
    gLangApplied = lang;
}
void langTranslateRead(std::string& s) {
    if (gLangApplied == 1) { auto it = gToEs.find(s); if (it != gToEs.end()) s = it->second; }
    else if (gLangApplied == 0) { auto it = gToEn.find(s); if (it != gToEn.end()) s = it->second; }
}
// Textos visibles que el juego tambien usa como identificadores (secciones del ini de la partida): no se pueden cambiar
// en la tabla de cadenas, asi que solo se sustituyen al dibujarlos (drawText), con el juego en espanol.
// Ademas, con el juego en espanol, cada texto dibujado que siga en ingles se anota una vez en vsrunner.log
// ("[idioma] sin traducir: ...", hasta 400) para encontrar lo que falta jugando (23-11).
static bool looksEnglish(const std::string& s) {
    int run = 0, words = 0;
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { if (++run == 3) words++; }
        else if (c >= 0x80) return false;   // tildes o simbolos: ya es espanol o no es texto
        else run = 0;
    }
    return words >= 1;
}
const std::string* langDrawAlias(const std::string& s) {
    if (gLangApplied != 1 || s.empty()) return nullptr;
    uint64_t h = fnv1a64(s);
    auto it = gDrawTr.find(h);
    if (it != gDrawTr.end()) return &it->second;
    // solo si se dibuja al menos 20 veces: el efecto maquina de escribir dibuja frases a medias que cambian cada frame
    static std::unordered_map<uint64_t, int> seen;
    if (gLoggedEn.size() >= 400 || gEsOut.count(h) || gLoggedEn.count(h) || s.back() == ' ' || !looksEnglish(s)) return nullptr;
    if (seen.size() > 20000) seen.clear();
    if (++seen[h] == 20 && gLoggedEn.insert(h).second) {
        std::string one = s; for (char& c : one) if (c == '\n' || c == '\r') c = '|';
        logf("[idioma] sin traducir: %s\n", one.c_str());
    }
    return nullptr;
}
void langInit(Game& g) { langApply(g, readModsLang(g.saveDir)); }

} // namespace vs
