// Modos de prueba de rendimiento: recorrido de salas, estres con enemigos y modo sin morir (ver debugtour.cpp)
#pragma once
#include <cstdint>
#include <string>

namespace vs {
class Game;
void debugTourInit(Game& g, const std::string& saveDir);
void debugTourBeforeStep(Game& g);
void debugTourAfterFrame(Game& g, double stepMs, double drawMs, double frameMs);   // frameMs = tiempo real entre frames

// Menu de TRUCOS (SELECT en la Vita, F2 en PC). Botones: bit 0 arriba, 1 abajo, 2 izquierda, 3 derecha, 4 aceptar (X),
// 5 volver (O), 6 abrir/cerrar (SELECT), 7 L, 8 R. Devuelve true si el juego debe quedarse en pausa este frame; si el
// menu esta abierto (o se acaba de cerrar y siguen botones pulsados) borra la entrada del mando para el juego.
bool cheatsInput(Game& g, uint32_t buttons);
}
