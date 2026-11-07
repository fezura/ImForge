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
#include <set>
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

namespace {

std::string Sha(const std::string& s) { return pr::Sha256Hex(s); }

std::string Bytes(unsigned char v, size_t n) { return std::string(n, (char)v); }

std::string Hmac(const std::string& key, const std::string& data) {
    unsigned char out[32];
    pr::HmacSha256(key.data(), key.size(), data.data(), data.size(), out);
    return pr::ToHex(out, sizeof(out));
}

std::string Normalized(const std::string& code) {
    std::string n;
    REQUIRE(pr::NormalizeCode(code, n));
    return n;
}

std::string FileCode(const std::string& path) {
    pr::PairingFileData f;
    std::string err;
    if (!pr::ReadPairingFile(path, f, &err)) return "<" + err + ">";
    return f.code;
}

// empty nonce gives the legacy hello with app metadata
Json Hello(const char* app, const std::string& nonce) {
    if (!nonce.empty()) return MsgPairingHello(nonce);
    return MsgHello(app, 4242, IMGUI_VERSION_NUM, "test", "test", {}, {});
}

using V = pr::Server::Verdict;
using R = pr::ClientHandshake::Result;

// seal on the app side and feed it to the builder as if it came off the transport
V AppToBuilder(pr::ClientHandshake& hs, pr::Server& s, const Json& m, uint64_t now, Json* got = nullptr) {
    std::string sealed;
    REQUIRE(hs.Seal(Frame(m), sealed));
    Json msg, reply;
    V v = s.OnFrame(sealed.data(), sealed.size(), now, msg, reply);
    if (got) *got = msg;
    return v;
}

R Handshake(pr::Server& s, const std::string& code, uint64_t now, V* last_verdict = nullptr) {
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(code));
    s.OnConnect(now);
    Json reply, answer;
    V v = s.OnMessage(Hello("app", hs.NonceA()), now, reply);
    if (last_verdict) *last_verdict = v;
    if (v != V::Reply) return R::Ignore;
    R r = hs.OnMessage(reply, answer);
    if (r != R::Reply) {
        s.OnDisconnect(now);
        return r;
    }
    v = s.OnMessage(answer, now, reply);
    if (last_verdict) *last_verdict = v;
    return hs.OnMessage(reply, answer);
}

void StartServer(pr::Server& s, const char* endpoint, uint64_t now) {
    test::UseTestPairingDir();
    s.endpoint = endpoint;
    REQUIRE(s.Start(now));
    s.TakeLog();
}

}  // namespace

TEST_CASE("sha256 matches the FIPS 180-4 examples") {
    CHECK(Sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(Sha("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    pr::Sha256 h;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) h.Update(chunk);
    unsigned char d[32];
    h.Final(d);
    CHECK(pr::ToHex(d, 32) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    std::string msg;
    for (int i = 0; i < 300; ++i) msg.push_back((char)(i * 7 + 3));
    for (size_t split = 0; split <= msg.size(); split += 13) {
        pr::Sha256 a;
        a.Update(msg.data(), split);
        a.Update(msg.data() + split, msg.size() - split);
        a.Final(d);
        CHECK(pr::ToHex(d, 32) == Sha(msg));
    }
}

TEST_CASE("hmac-sha256 matches RFC 4231") {
    CHECK(Hmac(Bytes(0x0b, 20), "Hi There") == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    CHECK(Hmac("Jefe", "what do ya want for nothing?") == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    CHECK(Hmac(Bytes(0xaa, 20), Bytes(0xdd, 50)) == "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    std::string k4;
    for (int i = 1; i <= 25; ++i) k4.push_back((char)i);
    CHECK(Hmac(k4, Bytes(0xcd, 50)) == "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    CHECK(Hmac(Bytes(0x0c, 20), "Test With Truncation").substr(0, 32) == "a3b6167473100ee06e0c796c2955552b");
    CHECK(Hmac(Bytes(0xaa, 131), "Test Using Larger Than Block-Size Key - Hash Key First") ==
          "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    CHECK(Hmac(Bytes(0xaa, 131),
               "This is a test using a larger than block-size key and a larger than block-size data. The key needs to "
               "be hashed before being used by the HMAC algorithm.") ==
          "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}

TEST_CASE("constant-time compare, secure wipe, hex and random bytes") {
    CHECK(pr::ConstantTimeEqual(std::string("abcdef"), std::string("abcdef")));
    CHECK_FALSE(pr::ConstantTimeEqual(std::string("abcdef"), std::string("abcdeg")));
    CHECK_FALSE(pr::ConstantTimeEqual(std::string("abc"), std::string("abcd")));

    unsigned char buf[32];
    std::memset(buf, 0x5a, sizeof(buf));
    pr::SecureWipe(buf, sizeof(buf));
    for (unsigned char b : buf) CHECK(b == 0);
    std::string secret = "a secret that is long enough to live on the heap";
    pr::SecureWipe(secret);
    CHECK(secret.empty());

    unsigned char raw[3] = {0x00, 0xab, 0xff};
    CHECK(pr::ToHex(raw, 3) == "00abff");
    unsigned char back[3];
    CHECK(pr::FromHex("00ABff", back, 3));
    CHECK(std::memcmp(raw, back, 3) == 0);
    CHECK_FALSE(pr::FromHex("00abf", back, 3));
    CHECK_FALSE(pr::FromHex("00abfg", back, 3));

    unsigned char r1[32], r2[32];
    REQUIRE(pr::RandomBytes(r1, sizeof(r1)));
    REQUIRE(pr::RandomBytes(r2, sizeof(r2)));
    CHECK(std::memcmp(r1, r2, sizeof(r1)) != 0);
    std::string n1 = pr::RandomHex(16), n2 = pr::RandomHex(16);
    CHECK(pr::IsLowerHex(n1, 32));
    CHECK(n1 != n2);
}

TEST_CASE("pairing codes: format, alphabet, uniqueness and normalization") {
    std::set<std::string> seen;
    for (int i = 0; i < 300; ++i) {
        std::string c = pr::GenerateCode();
        REQUIRE(c.size() == pr::kCodeChars + pr::kCodeChars / pr::kCodeGroup - 1);
        for (size_t j = 0; j < c.size(); ++j) {
            if (j % 5 == 4) CHECK(c[j] == '-');
            else CHECK(std::strchr(pr::kCodeAlphabet, c[j]) != nullptr);
        }
        CHECK(seen.insert(c).second);
        CHECK(pr::FormatCode(Normalized(c)) == c);
    }

    std::string code = pr::FormatCode("0123456789ABCDEFGHJKMNPQRSTVWXYZ");
    std::string sloppy = " o123 4567-89abcdefghjkmnpqrstvwxyz ";
    CHECK(Normalized(sloppy) == "0123456789ABCDEFGHJKMNPQRSTVWXYZ");
    CHECK(Normalized("O1i1L1" + std::string(26, '2')) == "011111" + std::string(26, '2'));
    std::string n;
    CHECK_FALSE(pr::NormalizeCode("U" + std::string(31, 'A'), n));
    CHECK_FALSE(pr::NormalizeCode(std::string(31, 'A'), n));
    CHECK_FALSE(pr::NormalizeCode(std::string(33, 'A'), n));
    CHECK_FALSE(pr::NormalizeCode("", n));
    CHECK_FALSE(pr::NormalizeCode((const char*)nullptr, n));
    CHECK_FALSE(pr::NormalizeCode("\xc3\xa9" + std::string(31, 'A'), n));
    CHECK_FALSE(pr::NormalizeCode(std::string(400, 'A'), n));
    CHECK(n.empty());

    unsigned char k1[32], k2[32], k3[32];
    REQUIRE(pr::DeriveKey(code, k1));
    REQUIRE(pr::DeriveKey(sloppy, k2));
    REQUIRE(pr::DeriveKey(pr::GenerateCode(), k3));
    CHECK(std::memcmp(k1, k2, 32) == 0);
    CHECK(std::memcmp(k1, k3, 32) != 0);
    CHECK_FALSE(pr::DeriveKey("ABCD-EFGH", k3));
    CHECK(pr::HandshakeMac(k1, pr::kBuilderLabel, "a", "b", "c") != pr::HandshakeMac(k1, pr::kAppLabel, "a", "b", "c"));
}

TEST_CASE("endpoint keys and pairing file locations") {
    CHECK(pr::EndpointKey("tcp://127.0.0.1:7777") == "tcp-7777");
    CHECK(pr::EndpointKey("tcp://0.0.0.0:7777") == "tcp-7777");
    CHECK(pr::EndpointKey("tcp://[::1]:9000") == "tcp-9000");
    CHECK(pr::EndpointKey("tcp://localhost") == "");
    CHECK(pr::EndpointKey("tcp://h:77x") == "");
    CHECK(pr::EndpointKey("pipe://ImForge") == "pipe-ImForge");
    CHECK(pr::EndpointKey("pipe://../../evil") == "pipe-.._.._evil");
    CHECK(pr::EndpointKey("shm://a/b\\c:d") == "shm-a_b_c_d");
    CHECK(pr::EndpointKey("udp://x") == "");
    CHECK(pr::EndpointKey("ImForge") == "");
    CHECK(pr::EndpointKey(nullptr) == "");
    CHECK(pr::EndpointKey(("pipe://" + std::string(500, 'x')).c_str()).size() == 5 + 64);

    pr::SetPairingDir(nullptr);
    std::string def = pr::PairingFilePath("tcp://127.0.0.1:7777");
    CHECK_FALSE(def.empty());
#ifdef _WIN32
    CHECK(def.find("\\ImForge\\live\\tcp-7777.pair") != std::string::npos);
#else
    CHECK(def.find("/imforge/tcp-7777.pair") != std::string::npos);
#endif
    const std::string& dir = test::UseTestPairingDir();
    std::string p = pr::PairingFilePath("pipe://ImForge");
    CHECK(p.compare(0, dir.size(), dir) == 0);
    CHECK(p.size() > 17);
    CHECK(p.compare(p.size() - 17, 17, "pipe-ImForge.pair") == 0);
    CHECK(pr::PairingFilePath("bogus").empty());
}

TEST_CASE("pairing file round trip, atomic replace, ownership and permissions") {
    test::UseTestPairingDir();
    std::string path = pr::PairingFilePath("tcp://127.0.0.1:65001");
    pr::DeletePairingFile(path);
    std::string err;
    pr::PairingFileData f;
    CHECK_FALSE(pr::ReadPairingFile(path, f, &err));
    CHECK(err.find("no pairing file") != std::string::npos);

    std::string c1 = pr::GenerateCode(), c2 = pr::GenerateCode();
    REQUIRE(pr::WritePairingFile(path, c1, "tcp://127.0.0.1:65001", &err));
    REQUIRE(pr::ReadPairingFile(path, f, &err));
    CHECK(f.code == c1);
    CHECK(f.version == 1);
    CHECK(f.endpoint == "tcp://127.0.0.1:65001");
    CHECK(f.pid == pr::file_detail::CurrentPid());
    CHECK(f.issued > 1600000000);
    REQUIRE(pr::WritePairingFile(path, c2, "tcp://127.0.0.1:65001", &err));
    CHECK(FileCode(path) == c2);
    CHECK_FALSE(pr::PairingFileExists(path + "." + std::to_string(pr::file_detail::CurrentPid()) + ".tmp"));

#ifndef _WIN32
    struct stat st;
    REQUIRE(::stat(path.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    REQUIRE(::stat(test::UseTestPairingDir().c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0700);

    REQUIRE(::chmod(path.c_str(), 0644) == 0);
    CHECK_FALSE(pr::ReadPairingFile(path, f, &err));
    CHECK(err.find("accessible by other users") != std::string::npos);
    REQUIRE(::chmod(path.c_str(), 0600) == 0);
    CHECK(pr::ReadPairingFile(path, f, &err));

    std::string link = pr::PairingFilePath("tcp://127.0.0.1:65002");
    ::unlink(link.c_str());
    REQUIRE(::symlink(path.c_str(), link.c_str()) == 0);
    CHECK_FALSE(pr::ReadPairingFile(link, f, &err));
    ::unlink(link.c_str());

    std::string loose = test::UseTestPairingDir() + "_loose";
    ::mkdir(loose.c_str(), 0777);
    ::chmod(loose.c_str(), 0777);
    pr::SetPairingDir(loose.c_str());
    std::string lp = pr::PairingFilePath("tcp://127.0.0.1:65003");
    REQUIRE(pr::WritePairingFile(lp, c1, "x", &err));
    REQUIRE(::stat(loose.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0700);
    pr::DeletePairingFile(lp);
    pr::SetPairingDir(test::UseTestPairingDir().c_str());
#endif

    std::string junk_path = pr::PairingFilePath("tcp://127.0.0.1:65004");
    REQUIRE(pr::WritePairingFile(junk_path, c1, "x", &err));
    {
#ifdef _WIN32
        FILE* fp = _wfopen(pr::file_detail::Widen(junk_path).c_str(), L"wb");
#else
        FILE* fp = std::fopen(junk_path.c_str(), "wb");
#endif
        REQUIRE(fp);
        std::string big(5000, ' ');
        std::fwrite(big.data(), 1, big.size(), fp);
        std::fclose(fp);
    }
    CHECK_FALSE(pr::ReadPairingFile(junk_path, f, &err));
    pr::DeletePairingFile(junk_path);

    CHECK(pr::DeletePairingFile(path));
    CHECK_FALSE(pr::PairingFileExists(path));
}

TEST_CASE("handshake happy path in-process; the code rotates as soon as it is used") {
    pr::Server s;
    uint64_t now = 1000;
    StartServer(s, "tcp://127.0.0.1:65010", now);
    std::string path = s.FilePath();
    REQUIRE(s.FileWritten());
    std::string code = FileCode(path);
    CHECK(code == s.Code());
    int gen = s.Generation();

    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(code));
    s.OnConnect(now);
    CHECK(s.GetState() == pr::Server::State::AwaitHello);
    Json reply, answer;
    REQUIRE(s.OnMessage(Hello("MyGame", hs.NonceA()), now, reply) == V::Reply);
    CHECK(IsMsg(reply, msg::kPairChallenge));
    CHECK(s.GetState() == pr::Server::State::Challenged);
    REQUIRE(hs.OnMessage(reply, answer) == R::Reply);
    CHECK(IsMsg(answer, msg::kPairResponse));
    REQUIRE(s.OnMessage(answer, now, reply) == V::Paired);
    CHECK(IsMsg(reply, msg::kPairOk));
    REQUIRE(hs.OnMessage(reply, answer) == R::Paired);
    CHECK(hs.Paired());
    CHECK(s.Established());
    CHECK(s.PeerApp().empty());
    CHECK_FALSE(s.ClientInfoReceived());
    CHECK(s.SessionChannel().Active());
    CHECK(hs.SessionChannel().Active());
    REQUIRE(AppToBuilder(hs, s, MsgClientInfo("MyGame", 4242, IMGUI_VERSION_NUM, "test", "test", {}, {}), now) == V::Accept);
    CHECK(s.ClientInfoReceived());
    CHECK(s.PeerApp() == "MyGame");
    CHECK(s.PeerPid() == 4242);
    CHECK(hs.SessionId() == s.SessionId());
    CHECK(s.Generation() == gen + 1);
    CHECK(s.Code() != code);
    CHECK(FileCode(path) == s.Code());
    CHECK(s.Pairings() == 1);

    s.OnDisconnect(now);
    CHECK(s.SessionId().empty());
    CHECK(s.PeerApp().empty());
    CHECK(s.GetState() == pr::Server::State::Idle);
    CHECK_FALSE(s.SessionChannel().Active());
    CHECK(s.SessionChannel().KeysWiped());
    hs.Wipe();
    CHECK(hs.SessionChannel().KeysWiped());
    s.Stop();
    CHECK(s.Code().empty());
    CHECK_FALSE(pr::PairingFileExists(path));
}

TEST_CASE("wrong code: pair_fail with attempts left, burned after 5, connections refused for 3 s") {
    pr::Server s;
    uint64_t now = 5000;
    StartServer(s, "tcp://127.0.0.1:65011", now);
    std::string first = s.Code();
    int gen = s.Generation();

    {
        INFO("an app holding a wrong code detects that the builder cannot prove it; the abandoned handshake is not counted");
        CHECK(Handshake(s, pr::GenerateCode(), now) == R::Failed);
        CHECK(s.Failures() == 0);
        CHECK(s.AbandonedHandshakes() == 1);
        CHECK(s.Code() == first);
    }
    for (int i = 1; i <= 5; ++i) {
        s.OnConnect(now);
        Json reply;
        REQUIRE(s.OnMessage(Hello("attacker", pr::RandomHex(16)), now, reply) == V::Reply);
        PairChallengeMsg ch;
        REQUIRE(ParsePairChallenge(reply, ch));
        V v = s.OnMessage(MsgPairResponse(ch.session_id, pr::RandomHex(32)), now, reply);
        CHECK(v == V::ReplyAndDrop);
        PairFailMsg f;
        REQUIRE(ParsePairFail(reply, f));
        CHECK(f.attempts_left == 5 - i);
        Json more;
        CHECK(s.OnMessage(Hello("attacker", pr::RandomHex(16)), now, more) == V::Ignore);
        s.OnDisconnect(now);
    }
    CHECK(s.TotalFailures() == 5);
    CHECK(s.Failures() == 0);
    CHECK(s.Code() != first);
    CHECK(s.Generation() == gen + 1);
    CHECK(FileCode(s.FilePath()) == s.Code());
    CHECK(s.Refusing(now));
    CHECK(s.Refusing(now + 2999));
    CHECK_FALSE(s.Refusing(now + 3000));
    CHECK(Handshake(s, s.Code(), now + 3000) == R::Paired);
}

TEST_CASE("an old code is refused once it was used (single use)") {
    pr::Server s;
    uint64_t now = 9000;
    StartServer(s, "tcp://127.0.0.1:65012", now);
    std::string used = s.Code();
    REQUIRE(Handshake(s, used, now) == R::Paired);
    s.OnDisconnect(now);

    CHECK(Handshake(s, used, now) == R::Failed);
    CHECK(s.Failures() == 0);

    s.OnConnect(now);
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(used));
    Json reply;
    REQUIRE(s.OnMessage(Hello("replay", hs.NonceA()), now, reply) == V::Reply);
    PairChallengeMsg ch;
    REQUIRE(ParsePairChallenge(reply, ch));
    unsigned char k[32];
    REQUIRE(pr::DeriveKey(used, k));
    std::string forged = pr::HandshakeMac(k, pr::kAppLabel, hs.NonceA(), ch.nonce_b, ch.session_id);
    CHECK(s.OnMessage(MsgPairResponse(ch.session_id, forged), now, reply) == V::ReplyAndDrop);
    CHECK(IsMsg(reply, msg::kPairFail));
    CHECK_FALSE(s.Established());
}

TEST_CASE("messages before pairing are ignored on both sides") {
    pr::Server s;
    uint64_t now = 20000;
    StartServer(s, "tcp://127.0.0.1:65013", now);
    s.OnConnect(now);
    Json reply;
    const Json early[] = {MsgSelect("MainMenu", "btn"), MsgLog(2, "x"), MsgStats(60, {"MainMenu"}), MsgPong(),
                          MsgBlocks("MainMenu", {}), MsgPairResponse(pr::RandomHex(16), pr::RandomHex(32))};
    for (const Json& m : early) CHECK(s.OnMessage(m, now, reply) == V::Ignore);
    CHECK(s.IgnoredMessages() == 6);
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(s.Code()));
    REQUIRE(s.OnMessage(Hello("app", hs.NonceA()), now, reply) == V::Reply);
    Json challenge = reply;
    CHECK(s.OnMessage(MsgHover("MainMenu", "btn"), now, reply) == V::Ignore);
    CHECK(s.OnMessage(Hello("app", hs.NonceA()), now, reply) == V::Ignore);

    Json answer;
    const Json pushes[] = {MsgPanel(Json::Object()), MsgProject(Json::Object()), MsgMode(true, "", ""), MsgPing(),
                           MsgTheme(Json::Object()), MsgRemovePanel("MainMenu"), MsgPairOk(pr::RandomHex(16))};
    for (const Json& m : pushes) CHECK(hs.OnMessage(m, answer) == R::Ignore);
    REQUIRE(hs.OnMessage(challenge, answer) == R::Reply);
    Json none;
    CHECK(hs.OnMessage(MsgPanel(Json::Object()), none) == R::Ignore);
    REQUIRE(s.OnMessage(answer, now, reply) == V::Paired);
    REQUIRE(hs.OnMessage(reply, none) == R::Paired);
    CHECK(AppToBuilder(hs, s, MsgSelect("MainMenu", "btn"), now) == V::Accept);
    CHECK(AppToBuilder(hs, s, Hello("again", pr::RandomHex(16)), now) == V::Ignore);
    CHECK(AppToBuilder(hs, s, MsgPairResponse(pr::RandomHex(16), pr::RandomHex(32)), now) == V::Ignore);
    CHECK(s.Established());
    INFO("once paired, a plain (unsealed) message is a security event that ends the session");
    CHECK(s.OnMessage(MsgSelect("MainMenu", "btn"), now, reply) == V::Drop);
    CHECK(s.SecurityEvents() == 1);
    CHECK_FALSE(s.Established());
    CHECK(s.SessionChannel().KeysWiped());
}

TEST_CASE("legacy hello: refused when pairing is required, accepted only when disabled") {
    pr::Server s;
    uint64_t now = 30000;
    StartServer(s, "tcp://127.0.0.1:65014", now);
    s.OnConnect(now);
    Json reply;
    CHECK(s.OnMessage(Hello("old", ""), now, reply) == V::ReplyAndDrop);
    PairFailMsg f;
    REQUIRE(ParsePairFail(reply, f));
    CHECK(f.reason.find("pairing required") != std::string::npos);
    CHECK(s.Failures() == 0);
    s.OnDisconnect(now);

    s.OnConnect(now);
    Json bad = MsgPairingHello("XYZ");
    CHECK(s.OnMessage(bad, now, reply) == V::ReplyAndDrop);
    s.OnDisconnect(now);

    s.OnConnect(now);
    Json v1 = MsgPairingHello(pr::RandomHex(16));
    v1.Set("v", 1);
    CHECK(s.OnMessage(v1, now, reply) == V::ReplyAndDrop);
    REQUIRE(ParsePairFail(reply, f));
    CHECK(f.reason.find("version mismatch") != std::string::npos);
    CHECK(s.Failures() == 0);
    s.OnDisconnect(now);

    s.require = false;
    s.OnConnect(now);
    CHECK(s.OnMessage(Hello("old", ""), now, reply) == V::Accept);
    CHECK(s.GetState() == pr::Server::State::Unauthenticated);
    CHECK(s.Established());
    CHECK(s.OnMessage(MsgStats(1, {}), now, reply) == V::Accept);
    std::string plain;
    CHECK(s.Encode(MsgPing(), plain));
    CHECK(plain == Frame(MsgPing()));
    s.OnDisconnect(now);
    CHECK(Handshake(s, s.Code(), now) == R::Paired);
}

TEST_CASE("timeouts and the 10 minute expiry") {
    pr::Server s;
    uint64_t now = 40000;
    StartServer(s, "tcp://127.0.0.1:65015", now);
    s.OnConnect(now);
    CHECK_FALSE(s.Tick(now + 4999));
    CHECK(s.Tick(now + 5000));
    s.OnDisconnect(now + 5000);
    CHECK(s.Failures() == 0);

    now += 6000;
    s.OnConnect(now);
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(s.Code()));
    Json reply;
    REQUIRE(s.OnMessage(Hello("slow", hs.NonceA()), now, reply) == V::Reply);
    CHECK_FALSE(s.Tick(now + 4999));
    CHECK(s.Tick(now + 5000));
    CHECK(s.Failures() == 0);
    CHECK(s.AbandonedHandshakes() == 1);
    s.OnDisconnect(now + 5000);

    int gen = s.Generation();
    std::string code = s.Code();
    uint64_t issued = s.IssuedAt();
    CHECK_FALSE(s.Tick(issued + pr::kCodeLifetimeMs - 1));
    CHECK(s.Generation() == gen);
    s.Tick(issued + pr::kCodeLifetimeMs);
    CHECK(s.Generation() == gen + 1);
    CHECK(s.Code() != code);
    CHECK(FileCode(s.FilePath()) == s.Code());
}

TEST_CASE("the app rejects a builder that cannot prove the code") {
    std::string code = pr::GenerateCode();
    pr::ClientHandshake hs;
    REQUIRE(hs.Begin(code));
    Json answer;
    CHECK(hs.OnMessage(MsgPairChallenge(pr::RandomHex(16), pr::RandomHex(16), pr::RandomHex(32)), answer) == R::Failed);
    CHECK(hs.Error().find("could not prove") != std::string::npos);
    CHECK(answer.IsNull());
    CHECK_FALSE(hs.Active());

    REQUIRE(hs.Begin(code));
    unsigned char k[32];
    REQUIRE(pr::DeriveKey(code, k));
    std::string nb = pr::RandomHex(16), sid = pr::RandomHex(16);
    std::string mac_b = pr::HandshakeMac(k, pr::kBuilderLabel, hs.NonceA(), nb, sid);
    std::string wrong_label = pr::HandshakeMac(k, pr::kAppLabel, hs.NonceA(), nb, sid);
    CHECK(hs.OnMessage(MsgPairChallenge(nb, sid, wrong_label), answer) == R::Failed);

    REQUIRE(hs.Begin(code));
    mac_b = pr::HandshakeMac(k, pr::kBuilderLabel, hs.NonceA(), nb, sid);
    REQUIRE(hs.OnMessage(MsgPairChallenge(nb, sid, mac_b), answer) == R::Reply);
    CHECK(hs.OnMessage(MsgPairOk(pr::RandomHex(16)), answer) == R::Failed);

    REQUIRE(hs.Begin(code));
    mac_b = pr::HandshakeMac(k, pr::kBuilderLabel, hs.NonceA(), nb, sid);
    REQUIRE(hs.OnMessage(MsgPairChallenge(nb, sid, mac_b), answer) == R::Reply);
    CHECK(hs.OnMessage(MsgPairFail(3, "wrong pairing code"), answer) == R::Rejected);
    CHECK(hs.AttemptsLeft() == 3);
    CHECK_FALSE(hs.Begin("not a code"));
}

// end to end over TCP with the real client
namespace {

const char* kPanel = R"({"schemaVersion":1,"kind":"panel","name":"Pairing",
  "root":{"id":"window","type":"Window","props":{"title":"Pairing"},
          "children":[{"id":"txt","type":"Text","props":{"text":"paired"}}]}})";

Json PanelJson() {
    Json j;
    REQUIRE(ParseJson(kPanel, std::strlen(kPanel), j));
    return j;
}

struct AppSide {
    test::HeadlessImGui ui;
    void Frame() {
        ui.BeginFrame();
        live::NewFrame();
        live::Draw("Pairing", {});
        ui.EndFrame();
    }
};

template <class Pred> bool PumpUntil(test::LiveTestBuilder& b, AppSide& app, Pred pred, uint64_t timeout_ms = 6000) {
    uint64_t deadline = detail::NowMs() + timeout_ms;
    for (;;) {
        b.Pump();
        app.Frame();
        b.Pump();
        if (pred()) return true;
        if (detail::NowMs() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void PumpFor(test::LiveTestBuilder& b, AppSide& app, uint64_t ms) {
    PumpUntil(b, app, [] { return false; }, ms);
}

}  // namespace

TEST_CASE("client pairs automatically from the pairing file; builder pushes before pairing are ignored") {
    AppSide app;
    test::LiveTestBuilder b;
    REQUIRE(b.Listen());
    std::string first_code = FileCode(b.pair.FilePath());
    REQUIRE(pr::IsValidCode(first_code));
    live::SetAppName("PairingTest");
    REQUIRE(live::Connect(b.Url().c_str()));
    CHECK(std::string(live::Status()).compare(0, 10, "connecting") == 0);

    REQUIRE(PumpUntil(b, app, [&] { return b.t->IsConnected(); }));
    CHECK_FALSE(b.Send(MsgPanel(PanelJson())));
    b.SendPlain(MsgPanel(PanelJson()));
    b.SendPlain(MsgMode(true, "", ""));
    REQUIRE_MESSAGE(PumpUntil(b, app, [&] { return live::IsPaired() && b.pair.Established(); }), live::Status());
    CHECK(live::IsConnected());
    CHECK(live::DebugPanelCount() == 0);
    CHECK(live::client_detail::Get().ignored_unpaired >= 2);
    CHECK(std::string(live::Status()).find("(paired)") != std::string::npos);
    REQUIRE(PumpUntil(b, app, [&] { return b.Count(msg::kClientInfo) == 1; }));
    CHECK(b.pair.PeerApp() == "PairingTest");
    CHECK(b.pair.Pairings() == 1);
    CHECK(FileCode(b.pair.FilePath()) != first_code);
    CHECK(b.Count(msg::kHello) == 1);
    CHECK(b.Count(msg::kPairResponse) == 1);

    b.Send(MsgPanel(PanelJson()));
    REQUIRE(PumpUntil(b, app, [&] { return live::DebugPanelCount() == 1; }));
    REQUIRE(PumpUntil(b, app, [&] { return b.Count(msg::kStats) >= 1; }, 4000));

    {
        INFO("a second concurrent client is turned away and does not disturb the session");
        std::unique_ptr<Transport> intruder = CreateTransport(b.Url().c_str());
        REQUIRE(intruder->Connect(b.Url().c_str()));
        pr::ClientHandshake hs;
        REQUIRE(hs.Begin(first_code));
        uint64_t until = detail::NowMs() + 600;
        while (detail::NowMs() < until) {
            intruder->Poll();
            if (intruder->IsConnected()) {
                std::string s = Frame(MsgPairingHello(hs.NonceA()));
                intruder->Send(s.data(), s.size());
            }
            b.Pump();
            app.Frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::vector<unsigned char> got;
        CHECK_FALSE(intruder->Receive(got));
        intruder->Close();
        CHECK(b.pair.Established());
        CHECK(b.pair.PeerApp() == "PairingTest");
        CHECK(b.Count(msg::kHello) == 1);
        CHECK(live::IsPaired());
    }

    live::Disconnect();
    CHECK_FALSE(live::IsPaired());
    CHECK(std::string(live::Status()) == "disconnected");
}

TEST_CASE("after the link ends the client re-reads the rotated code; a stale code fails then recovers") {
    AppSide app;
    test::LiveTestBuilder b;
    REQUIRE(b.Listen());
    std::string first_code = FileCode(b.pair.FilePath());
    REQUIRE(live::Connect(b.Url().c_str()));
    REQUIRE_MESSAGE(PumpUntil(b, app, [&] { return live::IsPaired(); }), live::Status());
    std::string second_code = FileCode(b.pair.FilePath());
    CHECK(second_code != first_code);

    b.Drop();
    REQUIRE(PumpUntil(b, app, [&] { return !live::IsPaired(); }, 3000));
    CHECK(b.pair.SessionId().empty());
    REQUIRE_MESSAGE(PumpUntil(b, app, [&] { return live::IsPaired() && b.pair.Pairings() == 2; }), live::Status());
    CHECK(FileCode(b.pair.FilePath()) != second_code);
    CHECK(b.pair.TotalFailures() == 0);

    {
        INFO("an explicit code that was already used is never sent: the client goes on with the pairing file");
        live::Disconnect();
        PumpFor(b, app, 200);
        CHECK(live::SetPairingCode(second_code.c_str()));
        REQUIRE(live::Connect(b.Url().c_str()));
        REQUIRE_MESSAGE(PumpUntil(b, app, [&] { return live::IsPaired() && b.pair.Pairings() == 3; }), live::Status());
        CHECK(b.pair.TotalFailures() == 0);
    }
    {
        INFO("a stale explicit code (rotated away) fails once, is burned, and the pairing file takes over");
        live::Disconnect();
        PumpFor(b, app, 200);
        std::string stale = FileCode(b.pair.FilePath());
        REQUIRE(b.pair.Pairings() == 3);
        b.pair.Rotate(detail::NowMs(), "test");
        CHECK(live::SetPairingCode(stale.c_str()));
        REQUIRE(live::Connect(b.Url().c_str()));
        REQUIRE_MESSAGE(PumpUntil(b, app, [&] { return live::IsPaired() && b.pair.Pairings() == 4; }, 8000), live::Status());
        CHECK(b.pair.TotalFailures() == 0);
        CHECK(b.pair.AbandonedHandshakes() >= 1);
        CHECK(live::client_detail::Get().explicit_code.empty());
    }
    live::Disconnect();
}

TEST_CASE("client waits quietly while there is no pairing file, then pairs once the builder publishes one") {
    AppSide app;
    test::LiveTestBuilder b;
    REQUIRE(b.Listen(false));
    REQUIRE(live::Connect(b.Url().c_str()));
    PumpFor(b, app, 1500);
    CHECK(b.connects == 0);
    CHECK_FALSE(live::IsPaired());
    CHECK(std::string(live::Status()).find("waiting for ImForge builder") == 0);
    CHECK(std::string(live::Status()).find("no pairing file") != std::string::npos);

    REQUIRE(b.pair.Start(detail::NowMs()));
    REQUIRE_MESSAGE(PumpUntil(b, app, [&] { return live::IsPaired(); }), live::Status());
    std::string path = b.pair.FilePath();
    CHECK(pr::PairingFileExists(path));

    b.pair.Stop();
    CHECK_FALSE(pr::PairingFileExists(path));
    live::Disconnect();
}

TEST_CASE("explicit SetPairingCode works without any pairing file (VM/remote fallback)") {
    AppSide app;
    test::LiveTestBuilder b;
    b.pair.write_file = false;
    b.pair.fixed_code = pr::GenerateCode();
    REQUIRE(b.Listen());
    CHECK_FALSE(pr::PairingFileExists(pr::PairingFilePath(b.Url().c_str())));
    CHECK_FALSE(live::SetPairingCode("not-a-code"));
    REQUIRE(live::Connect(b.Url().c_str()));
    PumpFor(b, app, 300);
    CHECK(b.connects == 0);
    std::string lower = b.pair.fixed_code;
    for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);
    CHECK(live::SetPairingCode(lower.c_str()));
    REQUIRE_MESSAGE(PumpUntil(b, app, [&] { return live::IsPaired(); }), live::Status());
    CHECK(live::client_detail::Get().explicit_code.empty());
    live::Disconnect();
}

TEST_CASE("a client holding a wrong code never answers the challenge: nothing is counted and it pairs once fixed") {
    AppSide app;
    test::LiveTestBuilder b;
    b.pair.write_file = false;
    REQUIRE(b.Listen());
    std::string wrong = pr::GenerateCode();
    CHECK(live::SetPairingCode(wrong.c_str()));
    REQUIRE(live::Connect(b.Url().c_str()));
    REQUIRE(PumpUntil(b, app, [&] { return b.pair.AbandonedHandshakes() >= 1; }));
    PumpFor(b, app, 2500);
    CHECK(b.pair.TotalFailures() == 0);
    CHECK(b.pair.AbandonedHandshakes() >= 2);
    CHECK(b.Count(msg::kPairResponse) == 0);
    CHECK_FALSE(live::IsPaired());
    CHECK(live::client_detail::Get().pair_error.find("could not prove") != std::string::npos);
    CHECK(live::SetPairingCode(b.pair.Code().c_str()));
    REQUIRE_MESSAGE(PumpUntil(b, app, [&] { return live::IsPaired(); }, 8000), live::Status());
    CHECK(b.pair.TotalFailures() == 0);
    live::Disconnect();
}
