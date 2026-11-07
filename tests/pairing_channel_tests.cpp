#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#ifndef IMFORGE_LIVE
#define IMFORGE_LIVE
#endif
#define IMFORGE_BACKEND_NAME "headless"
#include "imforge/imforge_client.h"
#include "imgui_headless.h"
#include "live_test_builder.h"

#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// per-message auth on the paired live link (imforge_pairing_channel.h)

using namespace imforge;
namespace live = imforge::live;
namespace pr = imforge::pairing;
namespace cd = imforge::live::client_detail;
using V = pr::Server::Verdict;
using R = pr::ClientHandshake::Result;
using CS = pr::Channel::Status;

namespace {

std::string Unhex(const std::string& hex) {
    std::string out(hex.size() / 2, '\0');
    REQUIRE(pr::FromHex(hex, &out[0], out.size()));
    return out;
}

std::string FileCode(const std::string& path) {
    pr::PairingFileData f;
    std::string err;
    if (!pr::ReadPairingFile(path, f, &err)) return "<" + err + ">";
    return f.code;
}

// builder (pairing::Server) and app (pairing::ClientHandshake) paired in-process. the handshake values are
// kept around so tests can re-derive the keys themselves
struct PairedSession {
    pr::Server s;
    pr::ClientHandshake app;
    std::string code, nonce_a, nonce_b, sid;
    uint64_t now = 1000;

    explicit PairedSession(const char* endpoint) {
        test::UseTestPairingDir();
        s.endpoint = endpoint;
        s.write_file = false;
        REQUIRE(s.Start(now));
        code = s.Code();
        REQUIRE(app.Begin(code));
        nonce_a = app.NonceA();
        s.OnConnect(now);
        Json reply, answer;
        REQUIRE(s.OnMessage(MsgPairingHello(nonce_a), now, reply) == V::Reply);
        PairChallengeMsg ch;
        REQUIRE(ParsePairChallenge(reply, ch));
        nonce_b = ch.nonce_b;
        sid = ch.session_id;
        REQUIRE(app.OnMessage(reply, answer) == R::Reply);
        REQUIRE(s.OnMessage(answer, now, reply) == V::Paired);
        REQUIRE(app.OnMessage(reply, answer) == R::Paired);
        s.TakeLog();
    }
    std::string AppSeal(const Json& m) {
        std::string out;
        REQUIRE(app.Seal(Frame(m), out));
        return out;
    }
    std::string BuilderSeal(const Json& m) {
        std::string out;
        REQUIRE(s.Encode(m, out));
        return out;
    }
    V ToBuilder(const std::string& frame, Json* got = nullptr) {
        Json m, reply;
        V v = s.OnFrame(frame.data(), frame.size(), now, m, reply);
        if (got) *got = m;
        return v;
    }
    CS ToApp(const std::string& frame, Json* got = nullptr) {
        const unsigned char* p = nullptr;
        size_t n = 0;
        CS st = app.Open(frame.data(), frame.size(), &p, &n);
        if (st == CS::Ok && got) REQUIRE(Unframe(p, n, *got));
        return st;
    }
    // HKDF-SHA256 re-derived from the primitives (single-block expand: T(1) = HMAC(PRK, info || 0x01)).
    std::string ExpectedKey(const char* label) const {
        unsigned char k[32], prk[32], out[32];
        REQUIRE(pr::DeriveKey(code, k));
        std::string salt = Unhex(nonce_a + nonce_b + sid);
        pr::HmacSha256(salt.data(), salt.size(), k, 32, prk);
        std::string info = std::string(label) + '\x01';
        pr::HmacSha256(prk, 32, info.data(), info.size(), out);
        return std::string((const char*)out, 32);
    }
};

std::string Mac(const std::string& key, const std::string& data) {
    unsigned char out[32];
    pr::HmacSha256(key.data(), key.size(), data.data(), data.size(), out);
    return std::string((const char*)out, 32);
}

}  // namespace

TEST_CASE("HKDF-SHA256 matches RFC 5869 test cases 1 and 3") {
    std::string ikm(22, '\x0b'), salt, info;
    for (int i = 0; i <= 0x0c; ++i) salt.push_back((char)i);
    for (int i = 0xf0; i <= 0xf9; ++i) info.push_back((char)i);
    unsigned char prk[32], okm[42];
    pr::HkdfExtract(salt.data(), salt.size(), ikm.data(), ikm.size(), prk);
    CHECK(pr::ToHex(prk, 32) == "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
    REQUIRE(pr::HkdfExpand(prk, info.data(), info.size(), okm, sizeof(okm)));
    CHECK(pr::ToHex(okm, 42) == "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");

    pr::HkdfExtract(nullptr, 0, ikm.data(), ikm.size(), prk);
    CHECK(pr::ToHex(prk, 32) == "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04");
    REQUIRE(pr::HkdfExpand(prk, nullptr, 0, okm, sizeof(okm)));
    CHECK(pr::ToHex(okm, 42) == "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8");
    CHECK_FALSE(pr::HkdfExpand(prk, nullptr, 0, okm, 255 * 32 + 1));
}

TEST_CASE("wire format: known-answer vector for the direction keys and the first sealed frame") {
    unsigned char k[32];
    REQUIRE(pr::DeriveKey("0123-4567-89AB-CDEF-GHJK-MNPQ-RSTV-WXYZ", k));
    const std::string na = "000102030405060708090a0b0c0d0e0f", nb = "101112131415161718191a1b1c1d1e1f",
                      sid = "202122232425262728292a2b2c2d2e2f";
    pr::Channel builder, app;
    REQUIRE(builder.Init(k, na, nb, sid, true));
    REQUIRE(app.Init(k, na, nb, sid, false));
    std::string frame;
    REQUIRE(builder.Seal(std::string("{\"t\":\"ping\"}"), frame));
    REQUIRE(frame.size() == pr::kChannelHeaderBytes + 12);
    CHECK(pr::ToHex(frame.data(), 32) == "62d795431d3c3506366db465594e7df2f7a69b03aeae01a485328eccd2a955c8");
    CHECK(pr::ToHex(frame.data() + 32, 8) == "0000000000000001");
    CHECK(frame.substr(40) == "{\"t\":\"ping\"}");
    const unsigned char* p = nullptr;
    size_t n = 0;
    REQUIRE(app.Open(frame.data(), frame.size(), &p, &n) == CS::Ok);
    CHECK(std::string((const char*)p, n) == "{\"t\":\"ping\"}");
    REQUIRE(app.Seal(std::string("{\"t\":\"pong\"}"), frame));
    CHECK(pr::ToHex(frame.data(), 32) == "4f655524a7a72860b90f50f5722854cd9ac8b5d5f1adf35807cc2a3bf7387b9a");
    REQUIRE(builder.Open(frame.data(), frame.size(), &p, &n) == CS::Ok);
    CHECK_FALSE(builder.Init(k, "XYZ", nb, sid, true));
    CHECK_FALSE(builder.Init(k, std::string(32, 'A'), nb, sid, true));
    CHECK_FALSE(builder.Active());
}

TEST_CASE("paired endpoints derive matching, direction-separated keys from K, both nonces and the session id") {
    PairedSession p("tcp://127.0.0.1:65120");
    std::string b2a = p.ExpectedKey(pr::kChannelLabelB2A), a2b = p.ExpectedKey(pr::kChannelLabelA2B);
    CHECK(b2a != a2b);

    std::string f = p.BuilderSeal(MsgPing());
    CHECK(f.substr(pr::kChannelMacBytes, 8) == std::string("\0\0\0\0\0\0\0\x01", 8));
    CHECK(f.substr(pr::kChannelHeaderBytes) == Frame(MsgPing()));
    CHECK(f.substr(0, 32) == Mac(b2a, f.substr(32)));
    Json got;
    REQUIRE(p.ToApp(f, &got) == CS::Ok);
    CHECK(IsMsg(got, msg::kPing));

    std::string g = p.AppSeal(MsgClientInfo("KeyTest", 7, 1, "b", "p", {}, {}));
    CHECK(g.substr(0, 32) == Mac(a2b, g.substr(32)));
    REQUIRE(p.ToBuilder(g, &got) == V::Accept);
    CHECK(IsMsg(got, msg::kClientInfo));
    CHECK(p.s.PeerApp() == "KeyTest");

    std::string f2 = p.BuilderSeal(MsgPing());
    CHECK(f2.substr(pr::kChannelMacBytes, 8) == std::string("\0\0\0\0\0\0\0\x02", 8));
    CHECK(f2.substr(0, 32) != f.substr(0, 32));
    REQUIRE(p.ToApp(f2) == CS::Ok);
    CHECK(p.s.SessionChannel().SentCount() == 2);
    CHECK(p.s.SessionChannel().ReceivedCount() == 1);
}

TEST_CASE("a single flipped bit anywhere in an app frame drops the link before any JSON is parsed") {
    const Json m = MsgSelect("MainMenu", "btn_play");
    size_t len = Frame(m).size() + pr::kChannelHeaderBytes;
    const size_t positions[] = {0, 17, 31, 32, 38, 39, 40, 41, len / 2, len - 1};
    int port = 65121;
    for (size_t pos : positions) {
        INFO("byte " << pos);
        PairedSession p(("tcp://127.0.0.1:" + std::to_string(port)).c_str());
        std::string f = p.AppSeal(m);
        REQUIRE(f.size() == len);
        f[pos] = (char)(f[pos] ^ 0x04);
        Json got;
        CHECK(p.ToBuilder(f, &got) == V::Drop);
        CHECK(got.IsNull());
        CHECK(p.s.SecurityEvents() == 1);
        CHECK(p.s.LastSecurityEvent().find("MAC mismatch") != std::string::npos);
        CHECK_FALSE(p.s.Established());
        CHECK(p.s.SessionChannel().KeysWiped());
        std::vector<pr::LogLine> log = p.s.TakeLog();
        REQUIRE_FALSE(log.empty());
        CHECK(log.back().security);
        CHECK(log.back().level == 3);
        CHECK(p.ToBuilder(p.AppSeal(MsgPong())) != V::Accept);
    }
    PairedSession p("tcp://127.0.0.1:65122");
    std::string f = p.AppSeal(m);
    CHECK(p.ToBuilder(f.substr(0, pr::kChannelHeaderBytes - 1)) == V::Drop);
    CHECK(p.s.LastSecurityEvent().find("too short") != std::string::npos);
}

TEST_CASE("the app wipes its keys on a tampered builder frame") {
    PairedSession p("tcp://127.0.0.1:65123");
    std::string f = p.BuilderSeal(MsgPanel(Json::Object()));
    f[f.size() - 2] = (char)(f[f.size() - 2] ^ 0x01);
    CHECK(p.ToApp(f) == CS::BadMac);
    CHECK(p.app.SessionChannel().KeysWiped());
    CHECK_FALSE(p.app.SessionChannel().Active());
    std::string out;
    CHECK_FALSE(p.app.Seal(Frame(MsgPong()), out));
    CHECK(p.ToApp(p.BuilderSeal(MsgPing())) == CS::Inactive);
}

TEST_CASE("replayed, reordered and dropped frames end the session") {
    {
        INFO("replay");
        PairedSession p("tcp://127.0.0.1:65124");
        std::string f = p.AppSeal(MsgHover("MainMenu", "a"));
        REQUIRE(p.ToBuilder(f) == V::Accept);
        CHECK(p.ToBuilder(f) == V::Drop);
        CHECK(p.s.LastSecurityEvent().find("expected #2, got #1") != std::string::npos);
        CHECK(p.s.SecurityEvents() == 1);
    }
    {
        INFO("reorder");
        PairedSession p("tcp://127.0.0.1:65125");
        std::string f1 = p.AppSeal(MsgHover("MainMenu", "a")), f2 = p.AppSeal(MsgHover("MainMenu", "b"));
        CHECK(p.ToBuilder(f2) == V::Drop);
        CHECK(p.s.LastSecurityEvent().find("expected #1, got #2") != std::string::npos);
        CHECK(p.ToBuilder(f1) != V::Accept);
    }
    {
        INFO("gap (a frame lost or withheld by a relay)");
        PairedSession p("tcp://127.0.0.1:65126");
        std::string f1 = p.AppSeal(MsgLog(1, "one")), f2 = p.AppSeal(MsgLog(1, "two")), f3 = p.AppSeal(MsgLog(1, "three"));
        REQUIRE(p.ToBuilder(f1) == V::Accept);
        CHECK(p.ToBuilder(f3) == V::Drop);
        (void)f2;
    }
    {
        INFO("builder -> app replay");
        PairedSession p("tcp://127.0.0.1:65127");
        std::string f = p.BuilderSeal(MsgMode(true, "", ""));
        REQUIRE(p.ToApp(f) == CS::Ok);
        CHECK(p.ToApp(f) == CS::BadSequence);
        CHECK(p.app.Describe(CS::BadSequence).find("expected #2, got #1") != std::string::npos);
        CHECK(p.app.SessionChannel().KeysWiped());
    }
}

TEST_CASE("frames from another session or reflected from the other direction are rejected") {
    PairedSession a("tcp://127.0.0.1:65128"), b("tcp://127.0.0.1:65129");
    CHECK(b.ToBuilder(a.AppSeal(MsgPong())) == V::Drop);
    CHECK(a.ToBuilder(a.BuilderSeal(MsgPing())) == V::Drop);
    PairedSession c("tcp://127.0.0.1:65130");
    CHECK(c.ToApp(c.AppSeal(MsgPong())) == CS::BadMac);
}

TEST_CASE("before pair_ok nothing is sealed or accepted, and client_info counts only on the paired link") {
    test::UseTestPairingDir();
    pr::Server s;
    s.endpoint = "tcp://127.0.0.1:65131";
    s.write_file = false;
    REQUIRE(s.Start(1000));
    s.OnConnect(1000);
    std::string out;
    CHECK_FALSE(s.Encode(MsgPing(), out));
    CHECK(out.empty());
    Json m, reply;
    std::string info = Frame(MsgClientInfo("Early", 1, 1, "b", "p", {}, {}));
    CHECK(s.OnFrame(info.data(), info.size(), 1000, m, reply) == V::Ignore);
    CHECK_FALSE(s.ClientInfoReceived());
    CHECK(s.PeerApp().empty());
    std::string fake_sealed = std::string(pr::kChannelHeaderBytes, '\x01') + Frame(MsgSelect("MainMenu", "x"));
    CHECK(s.OnFrame(fake_sealed.data(), fake_sealed.size(), 1000, m, reply) == V::Ignore);

    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(s.Code()));
    CHECK_FALSE(hs.Seal(Frame(MsgPong()), out));
    CHECK(hs.Open(fake_sealed.data(), fake_sealed.size(), nullptr, nullptr) == CS::Inactive);
    std::string hello = Frame(MsgPairingHello(hs.NonceA()));
    REQUIRE(s.OnFrame(hello.data(), hello.size(), 1000, m, reply) == V::Reply);
    Json challenge = reply;
    CHECK(s.OnFrame(info.data(), info.size(), 1000, m, reply) == V::Ignore);
    CHECK(s.GetState() == pr::Server::State::Challenged);
    Json answer;
    REQUIRE(hs.OnMessage(challenge, answer) == R::Reply);
    CHECK_FALSE(hs.Seal(Frame(MsgPong()), out));
    std::string resp = Frame(answer);
    REQUIRE(s.OnFrame(resp.data(), resp.size(), 1000, m, reply) == V::Paired);
    REQUIRE(hs.OnMessage(reply, answer) == R::Paired);
    CHECK_FALSE(s.ClientInfoReceived());
    INFO("the plain client_info that worked before pairing is now an unsealed frame on a paired link");
    std::string sealed;
    REQUIRE(hs.Seal(info, sealed));
    CHECK(s.OnFrame(sealed.data(), sealed.size(), 1000, m, reply) == V::Accept);
    CHECK(s.ClientInfoReceived());
    CHECK(s.PeerApp() == "Early");
    CHECK(s.OnFrame(info.data(), info.size(), 1000, m, reply) == V::Drop);
    CHECK(s.SecurityEvents() == 1);
    CHECK_FALSE(s.ClientInfoReceived());
}

// end to end over TCP with the real client
namespace {

struct Loop {
    test::HeadlessImGui ui;
    test::LiveTestBuilder b;

    void Frame() {
        b.Pump();
        ui.BeginFrame();
        live::NewFrame();
        live::Draw("Injected", {});
        ui.EndFrame();
        b.Pump();
    }
    bool Until(const std::function<bool()>& pred, uint64_t timeout_ms = 6000) {
        uint64_t deadline = detail::NowMs() + timeout_ms;
        for (;;) {
            Frame();
            if (pred()) return true;
            if (detail::NowMs() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    bool Paired(int pairings) {
        return Until([&] { return live::IsPaired() && b.pair.Pairings() == pairings && b.pair.ClientInfoReceived(); });
    }
};

Json InjectedPanel() {
    Json j;
    const char* text = R"({"schemaVersion":1,"kind":"panel","name":"Injected",
      "root":{"id":"window","type":"Window","props":{"title":"Injected"},"children":[{"id":"t","type":"Text"}]}})";
    REQUIRE(ParseJson(text, std::strlen(text), j));
    return MsgPanel(j);
}

}  // namespace

TEST_CASE("long session: 10k sealed messages each way over tcp") {
    Loop l;
    REQUIRE(l.b.Listen());
    live::SetAppName("LongSession");
    REQUIRE(live::Connect(l.b.Url().c_str()));
    REQUIRE_MESSAGE(l.Paired(1), live::Status());
    const size_t n = 10000;
    size_t pongs = l.b.Count(msg::kPong);
    for (size_t i = 0; i < n; ++i) REQUIRE(l.b.Send(MsgPing()));
    REQUIRE_MESSAGE(l.Until([&] { return l.b.Count(msg::kPong) >= pongs + n; }, 60000),
                    "pongs: " << l.b.Count(msg::kPong) - pongs << " status: " << live::Status());
    REQUIRE(l.b.Send(InjectedPanel()));
    REQUIRE(l.Until([&] { return live::DebugPanelCount() == 1; }));
    cd::Client& c = cd::Get();
    CHECK(l.b.pair.SecurityEvents() == 0);
    CHECK(c.security_events == 0);
    CHECK(l.b.drops == 0);
    CHECK(l.b.pair.SessionChannel().SentCount() >= n + 1);
    CHECK(l.b.pair.SessionChannel().ReceivedCount() >= n + 1);
    CHECK(c.handshake.SessionChannel().ReceivedCount() == l.b.pair.SessionChannel().SentCount());
    CHECK(l.b.pair.Pairings() == 1);
    MESSAGE("long session: builder sent " << l.b.pair.SessionChannel().SentCount() << " sealed frames, received "
                                          << l.b.pair.SessionChannel().ReceivedCount());
    live::Disconnect();
    l.Until([] { return false; }, 100);
    c.panels.clear();
}

TEST_CASE("the app drops a tampered, replayed or unsealed builder frame, applies nothing and re-pairs") {
    Loop l;
    REQUIRE(l.b.Listen());
    live::SetAppName("TamperApp");
    REQUIRE(live::Connect(l.b.Url().c_str()));
    REQUIRE_MESSAGE(l.Paired(1), live::Status());
    cd::Client& c = cd::Get();
    c.panels.clear();
    const char* kinds[] = {"tampered", "replayed", "unsealed"};
    for (int kind = 0; kind < 3; ++kind) {
        INFO(kinds[kind]);
        int events = c.security_events;
        int pairings = l.b.pair.Pairings();
        std::string f;
        if (kind == 0) {
            REQUIRE(l.b.pair.Encode(InjectedPanel(), f));
            f[f.size() - 3] = (char)(f[f.size() - 3] ^ 0x02);
            l.b.SendBytes(f);
        } else if (kind == 1) {
            REQUIRE(l.b.pair.Encode(InjectedPanel(), f));
            l.b.SendBytes(f);
            l.b.SendBytes(f);
        } else {
            l.b.SendPlain(InjectedPanel());
        }
        REQUIRE(l.Until([&] { return c.security_events == events + 1; }));
        CHECK_FALSE(live::IsPaired());
        CHECK(std::string(live::LastError()).compare(0, 9, "security:") == 0);
        CHECK(c.handshake.SessionChannel().KeysWiped());
        CHECK(live::DebugPanelCount() == (kind == 1 ? 1u : 0u));
        c.panels.clear();
        REQUIRE_MESSAGE(l.Paired(pairings + 1), live::Status());
        CHECK(l.b.pair.SecurityEvents() == 0);
    }
    live::Disconnect();
    l.Until([] { return false; }, 100);
}

TEST_CASE("the builder drops a tampered app frame, applies nothing and the app re-pairs") {
    Loop l;
    REQUIRE(l.b.Listen());
    live::SetAppName("TamperBuilder");
    REQUIRE(live::Connect(l.b.Url().c_str()));
    REQUIRE_MESSAGE(l.Paired(1), live::Status());
    cd::Client& c = cd::Get();
    std::string f;
    REQUIRE(c.handshake.Seal(Frame(MsgSelect("MainMenu", "forged")), f));
    f[pr::kChannelHeaderBytes + 3] = (char)(f[pr::kChannelHeaderBytes + 3] ^ 0x08);
    c.transport->Send(f.data(), f.size());
    REQUIRE(l.Until([&] { return l.b.drops == 1; }));
    CHECK(l.b.pair.SecurityEvents() == 1);
    CHECK(l.b.pair.LastSecurityEvent().find("MAC mismatch") != std::string::npos);
    CHECK(l.b.Count(msg::kSelect) == 0);
    REQUIRE_MESSAGE(l.Paired(2), live::Status());
    live::Disconnect();
    l.Until([] { return false; }, 100);
}

TEST_CASE("a port squatter neither burns the app's pairing-file code nor advances the builder's attempt counter") {
    Loop l;
    REQUIRE(l.b.Listen());
    std::string code = FileCode(l.b.pair.FilePath());
    REQUIRE(pr::IsValidCode(code));
    int gen = l.b.pair.Generation();
    cd::Client& c = cd::Get();
    live::SetAppName("SquatterVictim");
    for (int mode = 1; mode <= 2; ++mode) {
        INFO((mode == 1 ? "squatter answers with a challenge it cannot prove" : "squatter answers with pair_fail"));
        l.b.squat = mode;
        if (mode == 1) REQUIRE(live::Connect(l.b.Url().c_str()));
        size_t hellos = l.b.Count(msg::kHello);
        REQUIRE_MESSAGE(l.Until([&] { return l.b.Count(msg::kHello) >= hellos + 2; }, 12000), live::Status());
        CHECK_FALSE(live::IsPaired());
        CHECK(c.pair_error.find(mode == 1 ? "could not prove" : "rejected") != std::string::npos);
        CHECK_FALSE(cd::IsBurned(c, cd::CodeDigest(code)));
        CHECK(l.b.pair.TotalFailures() == 0);
        CHECK(l.b.pair.Generation() == gen);
        CHECK(FileCode(l.b.pair.FilePath()) == code);
    }
    for (const Json& m : l.b.received) {
        if (!IsMsg(m, msg::kHello)) continue;
        CHECK(m.Size() == 4);
        CHECK_FALSE(m["app"].IsString());
        CHECK_FALSE(m["pid"].IsNumber());
        CHECK_FALSE(m["widgets"].IsArray());
    }
    l.b.squat = 0;
    REQUIRE_MESSAGE(l.Paired(1), live::Status());
    CHECK(l.b.pair.Generation() == gen + 1);
    CHECK(cd::IsBurned(c, cd::CodeDigest(code)));
    CHECK(l.b.pair.TotalFailures() == 0);
    live::Disconnect();
    l.Until([] { return false; }, 100);
}

TEST_CASE("performance: MAC overhead for 10k 64 KB messages") {
    unsigned char k[32];
    REQUIRE(pr::RandomBytes(k, sizeof(k)));
    const std::string na = pr::RandomHex(16), nb = pr::RandomHex(16), sid = pr::RandomHex(16);
    pr::Channel tx, rx;
    REQUIRE(tx.Init(k, na, nb, sid, true));
    REQUIRE(rx.Init(k, na, nb, sid, false));
    const size_t kCount = 10000, kSize = 64 * 1024;
    std::string text(kSize - 64, 'x');
    for (size_t i = 0; i < text.size(); i += 61) text[i] = (char)('a' + i % 26);
    std::string payload = Frame(MsgLog(1, text.c_str()));
    payload.resize(kSize, ' ');
    using Clock = std::chrono::steady_clock;
    double seal_s = 0, open_s = 0;
    size_t ok = 0;
    std::string sealed;
    for (size_t i = 0; i < kCount; ++i) {
        Clock::time_point t0 = Clock::now();
        tx.Seal(payload, sealed);
        Clock::time_point t1 = Clock::now();
        const unsigned char* p = nullptr;
        size_t n = 0;
        if (rx.Open(sealed.data(), sealed.size(), &p, &n) == CS::Ok && n == kSize) ++ok;
        Clock::time_point t2 = Clock::now();
        seal_s += std::chrono::duration<double>(t1 - t0).count();
        open_s += std::chrono::duration<double>(t2 - t1).count();
    }
    CHECK(ok == kCount);
    CHECK(rx.ReceivedCount() == kCount);

    const size_t kJson = 200;
    std::string wire = Frame(MsgLog(1, text.c_str()));
    Clock::time_point j0 = Clock::now();
    size_t parsed = 0;
    for (size_t i = 0; i < kJson; ++i) {
        Json m;
        if (Unframe(wire.data(), wire.size(), m)) ++parsed;
    }
    double json_s = std::chrono::duration<double>(Clock::now() - j0).count();
    CHECK(parsed == kJson);

    const double mb = (double)kCount * (double)kSize / (1024.0 * 1024.0);
    const double per_msg_mac_us = (seal_s + open_s) / (double)kCount * 1e6;
    const double per_msg_json_us = json_s / (double)kJson * 1e6;
    MESSAGE("MAC (HMAC-SHA256 + sequence): " << kCount << " x 64 KB = " << mb << " MB; seal " << mb / seal_s << " MB/s, open "
                                             << mb / open_s << " MB/s; " << per_msg_mac_us << " us/message both sides vs "
                                             << per_msg_json_us << " us to parse the same message as JSON once");
    CHECK(mb / seal_s > 1.0);
    CHECK(mb / open_s > 1.0);
}
