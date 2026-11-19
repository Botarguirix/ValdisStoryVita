// Herramienta de prueba (PC): convierte data.win con el convertidor del runner.
// vsconv vsd <data.win> <carpeta_vsdata>      solo game.vsd + audio
// vsconv all <data.win> <juego_steam> <salida> conversion completa (como el primer arranque en la Vita)
// vsconv vdx4 <vsdata> <juego>                 recomprime texturas VDX2/VDX3 a VDX4 (como la Vita al arrancar)
#include "../src/convert.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    std::string err;
    auto prog = [](const char* what, float f) { std::printf("\r[%3d%%] %-60s", (int)(f * 100), what); std::fflush(stdout); };
    if (argc >= 4 && !std::strcmp(argv[1], "vsd")) {
        if (!vs::convertVsd(argv[2], argv[3], err)) { std::printf("error: %s\n", err.c_str()); return 1; }
        return 0;
    }
    if (argc >= 5 && !std::strcmp(argv[1], "all")) {
        if (!vs::convertGame(argv[2], argv[3], argv[4], prog, err)) { std::printf("\nerror: %s\n", err.c_str()); return 1; }
        std::printf("\nlisto\n");
        return 0;
    }
    if (argc >= 4 && !std::strcmp(argv[1], "vdx4")) {
        if (!vs::upgradeVdx(argv[2], argv[3], prog, err)) { std::printf("\nerror: %s\n", err.c_str()); return 1; }
        std::printf("\nlisto\n");
        return 0;
    }
    std::printf("uso: vsconv vsd <data.win> <vsdata> | vsconv all <data.win> <steam> <salida> | vsconv vdx4 <vsdata> <juego>\n");
    return 1;
}
