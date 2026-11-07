#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "imforge/imforge_transport.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define IMFORGE_TEST_PID _getpid()
#else
#include <unistd.h>
#define IMFORGE_TEST_PID getpid()
#endif

using namespace imforge;

namespace {

uint64_t NowMs() { return detail::NowMs(); }

std::string UniqueName(const char* prefix) {
    static int counter = 0;
    return std::string(prefix) + std::to_string((long long)IMFORGE_TEST_PID) + "-" + std::to_string(++counter);
}

std::vector<unsigned char> MakeMessage(int index, size_t size, unsigned char salt) {
    std::vector<unsigned char> m(size);
    uint32_t x = (uint32_t)index * 2654435761u + salt;
    for (size_t i = 0; i < size; ++i) {
        x = x * 1664525u + 1013904223u;
        m[i] = (unsigned char)(x >> 24);
    }
    return m;
}

size_t VariedSize(int index, size_t max_size) {
    size_t s = ((size_t)index * 7919u) % max_size;
    if (index % 10 == 0) s = 0;
    if (index % 97 == 1) s = max_size - 1;
    return s;
}

bool WaitConnected(Transport& a, Transport& b, uint64_t timeout_ms = 3000) {
    uint64_t deadline = NowMs() + timeout_ms;
    while (NowMs() < deadline) {
        a.Poll();
        b.Poll();
        if (a.IsConnected() && b.IsConnected()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool WaitDisconnected(Transport& t, uint64_t timeout_ms = 3000) {
    uint64_t deadline = NowMs() + timeout_ms;
    while (NowMs() < deadline) {
        t.Poll();
        if (!t.IsConnected()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

struct Side {
    Transport* t;
    unsigned char salt;
    int sent = 0;
    int received = 0;
    bool ok = true;
};

bool Exchange(Transport& a, Transport& b, int count, size_t max_size, uint64_t timeout_ms = 5000) {
    Side sides[2] = {{&a, 11}, {&b, 29}};
    uint64_t deadline = NowMs() + timeout_ms;
    std::vector<unsigned char> msg;
    while (NowMs() < deadline) {
        bool progress = false;
        for (int s = 0; s < 2; ++s) {
            Side& me = sides[s];
            for (int burst = 0; burst < 8 && me.sent < count; ++burst) {
                std::vector<unsigned char> m = MakeMessage(me.sent, VariedSize(me.sent, max_size), me.salt);
                if (!me.t->Send(m.data(), m.size())) break;
                ++me.sent;
                progress = true;
            }
        }
        a.Poll();
        b.Poll();
        for (int s = 0; s < 2; ++s) {
            Side& me = sides[s];
            Side& peer = sides[1 - s];
            while (me.t->Receive(msg)) {
                progress = true;
                std::vector<unsigned char> expect =
                    MakeMessage(me.received, VariedSize(me.received, max_size), peer.salt);
                if (msg != expect) {
                    me.ok = false;
                    MESSAGE("mismatch at message " << me.received << " size " << msg.size() << " expected "
                                                   << expect.size());
                    return false;
                }
                ++me.received;
            }
        }
        if (sides[0].received == count && sides[1].received == count) return sides[0].ok && sides[1].ok;
        if (!progress) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    MESSAGE("timeout: a sent " << sides[0].sent << " recv " << sides[0].received << ", b sent " << sides[1].sent
                               << " recv " << sides[1].received);
    return false;
}

bool SendLarge(Transport& from, Transport& to, size_t size, uint64_t timeout_ms = 5000) {
    std::vector<unsigned char> big = MakeMessage(777, size, 5);
    if (!from.Send(big.data(), big.size())) return false;
    uint64_t deadline = NowMs() + timeout_ms;
    std::vector<unsigned char> got;
    while (NowMs() < deadline) {
        from.Poll();
        to.Poll();
        if (to.Receive(got)) return got == big;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

TEST_CASE("framing helpers") {
    detail::FrameParser p;
    std::vector<unsigned char> f1 = detail::MakeFrame("abc", 3);
    std::vector<unsigned char> f2 = detail::MakeFrame("", 0);
    std::vector<unsigned char> stream(f1);
    stream.insert(stream.end(), f2.begin(), f2.end());
    stream.insert(stream.end(), f1.begin(), f1.end());
    std::vector<unsigned char> out;
    p.Append(stream.data(), 2);
    CHECK_FALSE(p.Pop(out));
    p.Append(stream.data() + 2, 3);
    CHECK_FALSE(p.Pop(out));
    p.Append(stream.data() + 5, stream.size() - 5);
    REQUIRE(p.Pop(out));
    CHECK(std::string(out.begin(), out.end()) == "abc");
    REQUIRE(p.Pop(out));
    CHECK(out.empty());
    REQUIRE(p.Pop(out));
    CHECK(std::string(out.begin(), out.end()) == "abc");
    CHECK_FALSE(p.Pop(out));
    unsigned char bad[4];
    detail::PutU32LE(bad, 0xFFFFFFF0u);
    p.Append(bad, 4);
    CHECK_FALSE(p.Pop(out));
    CHECK(p.Corrupt());

    detail::FrameQueue q(10);
    CHECK(q.Push(std::vector<unsigned char>(6)) == 0);
    CHECK(q.Push(std::vector<unsigned char>(6)) == 1);
    CHECK(q.Pop(out));
    CHECK_FALSE(q.Pop(out));
}

TEST_CASE("CreateTransport parses schemes") {
    CHECK(CreateTransport("tcp://127.0.0.1:7777") != nullptr);
    CHECK(CreateTransport("pipe://ImForge") != nullptr);
    CHECK(CreateTransport("shm://ImForge") != nullptr);
    CHECK(CreateTransport("udp://x") == nullptr);
    CHECK(CreateTransport("") == nullptr);
    CHECK(CreateTransport(nullptr) == nullptr);

    TcpTransport t;
    CHECK_FALSE(t.Listen("tcp://127.0.0.1"));
    CHECK_FALSE(t.Listen("pipe://x"));
    CHECK_FALSE(t.Connect("tcp://127.0.0.1:abc"));
    CHECK_FALSE(t.IsConnected());
    unsigned char byte = 0;
    CHECK_FALSE(t.Send(&byte, 1));
    std::vector<unsigned char> out;
    CHECK_FALSE(t.Receive(out));
    CHECK(t.Stats().messages_sent == 0);
}

TEST_CASE("tcp loopback exchange, large message and reconnect") {
    TcpTransport server;
    REQUIRE(server.Listen("tcp://127.0.0.1:0"));
    int port = server.BoundPort();
    REQUIRE(port > 0);
    std::string url = "tcp://127.0.0.1:" + std::to_string(port);

    TcpTransport client;
    REQUIRE(client.Connect(url.c_str()));
    REQUIRE(WaitConnected(server, client));

    CHECK(Exchange(server, client, 1000, 20000));
    TransportStats ss = server.Stats();
    CHECK(ss.messages_sent == 1000);
    CHECK(ss.messages_received == 1000);
    CHECK(ss.drops == 0);

    CHECK(SendLarge(client, server, size_t(5) << 20));
    CHECK(SendLarge(server, client, size_t(5) << 20));

    std::vector<unsigned char> too_big(kTransportMaxFrame + 1);
    CHECK_FALSE(client.Send(too_big.data(), too_big.size()));
    CHECK(client.Stats().drops == 1);

    client.Close();
    CHECK_FALSE(client.IsConnected());
    REQUIRE(WaitDisconnected(server));

    TcpTransport client2;
    REQUIRE(client2.Connect(url.c_str()));
    REQUIRE(WaitConnected(server, client2));
    CHECK(Exchange(server, client2, 50, 4096));

    REQUIRE(client.Connect(url.c_str()));
    uint64_t deadline = NowMs() + 1000;
    while (NowMs() < deadline) {
        client.Poll();
        server.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(client2.IsConnected());
    CHECK(Exchange(server, client2, 10, 512));
    client2.Close();
    REQUIRE(WaitConnected(server, client, 4000));
    CHECK(Exchange(server, client, 50, 4096));
    server.Close();
    REQUIRE(WaitDisconnected(client));
}

TEST_CASE("tcp client retries until a server appears") {
    int port = 0;
    {
        TcpTransport probe;
        REQUIRE(probe.Listen("tcp://127.0.0.1:0"));
        port = probe.BoundPort();
        probe.Close();
    }
    std::string url = "tcp://127.0.0.1:" + std::to_string(port);
    TcpTransport client;
    REQUIRE(client.Connect(url.c_str()));
    uint64_t t0 = NowMs();
    while (NowMs() - t0 < 300) {
        client.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK_FALSE(client.IsConnected());
    TcpTransport server;
    REQUIRE(server.Listen(url.c_str()));
    REQUIRE(WaitConnected(server, client, 4000));
    CHECK(Exchange(server, client, 20, 1024));
}

TEST_CASE("pipe exchange, large message and reconnect") {
    std::string url = "pipe://" + UniqueName("imforge-test-");
    PipeTransport server;
    REQUIRE(server.Listen(url.c_str()));
    PipeTransport client;
    REQUIRE(client.Connect(url.c_str()));
    REQUIRE(WaitConnected(server, client));

    CHECK(Exchange(server, client, 1000, 20000));
    CHECK(SendLarge(client, server, size_t(5) << 20));
    CHECK(SendLarge(server, client, size_t(5) << 20));

    client.Close();
    REQUIRE(WaitDisconnected(server));
    REQUIRE(client.Connect(url.c_str()));
    REQUIRE(WaitConnected(server, client));
    CHECK(Exchange(server, client, 100, 8192));

    server.Close();
    REQUIRE(WaitDisconnected(client));
    REQUIRE(server.Listen(url.c_str()));
    REQUIRE(WaitConnected(server, client, 4000));
    CHECK(Exchange(server, client, 20, 1024));
}

TEST_CASE("shm exchange and reconnect") {
    std::string url = "shm://" + UniqueName("imforge-test-");
    ShmTransport server;
    REQUIRE(server.Listen(url.c_str()));
    CHECK_FALSE(server.IsConnected());
    ShmTransport client;
    REQUIRE(client.Connect(url.c_str()));
    REQUIRE(WaitConnected(server, client));

    CHECK(Exchange(server, client, 1000, 20000));
    CHECK(SendLarge(client, server, server.MaxPayload()));
    CHECK(SendLarge(server, client, server.MaxPayload()));

    std::vector<unsigned char> too_big(server.MaxPayload() + 1);
    CHECK_FALSE(client.Send(too_big.data(), too_big.size()));

    client.Close();
    REQUIRE(WaitDisconnected(server, 4000));
    REQUIRE(client.Connect(url.c_str()));
    REQUIRE(WaitConnected(server, client));
    CHECK(Exchange(server, client, 100, 8192));
}

TEST_CASE("shm server restart") {
    std::string url = "shm://" + UniqueName("imforge-test-");
    ShmTransport server;
    REQUIRE(server.Listen(url.c_str()));
    ShmTransport client;
    REQUIRE(client.Connect(url.c_str()));
    REQUIRE(WaitConnected(server, client));
    CHECK(Exchange(server, client, 20, 1024));

    server.Close();
    REQUIRE(WaitDisconnected(client, 4000));
    REQUIRE(server.Listen(url.c_str()));
    REQUIRE(WaitConnected(server, client, 4000));
    CHECK(Exchange(server, client, 20, 1024));
}

TEST_CASE("shm client before server") {
    std::string url = "shm://" + UniqueName("imforge-test-");
    ShmTransport client;
    REQUIRE(client.Connect(url.c_str()));
    client.Poll();
    CHECK_FALSE(client.IsConnected());
    ShmTransport server;
    REQUIRE(server.Listen(url.c_str()));
    REQUIRE(WaitConnected(server, client, 4000));
    CHECK(Exchange(server, client, 20, 1024));
}

TEST_CASE("shm small ring wraps correctly") {
    std::string url = "shm://" + UniqueName("imforge-test-");
    ShmTransport server(4096);
    REQUIRE(server.Listen(url.c_str()));
    ShmTransport client;
    REQUIRE(client.Connect(url.c_str()));
    REQUIRE(WaitConnected(server, client));
    CHECK(client.Capacity() == 4096);
    CHECK(Exchange(server, client, 2000, 4096 - 8));
    CHECK(SendLarge(client, server, 4096 - 8));
    CHECK(SendLarge(server, client, 4096 - 8));
    for (size_t sz = 1; sz < 4096 - 8; sz += 97) {
        CHECK(SendLarge(client, server, sz, 1000));
        CHECK(SendLarge(server, client, sz, 1000));
    }
    TransportStats st = server.Stats();
    CHECK(st.drops == 0);
    CHECK(st.messages_sent == st.messages_received);
}
