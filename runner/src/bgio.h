// Escritura diferida de guardados y log. El juego guarda un .ini de ~40 KB en cada cambio de sala
// (quick_save_script: file_delete + ini_close + file_rename); en la tarjeta de la Vita eso bloqueaba
// el frame 1-2 s. Aqui las operaciones se aplican al instante sobre una vista en memoria y un hilo
// las vuelca a disco en el mismo orden.
#pragma once
#include <string>

namespace bgio {
bool exists(const std::string& path);
bool read(const std::string& path, std::string& out);
void write(const std::string& path, std::string data);
bool remove(const std::string& path);
bool rename(const std::string& from, const std::string& to);
void appendLog(const std::string& path, const char* text); // se abre con "w" la primera vez
void flush();                                                // espera a que todo este en disco
// Modo aislado (Boss Rush): las escrituras, borrados y renombrados de guardados solo cambian la vista en memoria;
// al desactivarlo se olvidan y los archivos se vuelven a leer de disco (las partidas del usuario no se tocan).
void setSandbox(bool on);
bool sandbox();
}
