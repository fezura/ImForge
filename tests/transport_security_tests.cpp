#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "imforge/imforge_protocol.h"
#include "imforge/imforge_transport.h"

#ifdef _WIN32
#include <process.h>
#define IMFORGE_SEC_PID _getpid()
#else
#include <filesystem>
#include <unistd.h>
#define IMFORGE_SEC_PID getpid()
#endif

using namespace imforge;

namespace {

std::string UniqueName(const char* prefix) {
    static int counter = 0;
    return std::string(prefix) + std::to_string((long long)IMFORGE_SEC_PID) + "-" + std::to_string(++counter);
}

void Put32(std::vector<unsigned char>& v, uint32_t x) {
    unsigned char b[4];
    detail::PutU32LE(b, x);
    v.insert(v.end(), b, b + 4);
}

std::string Nested(int depth) {
    std::string s;
    for (int i = 0; i < depth; ++i) s += "[";
    for (int i = 0; i < depth; ++i) s += "]";
    return s;
}

}  // namespace

TEST_CASE("frame parser rejects oversize and overflowing lengths before buffering payload") {
    for (uint32_t len : {0xFFFFFFFFu, 0xFFFFFFFCu, 0x80000000u, (uint32_t)kTransportMaxFrame + 1u}) {
        detail::FrameParser p;
        std::vector<unsigned char> hdr;
        Put32(hdr, len);
        p.Append(hdr.data(), hdr.size());
        std::vector<unsigned char> out;
        CHECK_FALSE(p.Pop(out));
        CHECK(p.Corrupt());
        unsigned char more[64] = {};
        p.Append(more, sizeof(more));
        CHECK_FALSE(p.Pop(out));
    }
}

TEST_CASE("frame parser handles frames fed one byte at a time and rejects them once corrupt") {
    detail::FrameParser p;
    std::vector<unsigned char> frame = detail::MakeFrame("hello", 5);
    std::vector<unsigned char> out;
    for (size_t i = 0; i < frame.size(); ++i) {
        CHECK_FALSE(p.Pop(out));
        p.Append(&frame[i], 1);
    }
    REQUIRE(p.Pop(out));
    CHECK(std::string(out.begin(), out.end()) == "hello");
    CHECK_FALSE(p.Corrupt());
}

TEST_CASE("tcp listen defaults to loopback and refuses remote addresses unless opted in") {
    SetAllowRemoteListen(false);
    {
        TcpTransport t;
        CHECK_FALSE(t.Listen("tcp://0.0.0.0:0"));
    }
    {
        TcpTransport t;
        CHECK_FALSE(t.Listen("tcp://[::]:0"));
    }
    {
        TcpTransport t;
        REQUIRE(t.Listen("tcp://:0"));
        CHECK(t.BoundPort() != 0);
    }
    {
        TcpTransport t;
        REQUIRE(t.Listen("tcp://localhost:0"));
    }
    {
        TcpTransport t;
        REQUIRE(t.Listen("tcp://127.0.0.1:0"));
    }
    SetAllowRemoteListen(true);
    {
        TcpTransport t;
        CHECK(t.Listen("tcp://0.0.0.0:0"));
    }
    SetAllowRemoteListen(false);
    TcpTransport again;
    CHECK_FALSE(again.Listen("tcp://0.0.0.0:0"));
}

#ifndef _WIN32
TEST_CASE("tcp server drops a peer that announces an oversize frame") {
    SetAllowRemoteListen(false);
    TcpTransport server;
    REQUIRE(server.Listen("tcp://127.0.0.1:0"));
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(s >= 0);
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)server.BoundPort());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::connect(s, (sockaddr*)&addr, sizeof(addr)) == 0);
    unsigned char hdr[4] = {0xFF, 0xFF, 0xFF, 0x7F};
    REQUIRE(::send(s, hdr, 4, 0) == 4);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool closed_by_server = false;
    while (std::chrono::steady_clock::now() < deadline) {
        server.Poll();
        char byte;
        if (::recv(s, &byte, 1, MSG_DONTWAIT) == 0) {
            closed_by_server = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(closed_by_server);
    CHECK_FALSE(server.IsConnected());
    std::vector<unsigned char> msg;
    CHECK_FALSE(server.Receive(msg));
    ::close(s);
}

TEST_CASE("unix socket transport prefers XDG_RUNTIME_DIR, uses 0600 and refuses foreign files") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / UniqueName("imforge-xdg-");
    fs::create_directories(dir);
    setenv("XDG_RUNTIME_DIR", dir.c_str(), 1);
    std::string name = UniqueName("sec");
    CHECK(PipeTransport::SocketPath(name) == dir.string() + "/imforge-" + name + ".sock");
    {
        PipeTransport t;
        REQUIRE(t.Listen(("pipe://" + name).c_str()));
        struct stat st;
        REQUIRE(::lstat(PipeTransport::SocketPath(name).c_str(), &st) == 0);
        CHECK(S_ISSOCK(st.st_mode));
        CHECK((st.st_mode & 0777) == 0600);
    }
    {
        std::string path = PipeTransport::SocketPath(name);
        std::error_code ec;
        fs::path victim = dir / "victim.txt";
        { FILE* f = std::fopen(victim.c_str(), "w"); std::fputs("x", f); std::fclose(f); }
        fs::create_symlink(victim, path, ec);
        REQUIRE_FALSE(ec);
        PipeTransport t;
        CHECK_FALSE(t.Listen(("pipe://" + name).c_str()));
        CHECK(fs::exists(victim));
        PipeTransport c;
        REQUIRE(c.Connect(("pipe://" + name).c_str()));
        for (int i = 0; i < 20; ++i) {
            c.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK_FALSE(c.IsConnected());
    }
    unsetenv("XDG_RUNTIME_DIR");
    CHECK(PipeTransport::SocketPath("x") == "/tmp/imforge-x.sock");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

namespace {

struct RawShm {
    void* base = nullptr;
    size_t size = 0;
    uint32_t cap = 0;
    explicit RawShm(const std::string& name, uint32_t capacity) : cap(capacity) {
        size = 16 + 2 * (12 + (size_t)capacity);
        int fd = shm_open(("/ImForge." + name).c_str(), O_RDWR, 0);
        REQUIRE(fd >= 0);
        base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close(fd);
        REQUIRE(base != MAP_FAILED);
    }
    ~RawShm() { munmap(base, size); }
    uint32_t* U32(size_t off) { return (uint32_t*)((unsigned char*)base + off); }
    // ring 1 (to_builder) is the one a server reads
    size_t RingOff() const { return 16 + 12 + cap; }
    uint32_t* Head() { return U32(RingOff() + 4); }
    uint32_t* Tail() { return U32(RingOff() + 8); }
    unsigned char* Data() { return (unsigned char*)base + RingOff() + 12; }
};

}  // namespace

TEST_CASE("shm reader never trusts head, tail or length written by the peer") {
    const uint32_t kCap = 256;
    std::string name = UniqueName("secshm");
    ShmTransport server(kCap);
    REQUIRE(server.Listen(("shm://" + name).c_str()));
    RawShm raw(name, kCap);
    std::vector<unsigned char> out;

    SUBCASE("tail beyond capacity") {
        *raw.Head() = 0;
        *raw.Tail() = 0xFFFFFFF0u;
        CHECK_FALSE(server.Receive(out));
    }
    SUBCASE("head beyond capacity") {
        *raw.Head() = 0xFFFFFF00u;
        *raw.Tail() = 5;
        CHECK_FALSE(server.Receive(out));
    }
    SUBCASE("length larger than the ring") {
        *raw.Head() = 0;
        detail::PutU32LE(raw.Data(), 0xFFFFFFFEu);
        *raw.Tail() = 100;
        CHECK_FALSE(server.Receive(out));
    }
    SUBCASE("length that would read past the producer's tail") {
        *raw.Head() = 0;
        detail::PutU32LE(raw.Data(), kCap - 8);
        *raw.Tail() = 10;
        CHECK_FALSE(server.Receive(out));
        CHECK(*raw.Head() == 10);
    }
    SUBCASE("record crossing the end of the buffer") {
        *raw.Head() = kCap - 8;
        detail::PutU32LE(raw.Data() + kCap - 8, 100);
        *raw.Tail() = 4;
        CHECK_FALSE(server.Receive(out));
    }
    SUBCASE("skip marker pointing at an unwritten ring") {
        *raw.Head() = kCap - 4;
        detail::PutU32LE(raw.Data() + kCap - 4, kShmSkipMarker);
        *raw.Tail() = 2;
        CHECK_FALSE(server.Receive(out));
    }
    SUBCASE("well formed record is still delivered") {
        *raw.Head() = 0;
        detail::PutU32LE(raw.Data(), 3);
        std::memcpy(raw.Data() + 4, "abc", 3);
        *raw.Tail() = 7;
        REQUIRE(server.Receive(out));
        CHECK(std::string(out.begin(), out.end()) == "abc");
        CHECK_FALSE(server.Receive(out));
    }
}

TEST_CASE("shm client refuses a segment with an inconsistent header") {
    const uint32_t kCap = 256;
    std::string name = UniqueName("secshmhdr");
    ShmTransport server(kCap);
    REQUIRE(server.Listen(("shm://" + name).c_str()));
    RawShm raw(name, kCap);
    *raw.U32(16) = 0x40000000u;
    ShmTransport client;
    REQUIRE(client.Connect(("shm://" + name).c_str()));
    for (int i = 0; i < 5; ++i) {
        client.Poll();
        CHECK_FALSE(client.IsConnected());
    }
}
#endif

TEST_CASE("protocol: oversize payloads and deeply nested JSON are rejected") {
    std::vector<unsigned char> big(kProtocolMaxPayload + 1, ' ');
    Json out;
    CHECK_FALSE(Unframe(big, out));

    std::string deep = "{\"t\":\"ping\",\"x\":" + Nested(300) + "}";
    CHECK_FALSE(Unframe(deep.data(), deep.size(), out));
    std::string ok_depth = "{\"t\":\"ping\",\"x\":" + Nested(100) + "}";
    CHECK(Unframe(ok_depth.data(), ok_depth.size(), out));

    std::string wide = "{\"t\":\"ping\",\"x\":[";
    for (int i = 0; i < 600000; ++i) wide += "0,";
    wide += "0]}";
    CHECK_FALSE(Unframe(wide.data(), wide.size(), out));
}

TEST_CASE("protocol: peer supplied lists, names and log text are bounded and sanitized") {
    std::vector<std::string> many(10000, "w");
    Json hello = MsgHello(std::string(5000, 'a').c_str(), 1, 2, "b", "c", many, many);
    HelloMsg h;
    REQUIRE(ParseHello(hello, h));
    CHECK(h.app.size() == kProtocolMaxName);
    CHECK(h.widgets.size() == kProtocolMaxList);
    CHECK(h.textures.size() == kProtocolMaxList);

    std::vector<BlockHeight> blocks(kProtocolMaxBlocks + 1, BlockHeight{"id", "1"});
    BlocksMsg b;
    CHECK_FALSE(ParseBlocks(MsgBlocks("Main", blocks), b));
    blocks.pop_back();
    CHECK(ParseBlocks(MsgBlocks("Main", blocks), b));

    TargetMsg t;
    CHECK_FALSE(ParseSelect(MsgSelect("Main", std::string(1000, 'x').c_str()), t));
    CHECK(ParseSelect(MsgSelect("Main", "button_1"), t));

    LogMsg l;
    REQUIRE(ParseLog(MsgLog(2, std::string(100000, 'z').c_str()), l));
    CHECK(l.msg.size() == kProtocolMaxLogText);

    std::string dirty = SanitizeText("line1\nline2\x1b[31mred\x07\x7f\ttab");
    CHECK(dirty.find('\n') == std::string::npos);
    CHECK(dirty.find('\x1b') == std::string::npos);
    CHECK(dirty.find('\x07') == std::string::npos);
    CHECK(dirty == "line1 line2?[31mred?? tab");
    CHECK(SanitizeText("caf\xC3\xA9") == "caf\xC3\xA9");
    CHECK(SanitizeText(std::string(10, 'a'), 4) == "aaaa");

    std::vector<Json> comps(kProtocolMaxList + 1, Json::Object());
    std::vector<Json> back;
    CHECK_FALSE(ParseComponents(MsgComponents(comps), back));
}

TEST_CASE("protocol: font chunks must fit their file and the size caps") {
    const std::string hash(64, 'a');
    std::vector<unsigned char> bytes(1000, 7);
    FontDataMsg d;
    REQUIRE(ParseFontData(MsgFontData(hash, 5000, 1000, bytes.data(), bytes.size()), d));
    CHECK(d.bytes == bytes);
    CHECK(d.offset == 1000);
    CHECK(d.total == 5000);

    CHECK_FALSE(ParseFontData(MsgFontData(hash, 1500, 1000, bytes.data(), bytes.size()), d));
    CHECK_FALSE(ParseFontData(MsgFontData(hash, 1000, 1000, bytes.data(), bytes.size()), d));
    CHECK_FALSE(ParseFontData(MsgFontData(hash, kProtocolMaxFontBytes + 1, 0, bytes.data(), bytes.size()), d));
    CHECK_FALSE(ParseFontData(MsgFontData(std::string(64, 'A'), 5000, 0, bytes.data(), bytes.size()), d));
    CHECK_FALSE(ParseFontData(MsgFontData(hash.substr(1), 5000, 0, bytes.data(), bytes.size()), d));
    std::vector<unsigned char> big(kProtocolFontChunk + 3, 1);
    CHECK_FALSE(ParseFontData(MsgFontData(hash, kProtocolMaxFontBytes, 0, big.data(), big.size()), d));

    Json m = MsgFontData(hash, 5000, 0, bytes.data(), bytes.size());
    m.Set("data", "not base64!");
    CHECK_FALSE(ParseFontData(m, d));
    m.Set("data", "QUJD\nREVG");
    CHECK_FALSE(ParseFontData(m, d));
    m.Set("data", "QQ==QUJD");
    CHECK_FALSE(ParseFontData(m, d));
    m.Set("data", "QUJD");
    REQUIRE(ParseFontData(m, d));
    CHECK(std::string(d.bytes.begin(), d.bytes.end()) == "ABC");
    m.Set("offset", -1);
    CHECK_FALSE(ParseFontData(m, d));
    m.Set("offset", 1.5);
    CHECK_FALSE(ParseFontData(m, d));
}

TEST_CASE("protocol: font lists are bounded and every hash and range is checked") {
    FontSpecMsg f;
    f.name = "Body";
    f.size = 16;
    f.file = std::string(64, 'b');
    f.icons = std::string(64, 'c');
    f.icon_lo = 0xe005;
    f.icon_hi = 0xf8ff;
    std::vector<FontSpecMsg> back;
    REQUIRE(ParseFonts(MsgFonts({f}), back));
    REQUIRE(back.size() == 1);
    CHECK(back[0].icons == f.icons);
    CHECK(back[0].icon_hi == 0xf8ff);

    CHECK_FALSE(ParseFonts(MsgFonts(std::vector<FontSpecMsg>(kProtocolMaxFonts + 1, f)), back));
    FontSpecMsg bad = f;
    bad.file = "../../etc/passwd";
    CHECK_FALSE(ParseFonts(MsgFonts({bad}), back));
    bad = f;
    bad.size = 5000;
    CHECK_FALSE(ParseFonts(MsgFonts({bad}), back));
    bad = f;
    bad.icon_lo = 0xf8ff;
    bad.icon_hi = 0xe000;
    CHECK_FALSE(ParseFonts(MsgFonts({bad}), back));
    bad = f;
    bad.name.clear();
    CHECK_FALSE(ParseFonts(MsgFonts({bad}), back));

    std::vector<std::string> hashes(100, std::string(64, 'd'));
    hashes.push_back("zz");
    ClientInfoMsg ci;
    REQUIRE(ParseClientInfo(MsgClientInfo("app", 1, 19191, "b", "p", {}, {}, hashes), ci));
    CHECK(ci.font_hashes.size() == kProtocolMaxFontFiles);
    REQUIRE(ParseClientInfo(MsgClientInfo("app", 1, 19191, "b", "p", {}, {}, {"zz"}), ci));
    CHECK(ci.font_hashes.empty());
}

TEST_CASE("protocol: icon tables are bounded and names are plain") {
    std::vector<std::pair<std::string, unsigned>> icons = {{"fa-gear", 0xf013}, {"fa-heart", 0xf004}}, back;
    REQUIRE(ParseIcons(MsgIcons(icons), back));
    CHECK(back.size() == 2);
    CHECK_FALSE(ParseIcons(MsgIcons({{"fa-Gear", 0xf013}}), back));
    CHECK_FALSE(ParseIcons(MsgIcons({{"../x", 0xf013}}), back));
    CHECK_FALSE(ParseIcons(MsgIcons({{std::string(kProtocolMaxIconName + 1, 'a'), 0xf013}}), back));
    CHECK_FALSE(ParseIcons(MsgIcons({{"fa-x", 0xD800}}), back));
    CHECK_FALSE(ParseIcons(MsgIcons({{"fa-x", 0x110000}}), back));
    CHECK_FALSE(ParseIcons(MsgIcons({{"fa-x", 0x0a}}), back));
    std::vector<std::pair<std::string, unsigned>> many;
    for (size_t i = 0; i <= kProtocolMaxIcons; ++i) many.emplace_back("i" + std::to_string(i), 0xe000);
    CHECK_FALSE(ParseIcons(MsgIcons(many), back));
}

TEST_CASE("protocol: icon code points may not be control characters") {
    std::vector<std::pair<std::string, unsigned>> back;
    CHECK_FALSE(ParseIcons(MsgIcons({{"fa-x", 0x7f}}), back));
    CHECK_FALSE(ParseIcons(MsgIcons({{"fa-x", 0x85}}), back));
    CHECK_FALSE(ParseIcons(MsgIcons({{"fa-x", 0x9f}}), back));
    CHECK(ParseIcons(MsgIcons({{"fa-x", 0xa0}}), back));
    CHECK(ParseIcons(MsgIcons({{"fa-x", 0x7e}}), back));
}

TEST_CASE("protocol: font names lose control characters before they reach a log") {
    FontSpecMsg f;
    f.name = "Body\x1b[2J\nfake line";
    f.size = 16;
    f.file = std::string(64, 'b');
    std::vector<FontSpecMsg> back;
    REQUIRE(ParseFonts(MsgFonts({f}), back));
    REQUIRE(back.size() == 1);
    for (char c : back[0].name) CHECK((unsigned char)c >= 0x20);
    CHECK(back[0].name.find("fake line") != std::string::npos);
}
