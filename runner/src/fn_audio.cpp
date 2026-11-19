// Funciones audio_* de GMS1 sobre el mezclador propio.
#include "runtime.h"
#include "audio.h"

namespace vs {

static const int kInstBase = 300000; // ids de instancias de sonido (no chocan con indices de sonido)
static std::map<int, int> gEmitterGain; // emitter -> gain*1000
static float gEmitter[64] = {};
static int gNextEmitter = 0;
static int gMusicVoice = 0; static float gMusicGain = 1;

static bool soundPath(Game& g, int snd, std::string& path, bool& ogg) {
    if (snd < 0 || snd >= (int)g.d.sounds.size()) return false;
    const Sound& s = g.d.sounds[snd];
    if (s.embedded >= 0) { path = g.dataDir + "/snd/" + std::to_string(s.embedded) + ".wav"; ogg = false; return true; }
    if (!s.file.empty()) {
        path = g.gameDir + "/" + s.file;
        ogg = s.file.size() > 4 && (s.file.substr(s.file.size() - 4) == ".ogg" || s.file.substr(s.file.size() - 4) == ".OGG");
        if (!ogg && s.file.find('.') == std::string::npos) { path += ".ogg"; ogg = true; }
        return true;
    }
    return false;
}

static int gDetId = 1;
static int playSnd(Game& g, int snd, bool loop, float gain) {
    if (g.det) return kInstBase + gDetId++;   // modo determinista: el audio no influye en la logica
    std::string path; bool ogg;
    if (!soundPath(g, snd, path, ogg)) { g.warnOnce("sonido invalido " + std::to_string(snd)); return -1; }
    float vol = g.d.sounds[snd].volume > 0 ? g.d.sounds[snd].volume : 1.0f;
    double t0 = Game::profNow();
    int v = audio::play(path, ogg, loop, gain * vol, 1.0f, snd);
    double ms = (Game::profNow() - t0) / 1000.0; g.audioMs += ms; g.audioCalls++;
    if (ms > 20 || g.perfVerbose) logf("[audio] play %.1f ms %s\n", ms, path.c_str());
    return v > 0 ? kInstBase + v : -1;
}

void Game::audioInit() { audio::init(); }
void Game::audioUpdate() {}

VS_FN(audio_play_sound) { return Value((double)playSnd(g, a[0].ival(), a[2].truthy(), 1.0f)); }
VS_FN(sound_play) { return Value((double)playSnd(g, a[0].ival(), false, 1.0f)); }
VS_FN(audio_play_sound_on) {
    int em = a[0].ival(); float gain = (em >= 0 && em < 64) ? gEmitter[em] : 1.0f;
    return Value((double)playSnd(g, a[1].ival(), a[2].truthy(), gain));
}
VS_FN(audio_play_music) {
    if (gMusicVoice) audio::stop(gMusicVoice - kInstBase);
    gMusicVoice = playSnd(g, a[0].ival(), a[1].truthy(), gMusicGain);
    return Value((double)gMusicVoice);
}
VS_FN(audio_stop_music) { if (gMusicVoice > 0) audio::stop(gMusicVoice - kInstBase); gMusicVoice = 0; return Value(); }
VS_FN(audio_music_gain) {
    gMusicGain = (float)a[0].num();
    if (gMusicVoice > 0) audio::setGain(gMusicVoice - kInstBase, gMusicGain, (int)a[1].num());
    return Value();
}
VS_FN(audio_stop_sound) {
    int id = a[0].ival();
    if (id >= kInstBase) audio::stop(id - kInstBase); else audio::stopTag(id);
    return Value();
}
VS_FN(audio_stop_all) { audio::stopAll(); gMusicVoice = 0; return Value(); }
VS_FN(audio_is_playing) {
    int id = a[0].ival();
    if (g.det) return Value(0.0);
    return Value((id >= kInstBase ? audio::playing(id - kInstBase) : audio::playingTag(id)) ? 1.0 : 0.0);
}
VS_FN(audio_sound_pitch) {
    int id = a[0].ival();
    if (id >= kInstBase) audio::setPitch(id - kInstBase, (float)a[1].num());
    return Value();
}
VS_FN(audio_sound_length) {
    std::string path; bool ogg; int id = a[0].ival();
    if (!soundPath(g, id, path, ogg)) return Value(0.0);
    double t0 = Game::profNow();
    float len = audio::lengthSeconds(path, ogg);
    double ms = (Game::profNow() - t0) / 1000.0; g.audioMs += ms; g.audioCalls++;
    if (ms > 20 || g.perfVerbose) logf("[audio] length %.1f ms %s\n", ms, path.c_str());
    return Value((double)len);
}
VS_FN(audio_emitter_create) { int e = gNextEmitter++ % 64; gEmitter[e] = 1; return Value((double)e); }
VS_FN(audio_emitter_gain) { int e = a[0].ival(); if (e >= 0 && e < 64) gEmitter[e] = (float)a[1].num(); return Value(); }
VS_FN(audio_emitter_position) { return Value(); }
VS_FN(audio_listener_position) { return Value(); }

} // namespace vs
