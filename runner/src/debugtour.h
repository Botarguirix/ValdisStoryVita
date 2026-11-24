// Modos de prueba de rendimiento: recorrido de salas, estres con enemigos y modo sin morir (ver debugtour.cpp)
#pragma once
#include <cstdint>
#include <string>

namespace vs {
class Game;
void debugTourInit(Game& g, const std::string& saveDir);
void debugTourBeforeStep(Game& g);
void debugTourAfterFrame(Game& g, double stepMs, double drawMs, double frameMs);   // frameMs = tiempo real entre frames

// Menus del runner. SELECT (soltar sin tocar L/R) abre MODS; SELECT + L + R abre TRUCOS (F3 / F2 en PC).
// Botones: bit 0 arriba, 1 abajo, 2 izquierda, 3 derecha, 4 aceptar (X), 5 volver (O), 6 SELECT, 7 L, 8 R. Devuelve
// true si el juego debe quedarse en pausa este frame; si un menu esta abierto (o se acaba de cerrar y siguen botones
// pulsados) borra la entrada del mando para el juego.
bool cheatsInput(Game& g, uint32_t buttons);

// Opciones del menu de MODS (save/mods.ini): no son trampas, son ajustes del port.
struct ModsCfg {
    int lang = 0;    // 0 ingles (original), 1 espanol
    int font = 0;    // 0 original; fuente del sistema de la Vita: 1 negrita, 2 normal, 3 estrecha, 4 con remates (9.9)
    int tsize = 100; // tamano del texto en % (9.9): 100, 90, 80, 70
    int icons = 1;   // 1 iconos de botones de la Vita, 0 nombres originales (mando de Xbox)
    int touch = 1;   // panel trasero: 0 apagado, 1 solo arriba (como Shotgun King Vita), 2 entero (mitades)
    int hud = 50;    // tamano del HUD en % (9.4): 100 original, 50 por defecto (pedido del usuario: al menos 50 % mas pequeno)
};
extern ModsCfg gMods;
// Archivo de prueba en save/ con su nombre exacto o con sufijo "_xxx" antes de la extension (ver debugtour.cpp)
bool testFileExists(const std::string& saveDir, const std::string& base);
// Botones de hombro del panel trasero para un toque en (x, y) del panel (0..1919 x 0..1087): 1 = L1, 2 = R1, 0 = nada
int rearTouchZone(int x, int y);
}
