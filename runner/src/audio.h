// Mezclador software 44.1 kHz estereo s16. La plataforma llama a audio::mix desde su hilo de audio.
#pragma once
#include <cstdint>
#include <string>

namespace audio {

constexpr int kRate = 44100;

void init();
// devuelve id de voz (>0) o 0 si falla. stream=true -> OGG en streaming desde archivo
int play(const std::string& path, bool isOgg, bool loop, float gain, float pitch, int tag);
void stop(int voice);
void stopTag(int tag);          // todas las voces de un sonido
void stopAll();
bool playing(int voice);
bool playingTag(int tag);
void setGain(int voice, float gain, int fadeMs);
void setPitch(int voice, float pitch);
float lengthSeconds(const std::string& path, bool isOgg);
void mix(int16_t* out, int frames);
void setMasterGain(float g);

} // namespace audio
