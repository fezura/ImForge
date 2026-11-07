#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if __has_include("miniz.h")
#include "miniz.h"
#define IMFORGE_PNG_HAS_MINIZ 1
#else
#define IMFORGE_PNG_HAS_MINIZ 0
#endif

namespace imforge::builder::png {

inline uint32_t Crc32(const unsigned char* p, size_t n, uint32_t crc = 0) {
    struct Table {
        uint32_t v[256];
        Table() {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                v[i] = c;
            }
        }
    };
    static const Table table;
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table.v[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

inline uint32_t Adler32(const unsigned char* p, size_t n) {
    uint32_t a = 1, b = 0;
    while (n > 0) {
        size_t chunk = n < 5552 ? n : 5552;
        n -= chunk;
        while (chunk-- > 0) {
            a += *p++;
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}

inline void PutU32BE(std::string& out, uint32_t v) {
    out.push_back((char)((v >> 24) & 0xFF));
    out.push_back((char)((v >> 16) & 0xFF));
    out.push_back((char)((v >> 8) & 0xFF));
    out.push_back((char)(v & 0xFF));
}

inline std::string ZlibStore(const std::vector<unsigned char>& raw) {
    std::string z;
    z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    z.push_back((char)0x78);
    z.push_back((char)0x01);
    size_t pos = 0;
    do {
        size_t len = raw.size() - pos;
        if (len > 65535) len = 65535;
        bool last = pos + len == raw.size();
        z.push_back((char)(last ? 1 : 0));
        z.push_back((char)(len & 0xFF));
        z.push_back((char)((len >> 8) & 0xFF));
        z.push_back((char)(~len & 0xFF));
        z.push_back((char)((~len >> 8) & 0xFF));
        if (len) z.append(reinterpret_cast<const char*>(raw.data() + pos), len);
        pos += len;
    } while (pos < raw.size());
    PutU32BE(z, Adler32(raw.data(), raw.size()));
    return z;
}

inline std::string Zlib(const std::vector<unsigned char>& raw) {
#if IMFORGE_PNG_HAS_MINIZ
    mz_ulong cap = mz_compressBound((mz_ulong)raw.size());
    std::string z(cap, '\0');
    if (mz_compress2(reinterpret_cast<unsigned char*>(&z[0]), &cap, raw.data(), (mz_ulong)raw.size(), 6) == MZ_OK) {
        z.resize(cap);
        return z;
    }
#endif
    return ZlibStore(raw);
}

inline void PutChunk(std::string& out, const char type[4], const std::string& data) {
    PutU32BE(out, (uint32_t)data.size());
    std::string body(type, 4);
    body += data;
    out += body;
    PutU32BE(out, Crc32(reinterpret_cast<const unsigned char*>(body.data()), body.size()));
}

inline std::string EncodeRGBA(const unsigned char* rgba, int w, int h) {
    const size_t stride = (size_t)w * 4;
    std::vector<unsigned char> raw;
    raw.reserve((stride + 1) * (size_t)h);
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = rgba + stride * (size_t)y;
        raw.push_back(1);
        for (size_t i = 0; i < stride; ++i) raw.push_back((unsigned char)(row[i] - (i >= 4 ? row[i - 4] : 0)));
    }
    std::string out("\x89PNG\r\n\x1a\n", 8);
    std::string ihdr;
    PutU32BE(ihdr, (uint32_t)w);
    PutU32BE(ihdr, (uint32_t)h);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    PutChunk(out, "IHDR", ihdr);
    PutChunk(out, "IDAT", Zlib(raw));
    PutChunk(out, "IEND", std::string());
    return out;
}

inline bool WriteRGBA(const std::string& path, const unsigned char* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    std::string data = EncodeRGBA(rgba, w, h);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace imforge::builder::png
