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
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace imforge;
namespace live = imforge::live;
namespace pr = imforge::pairing;
using V = pr::Server::Verdict;
using R = pr::ClientHandshake::Result;

namespace {

Json Hello(const std::string& nonce) { return MsgPairingHello(nonce); }

void StartServer(pr::Server& s, const char* endpoint, uint64_t now) {
    test::UseTestPairingDir();
    s.endpoint = endpoint;
    REQUIRE(s.Start(now));
    s.TakeLog();
}

std::string Upper(std::string s) {
    for (char& c : s)
        if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
    return s;
}

}  // namespace

TEST_CASE("builder refuses malformed nonce_a (length, case, charset) instead of building a MAC from it") {
    pr::Server s;
    StartServer(s, "tcp://127.0.0.1:65101", 1000);
    const std::string bad[] = {std::string(31, 'a'),         std::string(33, 'a'), std::string(64, 'a'), std::string(32, 'g'),
                               Upper(pr::RandomHex(16)),     std::string("0x") + std::string(30, 'a'),
                               std::string(16, 'a') + std::string(1, '\0') + std::string(15, 'a'), std::string()};
    for (const std::string& n : bad) {
        s.OnConnect(1000);
        Json reply;
        V v = s.OnMessage(Hello(n), 1000, reply);
        CHECK(v != V::Reply);
        CHECK_FALSE(s.Established());
        CHECK(s.GetState() != pr::Server::State::Challenged);
        s.OnDisconnect(1000);
    }
    CHECK(s.Failures() == 0);
}

TEST_CASE("pair_response with an upper-case or padded MAC is not accepted even when the value is right") {
    pr::Server s;
    StartServer(s, "tcp://127.0.0.1:65102", 1000);
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(s.Code()));
    s.OnConnect(1000);
    Json reply, answer;
    REQUIRE(s.OnMessage(Hello(hs.NonceA()), 1000, reply) == V::Reply);
    REQUIRE(hs.OnMessage(reply, answer) == R::Reply);
    PairResponseMsg r;
    REQUIRE(ParsePairResponse(answer, r));
    CHECK(s.OnMessage(MsgPairResponse(r.session_id, Upper(r.mac_a)), 1000, reply) == V::ReplyAndDrop);
    CHECK_FALSE(s.Established());
}

TEST_CASE("reflection: builder's MAC cannot be replayed as the app's MAC or the other way round") {
    pr::Server s;
    StartServer(s, "tcp://127.0.0.1:65103", 1000);
    pr::ClientHandshake attacker_view;
    REQUIRE(attacker_view.Begin(pr::GenerateCode()));
    s.OnConnect(1000);
    Json reply;
    REQUIRE(s.OnMessage(Hello(attacker_view.NonceA()), 1000, reply) == V::Reply);
    PairChallengeMsg ch;
    REQUIRE(ParsePairChallenge(reply, ch));
    CHECK(s.OnMessage(MsgPairResponse(ch.session_id, ch.mac_b), 1000, reply) == V::ReplyAndDrop);
    CHECK_FALSE(s.Established());
    s.OnDisconnect(1000);

    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(s.Code()));
    s.OnConnect(1000);
    Json answer;
    REQUIRE(s.OnMessage(Hello(hs.NonceA()), 1000, reply) == V::Reply);
    REQUIRE(ParsePairChallenge(reply, ch));
    unsigned char k[32];
    REQUIRE(pr::DeriveKey(s.Code(), k));
    std::string app_mac = pr::HandshakeMac(k, pr::kAppLabel, hs.NonceA(), ch.nonce_b, ch.session_id);
    CHECK(app_mac != ch.mac_b);
    Json forged = MsgPairChallenge(ch.nonce_b, ch.session_id, app_mac);
    CHECK(hs.OnMessage(forged, answer) == R::Failed);
    pr::SecureWipe(k, sizeof(k));
}

TEST_CASE("a pair_response bound to another session id or an earlier handshake is refused") {
    pr::Server s;
    StartServer(s, "tcp://127.0.0.1:65104", 1000);
    std::string code = s.Code();
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(code));
    s.OnConnect(1000);
    Json reply, answer;
    REQUIRE(s.OnMessage(Hello(hs.NonceA()), 1000, reply) == V::Reply);
    REQUIRE(hs.OnMessage(reply, answer) == R::Reply);
    Json recorded = answer;
    PairResponseMsg r;
    REQUIRE(ParsePairResponse(answer, r));
    CHECK(s.OnMessage(MsgPairResponse(pr::RandomHex(16), r.mac_a), 1000, reply) == V::ReplyAndDrop);
    s.OnDisconnect(1000);

    s.OnConnect(1000);
    pr::ClientHandshake hs2;
    REQUIRE(hs2.Begin(code));
    REQUIRE(s.OnMessage(Hello(hs2.NonceA()), 1000, reply) == V::Reply);
    CHECK(s.OnMessage(recorded, 1000, reply) == V::ReplyAndDrop);
    CHECK_FALSE(s.Established());
}

namespace {
void WrongResponse(pr::Server& s) {
    s.OnConnect(1000);
    Json reply;
    pr::ClientHandshake junk;
    REQUIRE(junk.Begin(pr::GenerateCode()));
    REQUIRE(s.OnMessage(Hello(junk.NonceA()), 1000, reply) == V::Reply);
    PairChallengeMsg ch;
    REQUIRE(ParsePairChallenge(reply, ch));
    CHECK(s.OnMessage(MsgPairResponse(ch.session_id, pr::RandomHex(32)), 1000, reply) == V::ReplyAndDrop);
    s.OnDisconnect(1000);
}
}  // namespace

TEST_CASE("failed attempts accumulate per builder across connections and cannot be reset by reconnecting") {
    pr::Server s;
    StartServer(s, "tcp://127.0.0.1:65105", 1000);
    std::string first = s.Code();
    for (int i = 0; i < 4; ++i) {
        WrongResponse(s);
        CHECK(s.Failures() == i + 1);
        s.OnConnect(1000);
        s.OnDisconnect(1000);
        CHECK(s.Failures() == i + 1);
    }
    CHECK(s.Code() == first);
    WrongResponse(s);
    CHECK(s.Code() != first);
    CHECK(s.Refusing(1000));
}

TEST_CASE("abandoned handshakes (squatter relays, stale apps, timeouts) neither count nor rotate the code") {
    pr::Server s;
    StartServer(s, "tcp://127.0.0.1:65111", 1000);
    std::string first = s.Code();
    int gen = s.Generation();
    for (int i = 0; i < 3 * pr::kMaxFailures; ++i) {
        s.OnConnect(1000);
        Json reply;
        pr::ClientHandshake other;
        REQUIRE(other.Begin(pr::GenerateCode()));
        REQUIRE(s.OnMessage(Hello(other.NonceA()), 1000, reply) == V::Reply);
        if (i % 2) CHECK(s.Tick(1000 + pr::kHandshakeTimeoutMs));
        s.OnDisconnect(1000 + pr::kHandshakeTimeoutMs);
    }
    CHECK(s.Failures() == 0);
    CHECK(s.TotalFailures() == 0);
    CHECK(s.AbandonedHandshakes() == 3 * pr::kMaxFailures);
    CHECK(s.Code() == first);
    CHECK(s.Generation() == gen);
    CHECK_FALSE(s.Refusing(1000 + pr::kHandshakeTimeoutMs));
}

TEST_CASE("frame budget before pairing is small and unlimited only after pairing") {
    pr::Server s;
    StartServer(s, "tcp://127.0.0.1:65106", 1000);
    s.OnConnect(1000);
    CHECK(s.MaxFrameBytes() == pr::kMaxHandshakeBytes);
    Json big = Json::Object(), m, reply0;
    big.Set("t", "hello");
    big.Set("pad", std::string(pr::kMaxHandshakeBytes, 'x'));
    std::string wire = Frame(big);
    CHECK(s.OnFrame(wire.data(), wire.size(), 1000, m, reply0) == V::Drop);
    s.OnDisconnect(1000);
    s.require = false;
    s.OnConnect(1000);
    CHECK(s.MaxFrameBytes() == pr::kMaxHelloBytes);
    s.OnDisconnect(1000);
    s.require = true;
    s.OnConnect(1000);
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(s.Code()));
    Json reply, answer;
    REQUIRE(s.OnMessage(Hello(hs.NonceA()), 1000, reply) == V::Reply);
    CHECK(s.MaxFrameBytes() == pr::kMaxHandshakeBytes);
    REQUIRE(hs.OnMessage(reply, answer) == R::Reply);
    REQUIRE(s.OnMessage(answer, 1000, reply) == V::Paired);
    CHECK(s.MaxFrameBytes() == 0);
    s.OnDisconnect(1000);
    CHECK(s.MaxFrameBytes() == pr::kMaxHandshakeBytes);
}

TEST_CASE("pair_ok without a verified challenge never marks the app paired") {
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(pr::GenerateCode()));
    Json answer;
    CHECK(hs.OnMessage(MsgPairOk(pr::RandomHex(16)), answer) == R::Ignore);
    CHECK_FALSE(hs.Paired());
    CHECK(hs.OnMessage(MsgPing(), answer) == R::Ignore);
    CHECK(hs.OnMessage(MsgPanel(Json::Object()), answer) == R::Ignore);
    CHECK_FALSE(hs.Paired());
}

TEST_CASE("codes normalize to one key; look-alikes and junk do not derive keys") {
    std::string code = pr::GenerateCode();
    unsigned char a[32], b[32], c[32];
    REQUIRE(pr::DeriveKey(code, a));
    std::string lower = code;
    for (char& ch : lower)
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    REQUIRE(pr::DeriveKey(lower, b));
    CHECK(std::memcmp(a, b, 32) == 0);
    std::string flipped = code;
    flipped[0] = flipped[0] == '0' ? '1' : '0';
    REQUIRE(pr::DeriveKey(flipped, c));
    CHECK(std::memcmp(a, c, 32) != 0);
    CHECK_FALSE(pr::DeriveKey("", a));
    CHECK_FALSE(pr::DeriveKey(code.substr(0, code.size() - 2), a));
    CHECK_FALSE(pr::DeriveKey(code + "A", a));
    CHECK_FALSE(pr::DeriveKey(std::string(code.size(), 'U'), a));
    CHECK_FALSE(pr::DeriveKey(std::string("ABCD\0EFGH", 9), a));
}

TEST_CASE("endpoint keys cannot escape the pairing directory") {
    const char* hostile[] = {"pipe://..\\..\\x", "pipe://../../etc/passwd", "pipe://a/b", "pipe://C:\\x", "pipe://a:b",
                             "pipe://..", "pipe://.", "shm://CON", "pipe://%2e%2e%2f", "pipe://\xe2\x80\xae", "tcp://h:../../1",
                             "tcp://h:", "tcp://h:99999999", "file:///etc/passwd", "pipe://", "", "pipe://name\nx"};
    std::string dir = test::UseTestPairingDir();
    for (const char* u : hostile) {
        std::string key = pr::EndpointKey(u);
        INFO(u);
        CHECK(key.find('/') == std::string::npos);
        CHECK(key.find('\\') == std::string::npos);
        CHECK(key.find(':') == std::string::npos);
        CHECK(key.size() <= 70);
        std::string path = pr::PairingFilePath(u);
        if (!key.empty()) {
            CHECK(path.compare(0, dir.size(), dir) == 0);
            CHECK(path.size() > dir.size() + 1);
            CHECK(path.compare(dir.size() + 1, 1, ".") != 0);
            CHECK(path.find(pr::file_detail::Sep(), dir.size() + 1) == std::string::npos);
        } else {
            CHECK(path.empty());
        }
    }
}

#ifndef _WIN32
TEST_CASE("pairing file reader refuses symlinks, fifos, foreign modes and oversize files") {
    std::string dir = test::UseTestPairingDir();
    std::string path = pr::PairingFilePath("tcp://127.0.0.1:65107");
    std::string err;
    REQUIRE(pr::WritePairingFile(path, pr::GenerateCode(), "tcp://127.0.0.1:65107", &err));
    pr::PairingFileData f;
    CHECK(pr::ReadPairingFile(path, f, &err));
    struct stat st;
    REQUIRE(::stat(path.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    REQUIRE(::stat(dir.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0700);

    std::string link = pr::PairingFilePath("tcp://127.0.0.1:65108");
    ::unlink(link.c_str());
    REQUIRE(::symlink(path.c_str(), link.c_str()) == 0);
    CHECK_FALSE(pr::ReadPairingFile(link, f, &err));

    std::string fifo = pr::PairingFilePath("tcp://127.0.0.1:65109");
    ::unlink(fifo.c_str());
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    CHECK_FALSE(pr::ReadPairingFile(fifo, f, &err));

    REQUIRE(::chmod(path.c_str(), 0644) == 0);
    CHECK_FALSE(pr::ReadPairingFile(path, f, &err));
    REQUIRE(::chmod(path.c_str(), 0600) == 0);
    CHECK(pr::ReadPairingFile(path, f, &err));

    ::unlink(link.c_str());
    int fd = ::open(link.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    REQUIRE(fd >= 0);
    std::string junk(pr::kPairingFileMaxBytes + 1, 'x');
    REQUIRE(::write(fd, junk.data(), junk.size()) == (ssize_t)junk.size());
    ::close(fd);
    CHECK_FALSE(pr::ReadPairingFile(link, f, &err));
    ::unlink(link.c_str());
    ::unlink(fifo.c_str());
    ::unlink(path.c_str());
}

TEST_CASE("a pairing directory with wide permissions is tightened; a foreign-looking path component is refused") {
    std::string dir = test::UseTestPairingDir();
    REQUIRE(::chmod(dir.c_str(), 0777) == 0);
    std::string err;
    std::string path = pr::PairingFilePath("tcp://127.0.0.1:65110");
    REQUIRE(pr::WritePairingFile(path, pr::GenerateCode(), "x", &err));
    struct stat st;
    REQUIRE(::stat(dir.c_str(), &st) == 0);
    CHECK((st.st_mode & 077) == 0);
    ::unlink(path.c_str());
}
#endif

TEST_CASE("the client drops a builder that sends an oversized frame before pairing and applies nothing") {
    test::HeadlessImGui ui;
    test::LiveTestBuilder b;
    REQUIRE(b.Listen());
    live::SetAppName("OversizeTest");
    REQUIRE(live::Connect(b.Url().c_str()));
    bool dropped = false;
    uint64_t deadline = detail::NowMs() + 6000;
    bool sent = false;
    while (detail::NowMs() < deadline && !dropped) {
        b.Pump();
        ui.BeginFrame();
        live::NewFrame();
        ui.EndFrame();
        b.Pump();
        if (b.t->IsConnected() && !sent && b.Count(msg::kHello) == 1) {
            Json big = Json::Object();
            big.Set("t", "log");
            big.Set("pad", std::string(pr::kMaxHandshakeBytes * 4, 'x'));
            b.SendPlain(big);
            sent = true;
        }
        if (sent && std::string(live::Status()).find("oversized") != std::string::npos) dropped = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(dropped);
    CHECK_FALSE(live::IsPaired());
    live::Disconnect();
}
