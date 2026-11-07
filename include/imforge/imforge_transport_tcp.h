#pragma once

#include "imforge_transport_socket.h"

#include <cstdio>

namespace imforge {

namespace detail {
inline std::atomic<bool>& RemoteListenFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}
inline bool IsLoopbackSockaddr(const sockaddr_storage& a) {
    if (a.ss_family == AF_INET) {
        uint32_t ip = ntohl(((const sockaddr_in*)&a)->sin_addr.s_addr);
        return (ip >> 24) == 127;
    }
    if (a.ss_family == AF_INET6) {
        const sockaddr_in6* s6 = (const sockaddr_in6*)&a;
        if (IN6_IS_ADDR_LOOPBACK(&s6->sin6_addr)) return true;
        const unsigned char* b = (const unsigned char*)&s6->sin6_addr;
        static const unsigned char kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        return std::memcmp(b, kMapped, 12) == 0 && b[12] == 127;
    }
    return false;
}
}  // namespace detail

// non-loopback listen is refused unless the app opts in here
inline void SetAllowRemoteListen(bool allow) { detail::RemoteListenFlag().store(allow); }

class TcpTransport : public Transport {
public:
    TcpTransport() {}
    ~TcpTransport() override { Close(); }

    bool Listen(const char* endpoint) override {
        sockaddr_storage addr;
        socklen_t len = 0;
        if (!Resolve(endpoint, true, addr, len)) return false;
        if (!detail::IsLoopbackSockaddr(addr)) {
            if (!detail::RemoteListenFlag().load()) {
                std::fprintf(stderr, "imforge: refusing to listen on a non-loopback address (%s); call "
                                     "imforge::SetAllowRemoteListen(true) to allow it\n", endpoint ? endpoint : "");
                return false;
            }
            std::fprintf(stderr, "imforge: WARNING listening on non-loopback address %s; any host that can reach this "
                                 "port can connect and send project data\n", endpoint ? endpoint : "");
        }
        return engine_.Listen((const sockaddr*)&addr, len);
    }
    bool Connect(const char* endpoint) override {
        sockaddr_storage addr;
        socklen_t len = 0;
        if (!Resolve(endpoint, false, addr, len)) return false;
        return engine_.Connect((const sockaddr*)&addr, len);
    }
    bool IsConnected() const override { return engine_.IsConnected(); }
    bool Send(const void* data, size_t len) override { return engine_.Send(data, len); }
    bool Receive(std::vector<unsigned char>& out) override { return engine_.Receive(out); }
    void Poll() override { engine_.Poll(); }
    void Close() override { engine_.Close(); }
    TransportStats Stats() const override { return engine_.Stats(); }
    int BoundPort() const override { return engine_.BoundPort(); }

    static bool SplitHostPort(const char* endpoint, std::string& host, std::string& port) {
        std::string rest;
        if (!detail::StripScheme(endpoint, "tcp://", rest)) return false;
        size_t colon = rest.rfind(':');
        if (colon == std::string::npos || colon + 1 >= rest.size()) return false;
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
        if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
            host = host.substr(1, host.size() - 2);
        }
        for (char c : port) {
            if (c < '0' || c > '9') return false;
        }
        return true;
    }

private:
    static bool Resolve(const char* endpoint, bool passive, sockaddr_storage& out, socklen_t& out_len) {
        std::string host, port;
        if (!SplitHostPort(endpoint, host, port)) return false;
        if (host.empty()) host = "127.0.0.1";
        addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = passive ? AI_PASSIVE : 0;
        addrinfo* res = nullptr;
        if (::getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) return false;
        const addrinfo* pick = res;
        for (const addrinfo* ai = res; ai; ai = ai->ai_next) {
            if (ai->ai_family == AF_INET) {
                pick = ai;
                break;
            }
        }
        bool ok = pick->ai_addrlen > 0 && pick->ai_addrlen <= sizeof(out);
        if (ok) {
            std::memset(&out, 0, sizeof(out));
            std::memcpy(&out, pick->ai_addr, pick->ai_addrlen);
            out_len = (socklen_t)pick->ai_addrlen;
        }
        ::freeaddrinfo(res);
        return ok;
    }

    detail::SocketEngine engine_;
};

}  // namespace imforge
