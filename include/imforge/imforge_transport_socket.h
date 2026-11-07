#pragma once

#include "imforge_transport_base.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#ifdef SetProp
#undef SetProp
#endif
#ifdef RemoveProp
#undef RemoveProp
#endif
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace imforge {
namespace detail {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
inline int SocketError() { return WSAGetLastError(); }
inline bool SocketWouldBlock(int e) { return e == WSAEWOULDBLOCK; }
inline bool SocketInProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline bool SocketInterrupted(int e) { return e == WSAEINTR; }
inline void CloseSocketHandle(SocketHandle s) { closesocket(s); }
inline bool SetNonBlocking(SocketHandle s) {
    u_long one = 1;
    return ioctlsocket(s, FIONBIO, &one) == 0;
}
constexpr int kSendFlags = 0;

struct WinsockState {
    std::mutex mutex;
    int refs = 0;
};
inline WinsockState& GetWinsockState() {
    static WinsockState state;
    return state;
}
inline bool WinsockAddRef() {
    WinsockState& st = GetWinsockState();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (st.refs == 0) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    }
    ++st.refs;
    return true;
}
inline void WinsockRelease() {
    WinsockState& st = GetWinsockState();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (st.refs > 0 && --st.refs == 0) WSACleanup();
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
inline int SocketError() { return errno; }
inline bool SocketWouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN; }
inline bool SocketInProgress(int e) { return e == EINPROGRESS; }
inline bool SocketInterrupted(int e) { return e == EINTR; }
inline void CloseSocketHandle(SocketHandle s) { ::close(s); }
inline bool SetNonBlocking(SocketHandle s) {
    int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
inline bool WinsockAddRef() { return true; }
inline void WinsockRelease() {}
#endif

inline bool SocketSelectable(SocketHandle s) {
#ifdef _WIN32
    (void)s;
    return true;
#else
    return s >= 0 && s < FD_SETSIZE;
#endif
}

inline int SelectNfds(SocketHandle a, SocketHandle b) {
#ifdef _WIN32
    (void)a;
    (void)b;
    return 0;
#else
    return (a > b ? a : b) + 1;
#endif
}

class SocketEngine {
public:
    SocketEngine() : winsock_ok_(WinsockAddRef()) {}
    ~SocketEngine() {
        Close();
        if (winsock_ok_) WinsockRelease();
    }
    SocketEngine(const SocketEngine&) = delete;
    SocketEngine& operator=(const SocketEngine&) = delete;

    bool Listen(const sockaddr* addr, socklen_t addr_len) {
        Close();
        if (!winsock_ok_ || !addr) return false;
        SocketHandle s = ::socket(addr->sa_family, SOCK_STREAM, 0);
        if (s == kInvalidSocket) return false;
        if (!SocketSelectable(s)) {
            CloseSocketHandle(s);
            return false;
        }
#ifdef _WIN32
        BOOL exclusive = TRUE;
        if (::setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive,
                         sizeof(exclusive)) != 0) {
            CloseSocketHandle(s);
            return false;
        }
#else
        int one = 1;
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
#endif
        if (::bind(s, addr, addr_len) != 0 || ::listen(s, 4) != 0 || !SetNonBlocking(s)) {
            CloseSocketHandle(s);
            return false;
        }
        sockaddr_storage local;
        std::memset(&local, 0, sizeof(local));
        socklen_t local_len = sizeof(local);
        if (::getsockname(s, (sockaddr*)&local, &local_len) == 0) {
            if (local.ss_family == AF_INET) {
                bound_port_ = ntohs(((const sockaddr_in*)&local)->sin_port);
            } else if (local.ss_family == AF_INET6) {
                bound_port_ = ntohs(((const sockaddr_in6*)&local)->sin6_port);
            }
        }
        listen_sock_ = s;
        listen_family_ = addr->sa_family;
        is_server_ = true;
        StartThread();
        return true;
    }

    bool Connect(const sockaddr* addr, socklen_t addr_len) {
        Close();
        if (!winsock_ok_ || !addr || addr_len == 0 || (size_t)addr_len > sizeof(peer_addr_)) return false;
        std::memset(&peer_addr_, 0, sizeof(peer_addr_));
        std::memcpy(&peer_addr_, addr, (size_t)addr_len);
        peer_len_ = addr_len;
        is_server_ = false;
        last_attempt_ms_ = NowMs();
        attempt_requested_.store(true);
        StartThread();
        return true;
    }

    bool IsConnected() const { return connected_.load(std::memory_order_acquire); }
    bool Active() const { return running_; }
    int BoundPort() const { return bound_port_; }
    TransportStats Stats() const { return stats_.Snapshot(); }

    bool Send(const void* data, size_t len) {
        if (len > kTransportMaxFrame) {
            stats_.drops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (!IsConnected()) return false;
        size_t dropped = send_queue_.Push(MakeFrame(data, len));
        if (dropped) stats_.drops.fetch_add(dropped, std::memory_order_relaxed);
        return true;
    }

    bool Receive(std::vector<unsigned char>& out) { return recv_queue_.Pop(out); }

    void Poll() {
        if (!running_ || is_server_) return;
        if (IsConnected() || connecting_.load(std::memory_order_acquire)) return;
        uint64_t now = NowMs();
        if (now - last_attempt_ms_ >= kTransportReconnectMs) {
            last_attempt_ms_ = now;
            attempt_requested_.store(true);
        }
    }

    void Close() {
        if (thread_.joinable()) {
            stop_.store(true);
            thread_.join();
        }
        CloseConn();
        if (listen_sock_ != kInvalidSocket) {
            CloseSocketHandle(listen_sock_);
            listen_sock_ = kInvalidSocket;
        }
        running_ = false;
        stop_.store(false);
        attempt_requested_.store(false);
        connecting_.store(false);
        connected_.store(false);
        send_queue_.Clear();
        recv_queue_.Clear();
        parser_.Reset();
        cur_.clear();
        cur_off_ = 0;
        bound_port_ = 0;
    }

private:
    void StartThread() {
        running_ = true;
        stop_.store(false);
        rbuf_.resize(64 * 1024);
        thread_ = std::thread(&SocketEngine::ThreadMain, this);
    }

    void ThreadMain() {
        while (!stop_.load(std::memory_order_relaxed)) {
            if (is_server_) {
                ServerStep();
            } else {
                ClientStep();
            }
        }
        CloseConn();
    }

    void ServerStep() {
        fd_set rd, wr;
        FD_ZERO(&rd);
        FD_ZERO(&wr);
        FD_SET(listen_sock_, &rd);
        bool want_write = false;
        if (conn_sock_ != kInvalidSocket) {
            want_write = !FlushSend();
            if (conn_sock_ != kInvalidSocket) {
                FD_SET(conn_sock_, &rd);
                if (want_write) FD_SET(conn_sock_, &wr);
            } else {
                want_write = false;
            }
        }
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = kTransportIoTimeoutMs * 1000;
        int r = ::select(SelectNfds(listen_sock_, conn_sock_), &rd, want_write ? &wr : nullptr, nullptr, &tv);
        if (r < 0) {
            if (!SocketInterrupted(SocketError())) SleepMs(kTransportIoTimeoutMs);
            return;
        }
        if (r == 0) return;
        if (FD_ISSET(listen_sock_, &rd)) AcceptOne();
        if (conn_sock_ != kInvalidSocket && FD_ISSET(conn_sock_, &rd)) ReadSome();
        if (conn_sock_ != kInvalidSocket && want_write && FD_ISSET(conn_sock_, &wr)) FlushSend();
    }

    void ClientStep() {
        if (conn_sock_ == kInvalidSocket) {
            if (!attempt_requested_.exchange(false)) {
                SleepMs(kTransportIdleSleepMs);
                return;
            }
            StartConnect();
            return;
        }
        if (connecting_.load(std::memory_order_relaxed)) {
            WaitConnect();
            return;
        }
        bool want_write = !FlushSend();
        if (conn_sock_ == kInvalidSocket) return;
        fd_set rd, wr;
        FD_ZERO(&rd);
        FD_ZERO(&wr);
        FD_SET(conn_sock_, &rd);
        if (want_write) FD_SET(conn_sock_, &wr);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = kTransportIoTimeoutMs * 1000;
        int r = ::select(SelectNfds(conn_sock_, conn_sock_), &rd, want_write ? &wr : nullptr, nullptr, &tv);
        if (r < 0) {
            if (!SocketInterrupted(SocketError())) SleepMs(kTransportIoTimeoutMs);
            return;
        }
        if (r == 0) return;
        if (FD_ISSET(conn_sock_, &rd)) ReadSome();
        if (conn_sock_ != kInvalidSocket && want_write && FD_ISSET(conn_sock_, &wr)) FlushSend();
    }

    void StartConnect() {
#ifndef _WIN32
        if (peer_addr_.ss_family == AF_UNIX) {
            struct stat st;
            const sockaddr_un* un = (const sockaddr_un*)&peer_addr_;
            if (::lstat(un->sun_path, &st) != 0 || !S_ISSOCK(st.st_mode) || st.st_uid != ::geteuid()) return;
        }
#endif
        SocketHandle s = ::socket(peer_addr_.ss_family, SOCK_STREAM, 0);
        if (s == kInvalidSocket) return;
        if (!SocketSelectable(s) || !SetNonBlocking(s)) {
            CloseSocketHandle(s);
            return;
        }
        int r = ::connect(s, (const sockaddr*)&peer_addr_, peer_len_);
        if (r == 0) {
            Adopt(s);
            return;
        }
        int e = SocketError();
        if (SocketInProgress(e)) {
            conn_sock_ = s;
            connect_start_ms_ = NowMs();
            connecting_.store(true, std::memory_order_release);
            return;
        }
        CloseSocketHandle(s);
    }

    void WaitConnect() {
        fd_set wr, ex;
        FD_ZERO(&wr);
        FD_ZERO(&ex);
        FD_SET(conn_sock_, &wr);
        FD_SET(conn_sock_, &ex);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = kTransportIoTimeoutMs * 1000;
        int r = ::select(SelectNfds(conn_sock_, conn_sock_), nullptr, &wr, &ex, &tv);
        if (r < 0) {
            if (!SocketInterrupted(SocketError())) FailConnect();
            return;
        }
        if (r == 0) {
            if (NowMs() - connect_start_ms_ > kTransportConnectTimeoutMs) FailConnect();
            return;
        }
        if (FD_ISSET(conn_sock_, &ex)) {
            FailConnect();
            return;
        }
        int err = 0;
        socklen_t err_len = sizeof(err);
        if (::getsockopt(conn_sock_, SOL_SOCKET, SO_ERROR, (char*)&err, &err_len) != 0 || err != 0) {
            FailConnect();
            return;
        }
        SocketHandle s = conn_sock_;
        conn_sock_ = kInvalidSocket;
        connecting_.store(false, std::memory_order_release);
        Adopt(s);
    }

    void FailConnect() {
        if (conn_sock_ != kInvalidSocket) {
            CloseSocketHandle(conn_sock_);
            conn_sock_ = kInvalidSocket;
        }
        connecting_.store(false, std::memory_order_release);
    }

    void Adopt(SocketHandle s) {
        SetNonBlocking(s);
        int one = 1;
        ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
#ifdef SO_NOSIGPIPE
        ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&one, sizeof(one));
#endif
        conn_sock_ = s;
        parser_.Reset();
        cur_.clear();
        cur_off_ = 0;
        send_queue_.Clear();
        connected_.store(true, std::memory_order_release);
    }

    void AcceptOne() {
        sockaddr_storage peer;
        socklen_t peer_len = sizeof(peer);
        SocketHandle s = ::accept(listen_sock_, (sockaddr*)&peer, &peer_len);
        if (s == kInvalidSocket) return;
        if (conn_sock_ != kInvalidSocket || !SocketSelectable(s)) {
            CloseSocketHandle(s);
            return;
        }
#if defined(__linux__)
        if (listen_family_ == AF_UNIX) {
            struct ucred cred;
            socklen_t cred_len = sizeof(cred);
            if (::getsockopt(s, SOL_SOCKET, SO_PEERCRED, &cred, &cred_len) != 0 || cred.uid != ::geteuid()) {
                CloseSocketHandle(s);
                return;
            }
        }
#endif
        Adopt(s);
    }

    void ReadSome() {
        auto n = ::recv(conn_sock_, (char*)rbuf_.data(), (int)rbuf_.size(), 0);
        if (n > 0) {
            stats_.bytes_received.fetch_add((uint64_t)n, std::memory_order_relaxed);
            parser_.Append(rbuf_.data(), (size_t)n);
            std::vector<unsigned char> frame;
            while (parser_.Pop(frame)) {
                stats_.messages_received.fetch_add(1, std::memory_order_relaxed);
                size_t dropped = recv_queue_.Push(std::move(frame));
                if (dropped) stats_.drops.fetch_add(dropped, std::memory_order_relaxed);
            }
            if (parser_.Corrupt()) Disconnect();
            return;
        }
        if (n == 0) {
            Disconnect();
            return;
        }
        int e = SocketError();
        if (SocketWouldBlock(e) || SocketInterrupted(e)) return;
        Disconnect();
    }

    bool FlushSend() {
        for (;;) {
            if (cur_.empty()) {
                if (!send_queue_.Pop(cur_)) return true;
                cur_off_ = 0;
            }
            size_t remain = cur_.size() - cur_off_;
            size_t chunk = remain > (size_t(1) << 20) ? (size_t(1) << 20) : remain;
            auto n = ::send(conn_sock_, (const char*)cur_.data() + cur_off_, (int)chunk, kSendFlags);
            if (n > 0) {
                stats_.bytes_sent.fetch_add((uint64_t)n, std::memory_order_relaxed);
                cur_off_ += (size_t)n;
                if (cur_off_ == cur_.size()) {
                    stats_.messages_sent.fetch_add(1, std::memory_order_relaxed);
                    cur_.clear();
                    cur_off_ = 0;
                }
                continue;
            }
            int e = SocketError();
            if (SocketWouldBlock(e) || SocketInterrupted(e)) return false;
            Disconnect();
            return true;
        }
    }

    void Disconnect() {
        CloseConn();
        parser_.Reset();
        cur_.clear();
        cur_off_ = 0;
        send_queue_.Clear();
    }

    void CloseConn() {
        connected_.store(false, std::memory_order_release);
        connecting_.store(false, std::memory_order_release);
        if (conn_sock_ != kInvalidSocket) {
            CloseSocketHandle(conn_sock_);
            conn_sock_ = kInvalidSocket;
        }
    }

    bool winsock_ok_ = false;
    bool is_server_ = false;
    bool running_ = false;
    int bound_port_ = 0;
    SocketHandle listen_sock_ = kInvalidSocket;
    int listen_family_ = 0;
    SocketHandle conn_sock_ = kInvalidSocket;
    sockaddr_storage peer_addr_{};
    socklen_t peer_len_ = 0;
    uint64_t last_attempt_ms_ = 0;
    uint64_t connect_start_ms_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> connecting_{false};
    std::atomic<bool> attempt_requested_{false};
    std::thread thread_;
    FrameQueue send_queue_;
    FrameQueue recv_queue_;
    FrameParser parser_;
    std::vector<unsigned char> rbuf_;
    std::vector<unsigned char> cur_;
    size_t cur_off_ = 0;
    AtomicStats stats_;
};

}  // namespace detail
}  // namespace imforge
