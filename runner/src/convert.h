// Conversion de los archivos ORIGINALES de Steam a los datos del runner, ejecutable en la propia consola.
// Equivale a tools/build_vsdata.py (+ tools/export_vsdata.csx): data.win -> game.vsd + snd/*.wav + tex/*.vdx,
// y las imagenes data_*/ *.png -> *.png.vdx. Asi no hace falta un PC para preparar los datos.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <string>

namespace vs {

// progreso: texto de la fase y fraccion global 0..1
using ConvertProgress = std::function<void(const char* what, float frac)>;

// dataWin: ruta de data.win. srcGameDir: carpeta con data_*, *.ogg del juego original.
// outDir: se crea outDir/vsdata y outDir/game. Devuelve false y rellena err si falla.
bool convertGame(const std::string& dataWin, const std::string& srcGameDir, const std::string& outDir,
                 const ConvertProgress& progress, std::string& err);

// Solo la parte de data.win -> game.vsd + audio (sin texturas), util para pruebas
bool convertVsd(const std::string& dataWin, const std::string& vsdataDir, std::string& err);

// PNG (en memoria) -> .vdx DXT1/DXT5 con RLE. pot: ajustar a potencia de dos (imagenes dinamicas).
bool pngToVdx(const unsigned char* png, size_t pngLen, const std::string& outPath, bool pot, std::string& err);

// VDX4 (bloques DXT + LZ4). hdr: cabecera de 8 palabras (se rellenan magia y tamanos).
bool writeVdx4(const std::string& outPath, uint32_t hdr[8], const std::vector<uint8_t>& blocks, std::string& err);
// Datos convertidos con versiones anteriores (VDX2/VDX3): se recomprimen a VDX4 una vez (marca vsdata/vdx4.ok).
bool vdx4Ready(const std::string& vsdataDir);
bool upgradeVdx(const std::string& vsdataDir, const std::string& gameDir, const ConvertProgress& progress, std::string& err);

} // namespace vs
