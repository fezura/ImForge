#pragma once

#include "imforge_transport_socket.h"

#ifdef _WIN32
#include <sddl.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
#else
#include <sys/stat.h>
#endif

namespace imforge {

#ifdef _WIN32

namespace detail {

inline bool CurrentUserSidString(std::string& out) {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    DWORD n = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &n);
    std::vector<unsigned char> buf(n ? n : 1);
    bool ok = n != 0 && GetTokenInformation(tok, TokenUser, buf.data(), n, &n) != 0;
    CloseHandle(tok);
    if (!ok) return false;
    LPSTR sid = nullptr;
    if (!ConvertSidToStringSidA(((TOKEN_USER*)buf.data())->User.Sid, &sid)) return false;
    out = sid;
    LocalFree(sid);
    return true;
}

// creating user only, no Everyone or Anonymous
struct PipeSecurity {
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR sd = nullptr;
    PipeSecurity() { std::memset(&sa, 0, sizeof(sa)); }
    ~PipeSecurity() {
        if (sd) LocalFree(sd);
    }
    bool Init() {
        std::string sid;
        if (!CurrentUserSidString(sid)) return false;
        std::string sddl = "D:P(A;;GA;;;" + sid + ")";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;
        return true;
    }
};

inline bool PipeServerIsCurrentUser(HANDLE pipe) {
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid) || pid == 0) return false;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return false;
    HANDLE tok = nullptr;
    bool same = false;
    if (OpenProcessToken(proc, TOKEN_QUERY, &tok)) {
        DWORD n = 0;
        GetTokenInformation(tok, TokenUser, nullptr, 0, &n);
        std::vector<unsigned char> theirs(n ? n : 1);
        std::string mine;
        if (n && GetTokenInformation(tok, TokenUser, theirs.data(), n, &n) && CurrentUserSidString(mine)) {
            LPSTR s = nullptr;
            if (ConvertSidToStringSidA(((TOKEN_USER*)theirs.data())->User.Sid, &s)) {
                same = mine == s;
                LocalFree(s);
            }
        }
        CloseHandle(tok);
    }
    CloseHandle(proc);
    return same;
}

}  // namespace detail

class PipeTransport : public Transport {
public:
    PipeTransport() {}
    ~PipeTransport() override { Close(); }

    bool Listen(const char* endpoint) override {
        Close();
        if (!ParseName(endpoint)) return false;
        detail::PipeSecurity security;
        if (!security.Init()) return false;
        HANDLE h = CreateNamedPipeA(path_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                    kPipeBufferSize, kPipeBufferSize, 0, &security.sa);
        if (h == INVALID_HANDLE_VALUE) return false;
        pipe_ = h;
        is_server_ = true;
        return StartThread();
    }

    bool Connect(const char* endpoint) override {
        Close();
        if (!ParseName(endpoint)) return false;
        is_server_ = false;
        last_attempt_ms_ = detail::NowMs();
        attempt_requested_.store(true);
        return StartThread();
    }

    bool IsConnected() const override { return connected_.load(std::memory_order_acquire); }

    bool Send(const void* data, size_t len) override {
        if (len > kTransportMaxFrame) {
            stats_.drops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (!IsConnected()) return false;
        size_t dropped = send_queue_.Push(detail::MakeFrame(data, len));
        if (dropped) stats_.drops.fetch_add(dropped, std::memory_order_relaxed);
        return true;
    }

    bool Receive(std::vector<unsigned char>& out) override { return recv_queue_.Pop(out); }

    void Poll() override {
        if (!running_ || is_server_ || IsConnected()) return;
        uint64_t now = detail::NowMs();
        if (now - last_attempt_ms_ >= kTransportReconnectMs) {
            last_attempt_ms_ = now;
            attempt_requested_.store(true);
        }
    }

    void Close() override {
        if (thread_.joinable()) {
            stop_.store(true);
            thread_.join();
        }
        if (pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        for (HANDLE& ev : events_) {
            if (ev) {
                CloseHandle(ev);
                ev = nullptr;
            }
        }
        running_ = false;
        stop_.store(false);
        connected_.store(false);
        attempt_requested_.store(false);
        send_queue_.Clear();
        recv_queue_.Clear();
        parser_.Reset();
    }

    TransportStats Stats() const override { return stats_.Snapshot(); }

private:
    static constexpr DWORD kPipeBufferSize = 64 * 1024;
    static constexpr DWORD kWaitMs = (DWORD)kTransportIoTimeoutMs;

    bool ParseName(const char* endpoint) {
        std::string name;
        if (!detail::StripScheme(endpoint, "pipe://", name)) return false;
        for (char c : name) {
            if (c == '\\' || c == '/') return false;
        }
        path_ = "\\\\.\\pipe\\" + name;
        return true;
    }

    bool StartThread() {
        for (HANDLE& ev : events_) {
            ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            if (!ev) {
                Close();
                return false;
            }
        }
        running_ = true;
        stop_.store(false);
        thread_ = std::thread(&PipeTransport::ThreadMain, this);
        return true;
    }

    void ThreadMain() {
        while (!stop_.load(std::memory_order_relaxed)) {
            if (is_server_) {
                ServerSession();
            } else {
                ClientSession();
            }
        }
    }

    void ServerSession() {
        OVERLAPPED ov;
        std::memset(&ov, 0, sizeof(ov));
        ov.hEvent = events_[0];
        ResetEvent(ov.hEvent);
        BOOL ok = ConnectNamedPipe(pipe_, &ov);
        DWORD err = ok ? ERROR_SUCCESS : GetLastError();
        bool accepted = false;
        if (ok || err == ERROR_PIPE_CONNECTED) {
            accepted = true;
        } else if (err == ERROR_IO_PENDING) {
            while (!stop_.load(std::memory_order_relaxed)) {
                DWORD w = WaitForSingleObject(ov.hEvent, kWaitMs);
                if (w == WAIT_OBJECT_0) {
                    DWORD n = 0;
                    accepted = GetOverlappedResult(pipe_, &ov, &n, FALSE) != 0;
                    break;
                }
                if (w != WAIT_TIMEOUT) break;
            }
            if (!accepted) {
                CancelIo(pipe_);
                DWORD n = 0;
                GetOverlappedResult(pipe_, &ov, &n, TRUE);
            }
        } else {
            DisconnectNamedPipe(pipe_);
            detail::SleepMs(kTransportIoTimeoutMs);
            return;
        }
        if (!accepted) {
            DisconnectNamedPipe(pipe_);
            return;
        }
        IoLoop();
        FlushFileBuffers(pipe_);
        DisconnectNamedPipe(pipe_);
    }

    void ClientSession() {
        if (!attempt_requested_.exchange(false)) {
            detail::SleepMs(kTransportIdleSleepMs);
            return;
        }
        HANDLE h = CreateFileA(path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        if (!detail::PipeServerIsCurrentUser(h)) {
            CloseHandle(h);
            return;
        }
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
        pipe_ = h;
        IoLoop();
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }

    void IoLoop() {
        OVERLAPPED rov, wov;
        std::memset(&rov, 0, sizeof(rov));
        std::memset(&wov, 0, sizeof(wov));
        rov.hEvent = events_[1];
        wov.hEvent = events_[2];
        bool read_pending = false;
        bool write_pending = false;
        std::vector<unsigned char> rbuf(kPipeBufferSize);
        std::vector<unsigned char> wbuf;
        parser_.Reset();
        send_queue_.Clear();
        connected_.store(true, std::memory_order_release);
        bool alive = true;
        while (alive && !stop_.load(std::memory_order_relaxed)) {
            if (!read_pending) {
                ResetEvent(rov.hEvent);
                BOOL ok = ReadFile(pipe_, rbuf.data(), (DWORD)rbuf.size(), nullptr, &rov);
                DWORD err = ok ? ERROR_SUCCESS : GetLastError();
                if (err == ERROR_IO_PENDING) {
                    read_pending = true;
                } else if (ok || err == ERROR_MORE_DATA) {
                    alive = CompleteRead(rov, rbuf);
                } else {
                    alive = false;
                }
                if (!alive) break;
            }
            if (!write_pending) {
                if (wbuf.empty()) send_queue_.Pop(wbuf);
                if (!wbuf.empty()) {
                    ResetEvent(wov.hEvent);
                    BOOL ok = WriteFile(pipe_, wbuf.data(), (DWORD)wbuf.size(), nullptr, &wov);
                    DWORD err = ok ? ERROR_SUCCESS : GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        write_pending = true;
                    } else if (ok) {
                        alive = CompleteWrite(wov, wbuf);
                    } else {
                        alive = false;
                    }
                    if (!alive) break;
                }
            }
            if (!read_pending && !write_pending) continue;
            HANDLE handles[2];
            DWORD count = 0;
            if (read_pending) handles[count++] = rov.hEvent;
            if (write_pending) handles[count++] = wov.hEvent;
            DWORD w = WaitForMultipleObjects(count, handles, FALSE, kWaitMs);
            if (w == WAIT_TIMEOUT) continue;
            if (w >= WAIT_OBJECT_0 && w < WAIT_OBJECT_0 + count) {
                HANDLE fired = handles[w - WAIT_OBJECT_0];
                if (fired == rov.hEvent) {
                    read_pending = false;
                    alive = CompleteRead(rov, rbuf);
                } else {
                    write_pending = false;
                    alive = CompleteWrite(wov, wbuf);
                }
            } else {
                alive = false;
            }
        }
        connected_.store(false, std::memory_order_release);
        if (read_pending || write_pending) {
            CancelIo(pipe_);
            DWORD n = 0;
            if (read_pending) GetOverlappedResult(pipe_, &rov, &n, TRUE);
            if (write_pending) GetOverlappedResult(pipe_, &wov, &n, TRUE);
        }
        parser_.Reset();
        send_queue_.Clear();
    }

    bool CompleteRead(OVERLAPPED& ov, std::vector<unsigned char>& rbuf) {
        DWORD n = 0;
        BOOL ok = GetOverlappedResult(pipe_, &ov, &n, TRUE);
        if (!ok && GetLastError() != ERROR_MORE_DATA) return false;
        if (n == 0) return true;
        stats_.bytes_received.fetch_add(n, std::memory_order_relaxed);
        parser_.Append(rbuf.data(), n);
        std::vector<unsigned char> frame;
        while (parser_.Pop(frame)) {
            stats_.messages_received.fetch_add(1, std::memory_order_relaxed);
            size_t dropped = recv_queue_.Push(std::move(frame));
            if (dropped) stats_.drops.fetch_add(dropped, std::memory_order_relaxed);
        }
        return !parser_.Corrupt();
    }

    bool CompleteWrite(OVERLAPPED& ov, std::vector<unsigned char>& wbuf) {
        DWORD n = 0;
        if (!GetOverlappedResult(pipe_, &ov, &n, TRUE)) return false;
        stats_.bytes_sent.fetch_add(n, std::memory_order_relaxed);
        stats_.messages_sent.fetch_add(1, std::memory_order_relaxed);
        wbuf.clear();
        return true;
    }

    std::string path_;
    bool is_server_ = false;
    bool running_ = false;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE events_[3] = {nullptr, nullptr, nullptr};
    uint64_t last_attempt_ms_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> attempt_requested_{false};
    std::thread thread_;
    detail::FrameQueue send_queue_;
    detail::FrameQueue recv_queue_;
    detail::FrameParser parser_;
    detail::AtomicStats stats_;
};

#else

class PipeTransport : public Transport {
public:
    PipeTransport() {}
    ~PipeTransport() override { Close(); }

    bool Listen(const char* endpoint) override {
        Close();
        sockaddr_un addr;
        std::string path;
        if (!MakeAddr(endpoint, addr, path)) return false;
        struct stat st;
        if (::lstat(path.c_str(), &st) == 0) {
            if (!S_ISSOCK(st.st_mode) || st.st_uid != ::geteuid()) return false;
            ::unlink(path.c_str());
        }
        mode_t old_mask = ::umask(0177);
        bool ok = engine_.Listen((const sockaddr*)&addr, (socklen_t)sizeof(addr));
        ::umask(old_mask);
        if (!ok) return false;
        ::chmod(path.c_str(), 0600);
        bound_path_ = path;
        return true;
    }
    bool Connect(const char* endpoint) override {
        Close();
        sockaddr_un addr;
        std::string path;
        if (!MakeAddr(endpoint, addr, path)) return false;
        return engine_.Connect((const sockaddr*)&addr, (socklen_t)sizeof(addr));
    }
    bool IsConnected() const override { return engine_.IsConnected(); }
    bool Send(const void* data, size_t len) override { return engine_.Send(data, len); }
    bool Receive(std::vector<unsigned char>& out) override { return engine_.Receive(out); }
    void Poll() override { engine_.Poll(); }
    void Close() override {
        engine_.Close();
        if (!bound_path_.empty()) {
            ::unlink(bound_path_.c_str());
            bound_path_.clear();
        }
    }
    TransportStats Stats() const override { return engine_.Stats(); }

    static std::string SocketPath(const std::string& name) {
        const char* xdg = std::getenv("XDG_RUNTIME_DIR");
        if (xdg && xdg[0] == '/') {
            std::string dir = xdg;
            while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
            return dir + "/imforge-" + name + ".sock";
        }
        return "/tmp/imforge-" + name + ".sock";
    }

private:
    static bool MakeAddr(const char* endpoint, sockaddr_un& addr, std::string& path) {
        std::string name;
        if (!detail::StripScheme(endpoint, "pipe://", name)) return false;
        for (char c : name) {
            if (c == '/' || c == '\\') return false;
        }
        path = SocketPath(name);
        std::memset(&addr, 0, sizeof(addr));
        if (path.size() >= sizeof(addr.sun_path)) return false;
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        return true;
    }

    detail::SocketEngine engine_;
    std::string bound_path_;
};

#endif

}  // namespace imforge
