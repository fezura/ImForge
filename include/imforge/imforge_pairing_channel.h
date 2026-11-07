#pragma once

// After pair_ok every payload (inside the normal length-prefixed frame) is
//
//   MAC (32 bytes) || seq (u64 big-endian) || compact JSON
//   MAC = HMAC-SHA256(direction key, seq || payload)
//
// seq starts at 1 per direction with no gaps, so tampered, replayed, reordered or dropped frames are
// caught before any JSON is parsed. Keys are HKDF-SHA256: salt = nonce_a || nonce_b || session_id (raw),
// IKM = K, info "imforge-v1 b2a" / "imforge-v1 a2b". No encryption on purpose: the link is local and
// carries UI definitions, not secrets.

#include "imforge_pairing.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace imforge {
namespace pairing {

constexpr const char* kChannelLabelB2A = "imforge-v1 b2a";
constexpr const char* kChannelLabelA2B = "imforge-v1 a2b";
constexpr size_t kChannelMacBytes = 32;
constexpr size_t kChannelSeqBytes = 8;
constexpr size_t kChannelHeaderBytes = kChannelMacBytes + kChannelSeqBytes;
constexpr size_t kHkdfMaxInfo = 256;

inline void HkdfExtract(const void* salt, size_t salt_len, const void* ikm, size_t ikm_len, unsigned char prk[32]) {
    HmacSha256(salt, salt_len, ikm, ikm_len, prk);
}

inline bool HkdfExpand(const unsigned char prk[32], const void* info, size_t info_len, unsigned char* out, size_t out_len) {
    if (out_len > 255 * 32 || info_len > kHkdfMaxInfo || (!info && info_len)) return false;
    unsigned char block[32 + kHkdfMaxInfo + 1];
    unsigned char t[32];
    size_t t_len = 0, done = 0;
    for (unsigned i = 1; done < out_len; ++i) {
        size_t n = 0;
        if (t_len) std::memcpy(block, t, t_len);
        n += t_len;
        if (info_len) std::memcpy(block + n, info, info_len);
        n += info_len;
        block[n++] = (unsigned char)i;
        HmacSha256(prk, 32, block, n, t);
        t_len = 32;
        size_t take = out_len - done < 32 ? out_len - done : 32;
        std::memcpy(out + done, t, take);
        done += take;
    }
    SecureWipe(block, sizeof(block));
    SecureWipe(t, sizeof(t));
    return true;
}

class Channel {
public:
    enum class Status { Ok, Inactive, Short, BadMac, BadSequence };

    Channel() {}
    ~Channel() { Wipe(); }
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // builder side sends b2a and receives a2b. nonces/session id are 32-char lowercase hex
    bool Init(const unsigned char key[32], const std::string& nonce_a, const std::string& nonce_b,
              const std::string& session_id, bool builder) {
        Wipe();
        if (!IsLowerHex(nonce_a, kNonceHexChars) || !IsLowerHex(nonce_b, kNonceHexChars) ||
            !IsLowerHex(session_id, kNonceHexChars))
            return false;
        unsigned char salt[3 * kNonceBytes];
        unsigned char prk[32];
        FromHex(nonce_a, salt, kNonceBytes);
        FromHex(nonce_b, salt + kNonceBytes, kNonceBytes);
        FromHex(session_id, salt + 2 * kNonceBytes, kNonceBytes);
        HkdfExtract(salt, sizeof(salt), key, 32, prk);
        const char* send_label = builder ? kChannelLabelB2A : kChannelLabelA2B;
        const char* recv_label = builder ? kChannelLabelA2B : kChannelLabelB2A;
        bool ok = HkdfExpand(prk, send_label, std::strlen(send_label), send_key_, 32) &&
                  HkdfExpand(prk, recv_label, std::strlen(recv_label), recv_key_, 32);
        SecureWipe(prk, sizeof(prk));
        SecureWipe(salt, sizeof(salt));
        if (!ok) {
            Wipe();
            return false;
        }
        active_ = true;
        return true;
    }

    bool Seal(const void* payload, size_t len, std::string& out) {
        out.clear();
        if (!active_ || send_seq_ == UINT64_MAX || (!payload && len)) return false;
        uint64_t seq = ++send_seq_;
        out.resize(kChannelHeaderBytes + len);
        unsigned char* p = reinterpret_cast<unsigned char*>(&out[0]);
        for (size_t i = 0; i < kChannelSeqBytes; ++i) p[kChannelMacBytes + i] = (unsigned char)(seq >> (56 - 8 * i));
        if (len) std::memcpy(p + kChannelHeaderBytes, payload, len);
        HmacSha256(send_key_, 32, p + kChannelMacBytes, kChannelSeqBytes + len, p);
        return true;
    }
    bool Seal(const std::string& payload, std::string& out) { return Seal(payload.data(), payload.size(), out); }

    // On Ok `payload` points into `frame`. Any failure wipes both keys, the link is dead after that.
    Status Open(const void* frame, size_t len, const unsigned char** payload, size_t* payload_len) {
        if (!active_) return Status::Inactive;
        const unsigned char* p = static_cast<const unsigned char*>(frame);
        if (!p || len < kChannelHeaderBytes) return Fail(Status::Short);
        unsigned char mac[32];
        HmacSha256(recv_key_, 32, p + kChannelMacBytes, len - kChannelMacBytes, mac);
        bool mac_ok = ConstantTimeEqual(mac, p, kChannelMacBytes);
        SecureWipe(mac, sizeof(mac));
        if (!mac_ok) return Fail(Status::BadMac);
        uint64_t seq = 0;
        for (size_t i = 0; i < kChannelSeqBytes; ++i) seq = (seq << 8) | p[kChannelMacBytes + i];
        last_seq_ = seq;
        if (recv_seq_ == UINT64_MAX || seq != recv_seq_ + 1) return Fail(Status::BadSequence);
        recv_seq_ = seq;
        if (payload) *payload = p + kChannelHeaderBytes;
        if (payload_len) *payload_len = len - kChannelHeaderBytes;
        return Status::Ok;
    }

    // safe to log, never contains key material
    std::string Describe(Status s) const {
        switch (s) {
        case Status::Ok: return "ok";
        case Status::Inactive: return "no session keys (link already closed)";
        case Status::Short: return "unauthenticated frame (too short to carry a MAC)";
        case Status::BadMac: return "frame failed authentication (MAC mismatch: tampered, forged or unsealed)";
        case Status::BadSequence:
            return "frame out of sequence (expected #" + std::to_string(recv_seq_ + 1) + ", got #" + std::to_string(last_seq_) +
                   ": replayed, reordered or dropped)";
        }
        return "unknown";
    }

    void Wipe() {
        SecureWipe(send_key_, sizeof(send_key_));
        SecureWipe(recv_key_, sizeof(recv_key_));
        active_ = false;
        send_seq_ = 0;
        recv_seq_ = 0;
        last_seq_ = 0;
    }

    bool Active() const { return active_; }
    uint64_t SentCount() const { return send_seq_; }
    uint64_t ReceivedCount() const { return recv_seq_; }
    bool KeysWiped() const {
        unsigned char acc = 0;
        for (size_t i = 0; i < 32; ++i) acc = (unsigned char)(acc | send_key_[i] | recv_key_[i]);
        return acc == 0;
    }

private:
    Status Fail(Status s) {
        uint64_t expected = recv_seq_, got = last_seq_;
        Wipe();
        recv_seq_ = expected;
        last_seq_ = got;
        return s;
    }

    unsigned char send_key_[32] = {};
    unsigned char recv_key_[32] = {};
    uint64_t send_seq_ = 0, recv_seq_ = 0, last_seq_ = 0;
    bool active_ = false;
};

}  // namespace pairing
}  // namespace imforge
