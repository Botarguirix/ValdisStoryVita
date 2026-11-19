// Modos de prueba de rendimiento: recorrido de salas, estres con enemigos y modo sin morir (ver debugtour.cpp)
#pragma once
#include <string>

namespace vs {
class Game;
void debugTourInit(Game& g, const std::string& saveDir);
void debugTourBeforeStep(Game& g);
void debugTourAfterFrame(Game& g, double stepMs, double drawMs, double frameMs);   // frameMs = tiempo real entre frames
}
