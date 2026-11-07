#pragma once

// pairing crypto: SHA-256, HMAC-SHA256, OS random, secure wipe, codes and handshake MACs (no imgui)

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#ifdef SetProp
#undef SetProp
#endif
#ifdef RemoveProp
#undef RemoveProp
#endif
#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#if defined(__linux__) && defined(__has_include)
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#define IMFORGE_PAIRING_HAS_GETRANDOM 1
#endif
#endif
#endif

namespace imforge {
namespace pairing {

constexpr const char kCodeAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
// 32 Crockford chars = 160 bits. Nobody types it (it comes from the pairing file), so it's sized
// against offline guessing from a sniffed challenge, not for humans.
constexpr size_t kCodeChars = 32;
constexpr size_t kCodeGroup = 4;
constexpr size_t kNonceBytes = 16;
constexpr size_t kNonceHexChars = kNonceBytes * 2;
constexpr size_t kMacHexChars = 64;
constexpr const char* kKeyLabel = "imforge-pair-v1";
constexpr const char* kBuilderLabel = "imforge-builder";
constexpr const char* kAppLabel = "imforge-app";

inline void SecureWipe(void* p, size_t n) {
    if (!p || n == 0) return;
#ifdef _WIN32
    SecureZeroMemory(p, n);
#else
    volatile unsigned char* v = static_cast<volatile unsigned char*>(p);
    while (n--) *v++ = 0;
#endif
}

inline void SecureWipe(std::string& s) {
    if (s.capacity() > 0) {
        s.resize(s.capacity());
        SecureWipe(&s[0], s.size());
    }
    s.clear();
}

inline bool ConstantTimeEqual(const void* a, const void* b, size_t n) {
    const volatile unsigned char* x = static_cast<const volatile unsigned char*>(a);
    const volatile unsigned char* y = static_cast<const volatile unsigned char*>(b);
    unsigned char diff = 0;
    for (size_t i = 0; i < n; ++i) diff = (unsigned char)(diff | (x[i] ^ y[i]));
    return diff == 0;
}

inline bool ConstantTimeEqual(const std::string& a, const std::string& b) {
    return a.size() == b.size() && ConstantTimeEqual(a.data(), b.data(), a.size());
}

inline bool RandomBytes(void* out, size_t n) {
    unsigned char* p = static_cast<unsigned char*>(out);
#ifdef _WIN32
    while (n > 0) {
        ULONG chunk = n > 0x10000000u ? 0x10000000u : (ULONG)n;
        if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, p, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) return false;
        p += chunk;
        n -= chunk;
    }
    return true;
#else
#ifdef IMFORGE_PAIRING_HAS_GETRANDOM
    while (n > 0) {
        ssize_t r = getrandom(p, n, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        p += r;
        n -= (size_t)r;
    }
    if (n == 0) return true;
#endif
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    while (n > 0) {
        ssize_t r = ::read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            ::close(fd);
            return false;
        }
        p += r;
        n -= (size_t)r;
    }
    ::close(fd);
    return true;
#endif
}

namespace detail {

inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// `w` is caller scratch, wiped once at the end instead of per block
inline void Sha256Block(uint32_t h[8], const unsigned char* p, uint32_t w[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    for (int i = 0; i < 16; ++i)
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
        uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

}  // namespace detail

class Sha256 {
public:
    static constexpr size_t kDigestSize = 32;

    Sha256() { Reset(); }
    ~Sha256() { Wipe(); }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void Reset() {
        static const uint32_t init[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        std::memcpy(h_, init, sizeof(h_));
        total_ = 0;
        used_ = 0;
    }

    void Update(const void* data, size_t len) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        total_ += (uint64_t)len;
        while (len > 0) {
            if (used_ == 0 && len >= 64) {
                detail::Sha256Block(h_, p, w_);
                p += 64;
                len -= 64;
                continue;
            }
            size_t take = 64 - used_ < len ? 64 - used_ : len;
            std::memcpy(buf_ + used_, p, take);
            used_ += take;
            p += take;
            len -= take;
            if (used_ == 64) {
                detail::Sha256Block(h_, buf_, w_);
                used_ = 0;
            }
        }
    }
    void Update(const std::string& s) { Update(s.data(), s.size()); }

    void Final(unsigned char out[kDigestSize]) {
        uint64_t bits = total_ * 8;
        unsigned char pad = 0x80;
        Update(&pad, 1);
        unsigned char zero = 0;
        while (used_ != 56) Update(&zero, 1);
        unsigned char len_be[8];
        for (int i = 0; i < 8; ++i) len_be[i] = (unsigned char)(bits >> (56 - 8 * i));
        Update(len_be, 8);
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = (unsigned char)(h_[i] >> 24);
            out[i * 4 + 1] = (unsigned char)(h_[i] >> 16);
            out[i * 4 + 2] = (unsigned char)(h_[i] >> 8);
            out[i * 4 + 3] = (unsigned char)h_[i];
        }
        Wipe();
        Reset();
    }

private:
    void Wipe() {
        SecureWipe(h_, sizeof(h_));
        SecureWipe(buf_, sizeof(buf_));
        SecureWipe(w_, sizeof(w_));
        total_ = 0;
        used_ = 0;
    }
    uint32_t h_[8];
    uint32_t w_[64];
    unsigned char buf_[64];
    uint64_t total_ = 0;
    size_t used_ = 0;
};

inline void Sha256Digest(const void* data, size_t len, unsigned char out[32]) {
    Sha256 s;
    s.Update(data, len);
    s.Final(out);
}

inline void HmacSha256(const void* key, size_t key_len, const void* data, size_t len, unsigned char out[32]) {
    unsigned char k[64];
    std::memset(k, 0, sizeof(k));
    if (key_len > 64) Sha256Digest(key, key_len, k);
    else if (key_len > 0) std::memcpy(k, key, key_len);
    unsigned char pad[64];
    unsigned char inner[32];
    for (int i = 0; i < 64; ++i) pad[i] = (unsigned char)(k[i] ^ 0x36);
    {
        Sha256 s;
        s.Update(pad, 64);
        s.Update(data, len);
        s.Final(inner);
    }
    for (int i = 0; i < 64; ++i) pad[i] = (unsigned char)(k[i] ^ 0x5c);
    {
        Sha256 s;
        s.Update(pad, 64);
        s.Update(inner, 32);
        s.Final(out);
    }
    SecureWipe(k, sizeof(k));
    SecureWipe(pad, sizeof(pad));
    SecureWipe(inner, sizeof(inner));
}

inline std::string ToHex(const void* data, size_t n) {
    static const char digits[] = "0123456789abcdef";
    const unsigned char* p = static_cast<const unsigned char*>(data);
    std::string out(n * 2, '0');
    for (size_t i = 0; i < n; ++i) {
        out[i * 2] = digits[p[i] >> 4];
        out[i * 2 + 1] = digits[p[i] & 15];
    }
    return out;
}

inline int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool FromHex(const std::string& hex, void* out, size_t n) {
    if (hex.size() != n * 2) return false;
    unsigned char* p = static_cast<unsigned char*>(out);
    for (size_t i = 0; i < n; ++i) {
        int hi = HexNibble(hex[i * 2]), lo = HexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        p[i] = (unsigned char)((hi << 4) | lo);
    }
    return true;
}

inline bool IsLowerHex(const std::string& s, size_t chars) {
    if (s.size() != chars) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

inline std::string RandomHex(size_t bytes) {
    unsigned char buf[64];
    if (bytes == 0 || bytes > sizeof(buf) || !RandomBytes(buf, bytes)) return std::string();
    std::string out = ToHex(buf, bytes);
    SecureWipe(buf, sizeof(buf));
    return out;
}

inline std::string Sha256Hex(const std::string& s) {
    unsigned char d[32];
    Sha256Digest(s.data(), s.size(), d);
    std::string out = ToHex(d, sizeof(d));
    SecureWipe(d, sizeof(d));
    return out;
}

// Crockford: uppercase, drop '-' and spaces, O becomes 0, I/L become 1. must end up kCodeChars long
inline bool NormalizeCode(const char* in, std::string& out) {
    out.clear();
    if (!in) return false;
    for (size_t i = 0; in[i]; ++i) {
        if (i > 256) {
            SecureWipe(out);
            return false;
        }
        char c = in[i];
        if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == 'O') c = '0';
        else if (c == 'I' || c == 'L') c = '1';
        if (!std::strchr(kCodeAlphabet, c) || c == '\0') {
            SecureWipe(out);
            return false;
        }
        out.push_back(c);
    }
    if (out.size() != kCodeChars) {
        SecureWipe(out);
        return false;
    }
    return true;
}

inline bool NormalizeCode(const std::string& in, std::string& out) { return NormalizeCode(in.c_str(), out); }

inline bool IsValidCode(const std::string& code) {
    std::string n;
    bool ok = NormalizeCode(code, n);
    SecureWipe(n);
    return ok;
}

// ABCDEFGH... to ABCD-EFGH-..., input must be normalized
inline std::string FormatCode(const std::string& normalized) {
    std::string out;
    out.reserve(normalized.size() + normalized.size() / kCodeGroup);
    for (size_t i = 0; i < normalized.size(); ++i) {
        if (i > 0 && i % kCodeGroup == 0) out.push_back('-');
        out.push_back(normalized[i]);
    }
    return out;
}

// empty if the OS gave us no random bytes
inline std::string GenerateCode() {
    unsigned char raw[kCodeChars * 5 / 8];
    if (!RandomBytes(raw, sizeof(raw))) return std::string();
    std::string n;
    n.reserve(kCodeChars);
    for (size_t i = 0; i < kCodeChars; ++i) {
        size_t bit = i * 5;
        unsigned v = ((unsigned)raw[bit / 8] << 8) | (bit / 8 + 1 < sizeof(raw) ? raw[bit / 8 + 1] : 0u);
        n.push_back(kCodeAlphabet[(v >> (11 - bit % 8)) & 31u]);
    }
    SecureWipe(raw, sizeof(raw));
    std::string out = FormatCode(n);
    SecureWipe(n);
    return out;
}

// K = SHA256("imforge-pair-v1|" + normalized code)
inline bool DeriveKey(const std::string& code, unsigned char key[32]) {
    std::string n;
    if (!NormalizeCode(code, n)) return false;
    Sha256 s;
    s.Update(kKeyLabel, std::strlen(kKeyLabel));
    s.Update("|", 1);
    s.Update(n);
    s.Final(key);
    SecureWipe(n);
    return true;
}

// hex(HMAC-SHA256(K, label|nonce_a|nonce_b|session_id)), everything lowercase hex
inline std::string HandshakeMac(const unsigned char key[32], const char* label, const std::string& nonce_a,
                                const std::string& nonce_b, const std::string& session_id) {
    std::string data = std::string(label) + "|" + nonce_a + "|" + nonce_b + "|" + session_id;
    unsigned char mac[32];
    HmacSha256(key, 32, data.data(), data.size(), mac);
    std::string out = ToHex(mac, sizeof(mac));
    SecureWipe(mac, sizeof(mac));
    return out;
}

}  // namespace pairing
}  // namespace imforge
