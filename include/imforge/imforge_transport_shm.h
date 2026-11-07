#pragma once

#include "imforge_transport_base.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef SetProp
#undef SetProp
#endif
#ifdef RemoveProp
#undef RemoveProp
#endif
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace imforge {

constexpr uint32_t kShmRingCapacity = 1u << 20;
constexpr uint32_t kShmMagic = 0x47464D49u;
constexpr uint32_t kShmVersion = 1;
constexpr uint32_t kShmSkipMarker = 0xFFFFFFFFu;
constexpr uint64_t kShmHeartbeatTimeoutMs = 2000;

// layout: u32 magic, u32 version, u32 heartbeat_builder, u32 heartbeat_app, Ring to_app, Ring to_builder
// Ring = u32 capacity, u32 head, u32 tail, u8 data[capacity]
class ShmTransport : public Transport {
public:
    explicit ShmTransport(uint32_t ring_capacity = kShmRingCapacity) : capacity_(ring_capacity) {}
    ~ShmTransport() override { Close(); }

    bool Listen(const char* endpoint) override {
        Close();
        if (!ParseName(endpoint) || capacity_ < kMinCapacity || capacity_ % 4 != 0) return false;
        is_server_ = true;
        if (!CreateSegment()) return false;
        InitSegment();
        active_ = true;
        return true;
    }

    bool Connect(const char* endpoint) override {
        Close();
        if (!ParseName(endpoint)) return false;
        is_server_ = false;
        active_ = true;
        last_attempt_ms_ = detail::NowMs();
        OpenSegment();
        return true;
    }

    bool IsConnected() const override { return connected_; }

    bool Send(const void* data, size_t len) override {
        if (len > kTransportMaxFrame || len > MaxPayload()) {
            stats_.drops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (!connected_) return false;
        std::lock_guard<std::mutex> lock(send_mutex_);
        std::vector<unsigned char> payload((const unsigned char*)data, (const unsigned char*)data + len);
        pending_bytes_ += payload.size() + 4;
        pending_.push_back(std::move(payload));
        while (pending_bytes_ > kTransportMaxQueueBytes && pending_.size() > 1) {
            pending_bytes_ -= pending_.front().size() + 4;
            pending_.pop_front();
            stats_.drops.fetch_add(1, std::memory_order_relaxed);
        }
        FlushLocked();
        return true;
    }

    bool Receive(std::vector<unsigned char>& out) override {
        if (!base_) return false;
        if (!RingRead(in_ring_, out)) return false;
        stats_.messages_received.fetch_add(1, std::memory_order_relaxed);
        stats_.bytes_received.fetch_add(out.size() + 4, std::memory_order_relaxed);
        return true;
    }

    void Poll() override {
        if (!active_) return;
        uint64_t now = detail::NowMs();
        if (!base_) {
            if (is_server_ || now - last_attempt_ms_ < kTransportReconnectMs) return;
            last_attempt_ms_ = now;
            if (!OpenSegment()) return;
        }
        ++my_heartbeat_;
        StoreU32(my_hb_, my_heartbeat_);
        uint32_t peer = LoadU32(peer_hb_);
        if (peer != last_peer_hb_) {
            last_peer_hb_ = peer;
            last_peer_change_ms_ = now;
        }
        bool alive = last_peer_change_ms_ != 0 && now - last_peer_change_ms_ < kShmHeartbeatTimeoutMs;
        if (connected_ && !alive) {
            {
                std::lock_guard<std::mutex> lock(send_mutex_);
                pending_.clear();
                pending_bytes_ = 0;
            }
            connected_ = false;
            if (!is_server_) {
                Unmap();
                last_attempt_ms_ = now;
                return;
            }
        }
        connected_ = alive;
        if (connected_) {
            std::lock_guard<std::mutex> lock(send_mutex_);
            FlushLocked();
        }
    }

    void Close() override {
        Unmap();
#ifndef _WIN32
        if (is_server_ && !sys_name_.empty()) shm_unlink(sys_name_.c_str());
#endif
        {
            std::lock_guard<std::mutex> lock(send_mutex_);
            pending_.clear();
            pending_bytes_ = 0;
        }
        active_ = false;
        connected_ = false;
        is_server_ = false;
        last_peer_hb_ = 0;
        last_peer_change_ms_ = 0;
    }

    TransportStats Stats() const override { return stats_.Snapshot(); }

    size_t MaxPayload() const { return capacity_ - 8; }
    uint32_t Capacity() const { return capacity_; }

private:
    static constexpr uint32_t kMinCapacity = 64;
    static constexpr uint32_t kMaxCapacity = 1u << 30;
    static constexpr size_t kHeaderSize = 16;
    static constexpr size_t kRingHeaderSize = 12;

    struct Ring {
        volatile uint32_t* head = nullptr;
        volatile uint32_t* tail = nullptr;
        unsigned char* data = nullptr;
        uint32_t capacity = 0;
    };

    static uint32_t LoadU32(volatile uint32_t* p) { return *p; }
    static void StoreU32(volatile uint32_t* p, uint32_t v) { *p = v; }
    static size_t SegmentSize(uint32_t capacity) { return kHeaderSize + 2 * (kRingHeaderSize + (size_t)capacity); }

    bool ParseName(const char* endpoint) {
        std::string name;
        if (!detail::StripScheme(endpoint, "shm://", name)) return false;
        for (char c : name) {
            if (c == '/' || c == '\\') return false;
        }
#ifdef _WIN32
        sys_name_ = "Local\\ImForge." + name;
#else
        sys_name_ = "/ImForge." + name;
#endif
        return true;
    }

    void BindRings() {
        unsigned char* b = (unsigned char*)base_;
        Ring to_app, to_builder;
        to_app.capacity = capacity_;
        to_app.head = (volatile uint32_t*)(b + kHeaderSize + 4);
        to_app.tail = (volatile uint32_t*)(b + kHeaderSize + 8);
        to_app.data = b + kHeaderSize + kRingHeaderSize;
        size_t second = kHeaderSize + kRingHeaderSize + capacity_;
        to_builder.capacity = capacity_;
        to_builder.head = (volatile uint32_t*)(b + second + 4);
        to_builder.tail = (volatile uint32_t*)(b + second + 8);
        to_builder.data = b + second + kRingHeaderSize;
        out_ring_ = is_server_ ? to_app : to_builder;
        in_ring_ = is_server_ ? to_builder : to_app;
        my_hb_ = (volatile uint32_t*)(b + (is_server_ ? 8 : 12));
        peer_hb_ = (volatile uint32_t*)(b + (is_server_ ? 12 : 8));
        last_peer_hb_ = LoadU32(peer_hb_);
        last_peer_change_ms_ = 0;
    }

    void InitSegment() {
        unsigned char* b = (unsigned char*)base_;
        std::memset(b, 0, mapped_size_);
        StoreU32((volatile uint32_t*)(b + kHeaderSize), capacity_);
        StoreU32((volatile uint32_t*)(b + kHeaderSize + kRingHeaderSize + capacity_), capacity_);
        std::atomic_thread_fence(std::memory_order_release);
        StoreU32((volatile uint32_t*)(b + 4), kShmVersion);
        StoreU32((volatile uint32_t*)(b + 0), kShmMagic);
        BindRings();
    }

    bool ValidateHeader(size_t available) {
        const unsigned char* b = (const unsigned char*)base_;
        if (available < kHeaderSize + kRingHeaderSize) return false;
        if (LoadU32((volatile uint32_t*)(b + 0)) != kShmMagic) return false;
        if (LoadU32((volatile uint32_t*)(b + 4)) != kShmVersion) return false;
        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t cap = LoadU32((volatile uint32_t*)(b + kHeaderSize));
        if (cap < kMinCapacity || cap > kMaxCapacity || cap % 4 != 0) return false;
        if (available < SegmentSize(cap)) return false;
        if (LoadU32((volatile uint32_t*)(b + kHeaderSize + kRingHeaderSize + cap)) != cap) return false;
        capacity_ = cap;
        return true;
    }

#ifdef _WIN32
    bool CreateSegment() {
        size_t size = SegmentSize(capacity_);
        HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD)((uint64_t)size >> 32),
                                      (DWORD)(size & 0xFFFFFFFFu), sys_name_.c_str());
        if (!h) return false;
        void* view = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!view || ViewSize(view) < size) {
            if (view) UnmapViewOfFile(view);
            CloseHandle(h);
            return false;
        }
        mapping_ = h;
        base_ = view;
        mapped_size_ = size;
        return true;
    }

    bool OpenSegment() {
        HANDLE h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, sys_name_.c_str());
        if (!h) return false;
        void* view = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!view) {
            CloseHandle(h);
            return false;
        }
        size_t avail = ViewSize(view);
        base_ = view;
        if (!ValidateHeader(avail)) {
            base_ = nullptr;
            UnmapViewOfFile(view);
            CloseHandle(h);
            return false;
        }
        mapping_ = h;
        mapped_size_ = SegmentSize(capacity_);
        BindRings();
        return true;
    }

    static size_t ViewSize(void* view) {
        MEMORY_BASIC_INFORMATION info;
        std::memset(&info, 0, sizeof(info));
        if (VirtualQuery(view, &info, sizeof(info)) == 0) return 0;
        return info.RegionSize;
    }

    void Unmap() {
        if (base_) UnmapViewOfFile(base_);
        if (mapping_) CloseHandle(mapping_);
        base_ = nullptr;
        mapping_ = nullptr;
        mapped_size_ = 0;
        connected_ = false;
    }

    HANDLE mapping_ = nullptr;
#else
    bool CreateSegment() {
        size_t size = SegmentSize(capacity_);
        int fd = shm_open(sys_name_.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd < 0) return false;
        struct stat own;
        if (fstat(fd, &own) != 0 || own.st_uid != geteuid()) {
            ::close(fd);
            return false;
        }
        if (ftruncate(fd, (off_t)size) != 0) {
            ::close(fd);
            return false;
        }
        void* view = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (view == MAP_FAILED) return false;
        base_ = view;
        mapped_size_ = size;
        return true;
    }

    bool OpenSegment() {
        int fd = shm_open(sys_name_.c_str(), O_RDWR, 0);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) != 0 || st.st_uid != geteuid() || (st.st_mode & 022) != 0 ||
            st.st_size < (off_t)(kHeaderSize + kRingHeaderSize)) {
            ::close(fd);
            return false;
        }
        size_t size = (size_t)st.st_size;
        void* view = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        if (view == MAP_FAILED) return false;
        base_ = view;
        mapped_size_ = size;
        if (!ValidateHeader(size)) {
            Unmap();
            return false;
        }
        BindRings();
        return true;
    }

    void Unmap() {
        if (base_) munmap(base_, mapped_size_);
        base_ = nullptr;
        mapped_size_ = 0;
        connected_ = false;
    }
#endif

    void FlushLocked() {
        while (!pending_.empty()) {
            const std::vector<unsigned char>& msg = pending_.front();
            int r = RingWrite(out_ring_, msg.data(), (uint32_t)msg.size());
            if (r < 0) break;
            if (r == 0) continue;
            stats_.messages_sent.fetch_add(1, std::memory_order_relaxed);
            stats_.bytes_sent.fetch_add(msg.size() + 4, std::memory_order_relaxed);
            pending_bytes_ -= msg.size() + 4;
            pending_.pop_front();
        }
    }

    // 1 written, 0 only published a skip marker, -1 no space
    static int RingWrite(const Ring& ring, const unsigned char* payload, uint32_t len) {
        const uint32_t cap = ring.capacity;
        const uint32_t rec = 4 + len;
        uint32_t head = LoadU32(ring.head);
        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t tail = LoadU32(ring.tail);
        if (head >= cap || tail >= cap) return -1;
        uint32_t new_tail;
        if (cap - tail >= rec) {
            uint32_t free_bytes = (head + cap - tail - 1) % cap;
            if (rec > free_bytes) return -1;
            detail::PutU32LE(ring.data + tail, len);
            if (len) std::memcpy(ring.data + tail + 4, payload, len);
            new_tail = tail + rec;
            if (new_tail == cap) new_tail = 0;
        } else {
            if (head > tail || head == 0) return -1;
            bool marker_fits = cap - tail >= 4;
            if (rec < head) {
                if (marker_fits) detail::PutU32LE(ring.data + tail, kShmSkipMarker);
                detail::PutU32LE(ring.data, len);
                if (len) std::memcpy(ring.data + 4, payload, len);
                new_tail = rec;
            } else {
                if (!marker_fits) return -1;
                detail::PutU32LE(ring.data + tail, kShmSkipMarker);
                std::atomic_thread_fence(std::memory_order_release);
                StoreU32(ring.tail, 0);
                return 0;
            }
        }
        std::atomic_thread_fence(std::memory_order_release);
        StoreU32(ring.tail, new_tail);
        return 1;
    }

    static bool RingRead(const Ring& ring, std::vector<unsigned char>& out) {
        const uint32_t cap = ring.capacity;
        uint32_t tail = LoadU32(ring.tail);
        std::atomic_thread_fence(std::memory_order_acquire);
        uint32_t head = LoadU32(ring.head);
        if (head == tail) return false;
        if (head >= cap || tail >= cap) {
            StoreU32(ring.head, tail < cap ? tail : 0);
            return false;
        }
        if (cap - head < 4) {
            head = 0;
            if (head == tail) {
                std::atomic_thread_fence(std::memory_order_release);
                StoreU32(ring.head, 0);
                return false;
            }
        }
        uint32_t len = detail::GetU32LE(ring.data + head);
        if (len == kShmSkipMarker) {
            head = 0;
            if (head == tail) {
                std::atomic_thread_fence(std::memory_order_release);
                StoreU32(ring.head, 0);
                return false;
            }
            len = detail::GetU32LE(ring.data);
        }
        const uint64_t record_end = (uint64_t)head + 4 + len;
        const uint64_t readable_end = tail > head ? tail : cap;
        if (len > cap - 8 || record_end > readable_end) {
            std::atomic_thread_fence(std::memory_order_release);
            StoreU32(ring.head, tail);
            return false;
        }
        out.assign(ring.data + head + 4, ring.data + head + 4 + len);
        uint32_t new_head = head + 4 + len;
        if (new_head == cap) new_head = 0;
        std::atomic_thread_fence(std::memory_order_release);
        StoreU32(ring.head, new_head);
        return true;
    }

    uint32_t capacity_;
    std::string sys_name_;
    bool is_server_ = false;
    bool active_ = false;
    bool connected_ = false;
    void* base_ = nullptr;
    size_t mapped_size_ = 0;
    Ring in_ring_;
    Ring out_ring_;
    volatile uint32_t* my_hb_ = nullptr;
    volatile uint32_t* peer_hb_ = nullptr;
    uint32_t my_heartbeat_ = 0;
    uint32_t last_peer_hb_ = 0;
    uint64_t last_peer_change_ms_ = 0;
    uint64_t last_attempt_ms_ = 0;
    std::mutex send_mutex_;
    std::deque<std::vector<unsigned char>> pending_;
    size_t pending_bytes_ = 0;
    detail::AtomicStats stats_;
};

}  // namespace imforge
