// Prueba de compresion de paginas .vdx (VDX3) con lz4lite: tamano, verificacion con liblz4 y tiempo de descompresion.
#include "../src/lz4lite.h"
#include <lz4.h>
#include <lz4hc.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>

static bool unrle(const unsigned char* src, size_t srcLen, unsigned char* dst, size_t dstLen, size_t bs) {
    const unsigned char* end = src + srcLen; size_t o = 0;
    while (src + 4 <= end) {
        uint32_t tok; std::memcpy(&tok, src, 4); src += 4;
        size_t n = tok & 0x7fffffff;
        if (tok & 0x80000000u) { if (src + bs > end || o + n * bs > dstLen) return false; for (size_t k = 0; k < n; k++, o += bs) std::memcpy(dst + o, src, bs); src += bs; }
        else { if (src + n * bs > end || o + n * bs > dstLen) return false; std::memcpy(dst + o, src, n * bs); src += n * bs; o += n * bs; }
    }
    return o == dstLen;
}
// separa los campos de cada bloque DXT en planos (bs=8: [4 color][4 indices]; bs=16: [8 alfa][4 color][4 indices])
static void split(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, size_t bs) {
    size_t nb = in.size() / bs; out.resize(in.size());
    std::vector<size_t> parts = bs == 8 ? std::vector<size_t>{4, 4} : std::vector<size_t>{2, 6, 4, 4};
    size_t o = 0, fo = 0;
    for (size_t p : parts) { for (size_t b = 0; b < nb; b++) { std::memcpy(&out[o], &in[b * bs + fo], p); o += p; } fo += p; }
}
static double now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("uso: lz4test carpeta_tex\n"); return 1; }
    DIR* d = opendir(argv[1]); if (!d) return 1;
    size_t tRle = 0, tRaw = 0, tMine = 0, tHc = 0, tSplit = 0; double decMs = 0, decFastMs = 0; int nf = 0, bad = 0;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name; if (n.size() < 5 || n.substr(n.size() - 4) != ".vdx") continue;
        std::string p = std::string(argv[1]) + "/" + n;
        FILE* f = std::fopen(p.c_str(), "rb"); if (!f) continue;
        std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        std::vector<uint8_t> file(sz); std::fread(file.data(), 1, sz, f); std::fclose(f);
        uint32_t hdr[8]; std::memcpy(hdr, file.data(), 32);
        if (std::memcmp(hdr, "VDX3", 4)) continue;
        size_t bs = hdr[5] == 0x83F0 || hdr[5] == 0x83F1 ? 8 : 16;
        std::vector<uint8_t> raw(hdr[6]);
        if (hdr[7]) { if (!unrle(file.data() + 32, hdr[7], raw.data(), raw.size(), bs)) { bad++; continue; } }
        else std::memcpy(raw.data(), file.data() + 32, raw.size());
        std::vector<uint8_t> mine; lz4lite::compress(raw.data(), raw.size(), mine);
        std::vector<uint8_t> chk(raw.size());
        int r = LZ4_decompress_safe((const char*)mine.data(), (char*)chk.data(), (int)mine.size(), (int)chk.size());
        if (r != (int)raw.size() || chk != raw) { std::printf("FALLO liblz4 %s\n", n.c_str()); bad++; }
        std::fill(chk.begin(), chk.end(), 0);
        double t0 = now();
        bool ok = lz4lite::decompress(mine.data(), mine.size(), chk.data(), chk.size());
        decMs += now() - t0;
        if (!ok || chk != raw) { std::printf("FALLO lz4lite %s\n", n.c_str()); bad++; }
        std::vector<uint8_t> chk2(raw.size() + lz4lite::kSlack, 0);
        double t1 = now();
        bool ok2 = lz4lite::decompressFast(mine.data(), mine.size(), chk2.data(), raw.size());
        decFastMs += now() - t1;
        if (!ok2 || std::memcmp(chk2.data(), raw.data(), raw.size())) { std::printf("FALLO rapido %s\n", n.c_str()); bad++; }
        std::vector<char> hc(LZ4_compressBound((int)raw.size()));
        int hn = LZ4_compress_HC((const char*)raw.data(), hc.data(), (int)raw.size(), (int)hc.size(), 9);
        std::vector<uint8_t> sp, spc; split(raw, sp, bs); lz4lite::compress(sp.data(), sp.size(), spc);
        tRle += sz; tRaw += raw.size(); tMine += mine.size() + 32; tHc += hn + 32; tSplit += spc.size() + 32; nf++;
    }
    closedir(d);
    std::printf("%d paginas, %d fallos | crudo %.1f MB | VDX3 (RLE) %.1f MB | lz4lite %.1f MB | lz4 HC %.1f MB | lz4lite+planos %.1f MB | descompr. %.0f ms total, rapida %.0f ms\n",
                nf, bad, tRaw / 1e6, tRle / 1e6, tMine / 1e6, tHc / 1e6, tSplit / 1e6, decMs, decFastMs);
    return bad ? 1 : 0;
}
