#pragma once

// bare-bones builder end of the live link: pairs over TCP like the real one and records what it receives

#include "imforge/imforge_pairing_session.h"
#include "imforge/imforge_protocol.h"
#include "imforge/imforge_transport.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace imforge {
namespace test {

// keep pairing files for both sides in a per-process temp dir
inline const std::string& UseTestPairingDir() {
    static std::string dir;
    if (dir.empty()) {
#ifdef _WIN32
        wchar_t buf[MAX_PATH + 1];
        DWORD n = GetTempPathW(MAX_PATH, buf);
        dir = pairing::file_detail::Narrow(std::wstring(buf, n)) + "imforge_pairing_tests_" + std::to_string((int)GetCurrentProcessId());
#else
        dir = "/tmp/imforge_pairing_tests_" + std::to_string((int)::getpid());
#endif
        pairing::SetPairingDir(dir.c_str());
    }
    return dir;
}

struct LiveTestBuilder {
    std::unique_ptr<Transport> t;
    pairing::Server pair;
    std::string url;
    std::vector<Json> received;
    bool was_connected = false;
    uint64_t drop_at = 0;
    int connects = 0;
    int drops = 0;
    bool listening = false;
    // 1: play port squatter, answer every hello with a challenge we can't prove; 2: answer with pair_fail.
    // the real pairing::Server never sees those hellos
    int squat = 0;

    bool Listen(bool start_pairing = true) {
        UseTestPairingDir();
        t = CreateTransport("tcp://127.0.0.1:0");
        if (!t || !t->Listen("tcp://127.0.0.1:0") || t->BoundPort() <= 0) return false;
        url = "tcp://127.0.0.1:" + std::to_string(t->BoundPort());
        listening = true;
        pair.endpoint = url;
        return !start_pairing || pair.Start(detail::NowMs());
    }
    const std::string& Url() const { return url; }

    // sealed; sends nothing until the session is up
    bool Send(const Json& m) {
        std::string s;
        return pair.Encode(m, s) && t->Send(s.data(), s.size());
    }
    // plain frame, paired or not (handshake replies, or faking an unauthenticated message)
    void SendPlain(const Json& m) {
        std::string s = Frame(m);
        t->Send(s.data(), s.size());
    }
    void SendBytes(const std::string& bytes) { t->Send(bytes.data(), bytes.size()); }

    void Drop() {
        uint64_t now = detail::NowMs();
        drop_at = 0;
        if (was_connected) pair.OnDisconnect(now);
        was_connected = false;
        t->Close();
        listening = !pair.Refusing(now) && t->Listen(url.c_str());
    }

    void Pump() {
        uint64_t now = detail::NowMs();
        if (!listening) {
            if (pair.Running()) pair.Tick(now);
            if (pair.Refusing(now) || !(listening = t->Listen(url.c_str()))) return;
        }
        t->Poll();
        bool connected = t->IsConnected();
        if (connected && !was_connected) {
            ++connects;
            pair.OnConnect(now);
        } else if (!connected && was_connected) {
            pair.OnDisconnect(now);
        }
        was_connected = connected;
        std::vector<unsigned char> buf;
        while (drop_at == 0 && t->Receive(buf)) {
            Json m, reply;
            if (!pair.Running() || squat != 0) {
                if (!Unframe(buf, m)) continue;
                received.push_back(m);
                if (squat != 0 && IsMsg(m, msg::kHello))
                    SendPlain(squat == 1 ? MsgPairChallenge(pairing::RandomHex(16), pairing::RandomHex(16), pairing::RandomHex(32))
                                         : MsgPairFail(0, "squatter says no"));
                continue;
            }
            pairing::Server::Verdict v = pair.OnFrame(buf, now, m, reply);
            if (!m.IsNull()) received.push_back(m);
            if (!reply.IsNull()) SendPlain(reply);
            if (v == pairing::Server::Verdict::ReplyAndDrop) drop_at = now + 100;
            if (v == pairing::Server::Verdict::Drop) {
                ++drops;
                drop_at = now;
            }
        }
        if (pair.Running() && pair.Tick(now) && drop_at == 0) drop_at = now;
        if (drop_at != 0 && now >= drop_at) Drop();
        pair.TakeLog();
    }

    size_t Count(const char* type) const {
        size_t n = 0;
        for (const Json& m : received)
            if (IsMsg(m, type)) ++n;
        return n;
    }
    const Json* Last(const char* type) const {
        for (size_t i = received.size(); i > 0; --i)
            if (IsMsg(received[i - 1], type)) return &received[i - 1];
        return nullptr;
    }
    const Json* First(const char* type) const {
        for (const Json& m : received)
            if (IsMsg(m, type)) return &m;
        return nullptr;
    }
};

}  // namespace test
}  // namespace imforge
