#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace imforge {

struct TransportStats {
    uint64_t bytes_sent = 0;
    uint64_t bytes_received = 0;
    uint64_t messages_sent = 0;
    uint64_t messages_received = 0;
    uint64_t drops = 0;
};

struct Transport {
    virtual ~Transport() {}
    virtual bool Listen(const char* endpoint) = 0;
    virtual bool Connect(const char* endpoint) = 0;
    virtual bool IsConnected() const = 0;
    virtual bool Send(const void* data, size_t len) = 0;
    virtual bool Receive(std::vector<unsigned char>& out) = 0;
    virtual void Poll() = 0;
    virtual void Close() = 0;
    virtual TransportStats Stats() const { return TransportStats(); }
    virtual int BoundPort() const { return 0; }
};

constexpr size_t kTransportMaxFrame = size_t(32) << 20;
constexpr size_t kTransportMaxQueueBytes = size_t(64) << 20;
constexpr uint64_t kTransportReconnectMs = 1000;
constexpr uint64_t kTransportConnectTimeoutMs = 3000;
constexpr int kTransportIoTimeoutMs = 10;
constexpr int kTransportIdleSleepMs = 5;

namespace detail {

inline uint64_t NowMs() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline bool StripScheme(const char* endpoint, const char* scheme, std::string& rest) {
    if (!endpoint) return false;
    size_t n = std::strlen(scheme);
    if (std::strncmp(endpoint, scheme, n) == 0) {
        rest = endpoint + n;
        return !rest.empty();
    }
    if (std::strstr(endpoint, "://")) return false;
    rest = endpoint;
    return !rest.empty();
}

inline void PutU32LE(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

inline uint32_t GetU32LE(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

inline std::vector<unsigned char> MakeFrame(const void* data, size_t len) {
    std::vector<unsigned char> f(4 + len);
    PutU32LE(f.data(), (uint32_t)len);
    if (len) std::memcpy(f.data() + 4, data, len);
    return f;
}

struct AtomicStats {
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> drops{0};

    TransportStats Snapshot() const {
        TransportStats s;
        s.bytes_sent = bytes_sent.load(std::memory_order_relaxed);
        s.bytes_received = bytes_received.load(std::memory_order_relaxed);
        s.messages_sent = messages_sent.load(std::memory_order_relaxed);
        s.messages_received = messages_received.load(std::memory_order_relaxed);
        s.drops = drops.load(std::memory_order_relaxed);
        return s;
    }
    void Reset() {
        bytes_sent = 0;
        bytes_received = 0;
        messages_sent = 0;
        messages_received = 0;
        drops = 0;
    }
};

class FrameQueue {
public:
    explicit FrameQueue(size_t limit = kTransportMaxQueueBytes) : limit_(limit) {}

    size_t Push(std::vector<unsigned char>&& frame) {
        std::lock_guard<std::mutex> lock(mutex_);
        bytes_ += frame.size();
        items_.push_back(std::move(frame));
        size_t dropped = 0;
        while (bytes_ > limit_ && items_.size() > 1) {
            bytes_ -= items_.front().size();
            items_.pop_front();
            ++dropped;
        }
        return dropped;
    }
    bool Pop(std::vector<unsigned char>& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.empty()) return false;
        out = std::move(items_.front());
        items_.pop_front();
        bytes_ -= out.size();
        return true;
    }
    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.empty();
    }
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
        bytes_ = 0;
    }

private:
    mutable std::mutex mutex_;
    std::deque<std::vector<unsigned char>> items_;
    size_t bytes_ = 0;
    size_t limit_;
};

class FrameParser {
public:
    void Reset() {
        buf_.clear();
        pos_ = 0;
        corrupt_ = false;
    }
    void Append(const unsigned char* data, size_t len) {
        if (corrupt_ || len == 0) return;
        if (pos_ > 0 && pos_ == buf_.size()) {
            buf_.clear();
            pos_ = 0;
        }
        buf_.insert(buf_.end(), data, data + len);
    }
    bool Pop(std::vector<unsigned char>& out) {
        if (corrupt_) return false;
        size_t avail = buf_.size() - pos_;
        if (avail < 4) {
            Compact();
            return false;
        }
        uint32_t len = GetU32LE(buf_.data() + pos_);
        if (len > kTransportMaxFrame) {
            corrupt_ = true;
            return false;
        }
        if (avail - 4 < len) {
            Compact();
            return false;
        }
        out.assign(buf_.begin() + (ptrdiff_t)(pos_ + 4), buf_.begin() + (ptrdiff_t)(pos_ + 4 + len));
        pos_ += 4 + len;
        if (pos_ == buf_.size()) {
            buf_.clear();
            pos_ = 0;
        }
        return true;
    }
    bool Corrupt() const { return corrupt_; }

private:
    void Compact() {
        if (pos_ == 0) return;
        buf_.erase(buf_.begin(), buf_.begin() + (ptrdiff_t)pos_);
        pos_ = 0;
    }
    std::vector<unsigned char> buf_;
    size_t pos_ = 0;
    bool corrupt_ = false;
};

}  // namespace detail
}  // namespace imforge
