#pragma once

// Pairing handshake, transport-agnostic. Caller feeds messages in, sends replies, closes when told.
//
//   app -> builder  hello {v:2, pairing:true, nonce_a}                       (nothing else before authentication)
//   builder -> app  pair_challenge {nonce_b, session_id, mac_b = HMAC(K, "imforge-builder|na|nb|sid")}
//   app -> builder  pair_response {session_id, mac_a = HMAC(K, "imforge-app|na|nb|sid")}
//   builder -> app  pair_ok {session_id}  |  pair_fail {attempts_left, reason} + close
//   K = SHA256("imforge-pair-v1|" + normalized one-time code)
// After pair_ok everything is sealed (imforge_pairing_channel.h), first sealed app message is client_info.

#include "imforge_json.h"
#include "imforge_pairing.h"
#include "imforge_pairing_channel.h"
#include "imforge_pairing_file.h"
#include "imforge_protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace imforge {
namespace pairing {

constexpr int kMaxFailures = 5;
constexpr uint64_t kRefuseMs = 3000;
constexpr uint64_t kCodeLifetimeMs = 10ull * 60ull * 1000ull;
constexpr uint64_t kHandshakeTimeoutMs = 5000;
constexpr uint64_t kHelloTimeoutMs = 5000;
constexpr size_t kMaxHelloBytes = size_t(2) << 20;
constexpr size_t kMaxHandshakeBytes = 4096;

struct LogLine {
    int level = 0;  // 0 debug, 1 info, 2 warning, 3 error
    std::string text;
    bool security = false;  // integrity violation on an established link
};

class Server {
public:
    enum class State { Stopped, Idle, AwaitHello, Challenged, Paired, Unauthenticated };
    // Reply: send `reply` as a plain Frame(). Paired: send `reply` (pair_ok, still plain), sealed from then on.
    // Drop: close now without replying (oversized pre-pairing frame or a failed MAC, which is a security event).
    // Ignore: unauthenticated, malformed or out of sequence, throw it away.
    enum class Verdict { Ignore, Accept, Reply, ReplyAndDrop, Paired, Drop };

    std::string endpoint;     // URL clients connect to; selects the pairing file
    bool require = true;      // false also accepts legacy unauthenticated hellos
    bool write_file = true;
    std::string fixed_code;   // for tests, every rotation reuses it

    Server() {}
    ~Server() { Stop(); }
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool Start(uint64_t now) {
        Stop();
        state_ = State::Idle;
        failures_ = 0;
        refuse_until_ = 0;
        Rotate(now, "server started");
        return !code_.empty();
    }

    void Stop() {
        if (state_ == State::Stopped) return;
        WipeSession();
        SecureWipe(code_);
        if (file_written_ && DeletePairingFile(file_path_)) Log(1, "pairing file deleted: " + file_path_);
        file_written_ = false;
        state_ = State::Stopped;
    }

    void Rotate(uint64_t now, const char* reason) {
        SecureWipe(code_);
        if (!fixed_code.empty()) {
            std::string n;
            if (NormalizeCode(fixed_code, n)) code_ = FormatCode(n);
            SecureWipe(n);
        } else {
            code_ = GenerateCode();
        }
        issued_ = now;
        failures_ = 0;
        ++generation_;
        if (code_.empty()) {
            if (file_written_) {
                DeletePairingFile(file_path_);
                file_written_ = false;
            }
            Log(3, "cannot generate a pairing code (no OS random source); pairing is impossible");
            return;
        }
        file_error_.clear();
        if (write_file) {
            file_path_ = PairingFilePath(endpoint.c_str());
            if (WritePairingFile(file_path_, code_, endpoint, &file_error_)) {
                file_written_ = true;
                Log(1, std::string("new one-time pairing code (") + reason + "), published in " + file_path_);
            } else {
                if (file_written_) DeletePairingFile(file_path_);
                file_written_ = false;
                Log(2, std::string("new one-time pairing code (") + reason + ") but the pairing file failed: " + file_error_);
            }
        } else {
            Log(1, std::string("new one-time pairing code (") + reason + ")");
        }
    }

    void OnConnect(uint64_t now) {
        if (state_ == State::Stopped) return;
        WipeSession();
        state_ = State::AwaitHello;
        conn_at_ = now;
        ignored_ = 0;
    }

    // Only a wrong pair_response counts as a failed attempt. A peer that drops after the challenge hasn't
    // guessed anything, and a port squatter relaying the app shouldn't be able to burn our attempts.
    void OnDisconnect(uint64_t now) {
        (void)now;
        if (state_ == State::Stopped) return;
        if (state_ == State::Challenged) Abandon("the app closed the connection during the handshake");
        else if (state_ == State::Paired) Log(1, "paired session with " + Peer() + " ended; session keys wiped");
        else if (state_ == State::Unauthenticated) Log(1, "unauthenticated session with " + Peer() + " ended");
        WipeSession();
        state_ = State::Idle;
    }

    // every received payload comes through here. on a paired link MAC/seq is checked before any JSON parsing
    Verdict OnFrame(const void* data, size_t len, uint64_t now, Json& msg, Json& reply) {
        msg = Json();
        reply = Json();
        if (state_ == State::Paired) {
            const unsigned char* payload = nullptr;
            size_t n = 0;
            Channel::Status st = channel_.Open(data, len, &payload, &n);
            if (st != Channel::Status::Ok) return Violation(channel_.Describe(st));
            if (!Unframe(payload, n, msg)) {
                msg = Json();
                Log(1, "malformed message (" + std::to_string(n) + " bytes) from " + Peer() + " dropped");
                return Verdict::Ignore;
            }
            return Route(msg, now, reply);
        }
        size_t max_bytes = MaxFrameBytes();
        if (state_ != State::Stopped && max_bytes != 0 && len > max_bytes) {
            Log(2, "oversized message (" + std::to_string(len) + " bytes) before pairing; connection dropped");
            return Verdict::Drop;
        }
        if (!Unframe(data, len, msg)) {
            msg = Json();
            Log(1, "malformed message (" + std::to_string(len) + " bytes) dropped");
            return Verdict::Ignore;
        }
        return OnMessage(msg, now, reply);
    }
    Verdict OnFrame(const std::vector<unsigned char>& frame, uint64_t now, Json& msg, Json& reply) {
        return OnFrame(frame.data(), frame.size(), now, msg, reply);
    }

    // unsealed messages. on a paired link that's a security event
    Verdict OnMessage(const Json& m, uint64_t now, Json& reply) {
        reply = Json();
        if (state_ == State::Paired)
            return Violation("unsealed '" + SanitizeText(m["t"].AsString(), 64) + "' message on a paired link");
        return Route(m, now, reply);
    }

    // sealed when paired, plain on a legacy session, false before either. handshake replies use plain Frame(reply)
    bool Encode(const Json& m, std::string& out) {
        out.clear();
        if (state_ == State::Unauthenticated) {
            out = Frame(m);
            return true;
        }
        if (state_ != State::Paired) return false;
        return channel_.Seal(Frame(m), out);
    }

    // code expiry and timeouts. true means close the connection
    bool Tick(uint64_t now) {
        if (state_ == State::Stopped) return false;
        if (state_ != State::Challenged && now - issued_ >= kCodeLifetimeMs) Rotate(now, "unused for 10 minutes");
        if (state_ == State::AwaitHello && now - conn_at_ >= kHelloTimeoutMs) {
            Log(2, "connection sent no hello within 5 s; dropped");
            state_ = State::Idle;
            return true;
        }
        if (state_ == State::Challenged && now >= deadline_) {
            Abandon("no pair_response within 5 s");
            return true;
        }
        return false;
    }

    // 0 = no limit (established). the pairing hello is tiny, only a legacy hello with metadata can be big
    size_t MaxFrameBytes() const {
        if (Established()) return 0;
        return state_ == State::AwaitHello && !require ? kMaxHelloBytes : kMaxHandshakeBytes;
    }

    State GetState() const { return state_; }
    bool Running() const { return state_ != State::Stopped; }
    bool Established() const { return state_ == State::Paired || state_ == State::Unauthenticated; }
    bool Refusing(uint64_t now) const { return now < refuse_until_; }
    uint64_t RefuseUntil() const { return refuse_until_; }
    const std::string& Code() const { return code_; }
    uint64_t IssuedAt() const { return issued_; }
    uint64_t ExpiresAt() const { return issued_ + kCodeLifetimeMs; }
    const std::string& FilePath() const { return file_path_; }
    const std::string& FileError() const { return file_error_; }
    bool FileWritten() const { return file_written_; }
    int Failures() const { return failures_; }
    int TotalFailures() const { return total_failures_; }
    int AttemptsLeft() const { return kMaxFailures - failures_; }
    int Pairings() const { return pairings_; }
    int Generation() const { return generation_; }
    const std::string& LastFailure() const { return last_failure_; }
    int SecurityEvents() const { return security_events_; }
    const std::string& LastSecurityEvent() const { return last_security_event_; }
    int AbandonedHandshakes() const { return abandoned_; }
    bool ClientInfoReceived() const { return info_received_; }
    const Channel& SessionChannel() const { return channel_; }
    const std::string& PeerApp() const { return peer_app_; }
    int PeerPid() const { return peer_pid_; }
    const std::string& SessionId() const { return session_id_; }
    uint64_t IgnoredMessages() const { return ignored_; }

    std::vector<LogLine> TakeLog() {
        std::vector<LogLine> out;
        out.swap(log_);
        return out;
    }

private:
    Verdict Route(const Json& m, uint64_t now, Json& reply) {
        const std::string& t = m["t"].AsString();
        bool handshake = t == msg::kHello || t == msg::kPairResponse || t == msg::kPairChallenge || t == msg::kPairOk ||
                         t == msg::kPairFail;
        if (state_ == State::Paired || state_ == State::Unauthenticated) {
            if (handshake) return Ignore(t);
            if (t == msg::kClientInfo) OnClientInfo(m);
            return Verdict::Accept;
        }
        if (state_ == State::AwaitHello && t == msg::kHello) return OnHello(m, now, reply);
        if (state_ == State::Challenged && t == msg::kPairResponse) return OnResponse(m, now, reply);
        return Ignore(t);
    }

    void OnClientInfo(const Json& m) {
        ClientInfoMsg info;
        if (!ParseClientInfo(m, info)) return;
        bool first = !info_received_;
        info_received_ = true;
        peer_app_ = SanitizeText(info.app, kProtocolMaxName);
        peer_pid_ = info.pid;
        if (first)
            Log(1, std::string(state_ == State::Paired ? "authenticated" : "UNAUTHENTICATED") + " client_info from " + Peer());
    }

    Verdict OnHello(const Json& m, uint64_t now, Json& reply) {
        HelloMsg h;
        if (!ParseHello(m, h)) return Ignore(msg::kHello);
        if (!h.pairing) {
            peer_app_ = SanitizeText(h.app, kProtocolMaxName);
            peer_pid_ = h.pid;
            if (!require) {
                state_ = State::Unauthenticated;
                info_received_ = true;
                Log(2, "UNAUTHENTICATED client " + Peer() + " accepted: pairing is disabled in the builder");
                return Verdict::Accept;
            }
            Log(2, "client " + Peer() + " did not request pairing (old imforge_client.h?); refused");
            reply = MsgPairFail(AttemptsLeft(), "pairing required: rebuild the app with the current imforge_client.h");
            state_ = State::Idle;
            return Verdict::ReplyAndDrop;
        }
        if (h.version != kProtocolVersion) {
            Log(2, "app speaks live protocol v" + std::to_string(h.version) + ", this builder v" +
                       std::to_string(kProtocolVersion) + "; refused");
            reply = MsgPairFail(AttemptsLeft(), "live protocol version mismatch: rebuild the app with the current imforge_client.h");
            state_ = State::Idle;
            return Verdict::ReplyAndDrop;
        }
        if (h.nonce_a.empty() || code_.empty()) {
            Log(2, "malformed pairing hello from " + Peer() + "; refused");
            reply = MsgPairFail(AttemptsLeft(), code_.empty() ? "builder has no pairing code" : "malformed pairing hello");
            state_ = State::Idle;
            return Verdict::ReplyAndDrop;
        }
        nonce_a_ = h.nonce_a;
        nonce_b_ = RandomHex(kNonceBytes);
        session_id_ = RandomHex(kNonceBytes);
        if (nonce_b_.empty() || session_id_.empty() || !DeriveKey(code_, key_)) {
            WipeSession();
            Log(3, "cannot derive pairing material; refused " + Peer());
            reply = MsgPairFail(AttemptsLeft(), "builder could not create a challenge");
            state_ = State::Idle;
            return Verdict::ReplyAndDrop;
        }
        have_key_ = true;
        reply = MsgPairChallenge(nonce_b_, session_id_, HandshakeMac(key_, kBuilderLabel, nonce_a_, nonce_b_, session_id_));
        state_ = State::Challenged;
        deadline_ = now + kHandshakeTimeoutMs;
        Log(1, "pairing requested by " + Peer());
        return Verdict::Reply;
    }

    Verdict OnResponse(const Json& m, uint64_t now, Json& reply) {
        PairResponseMsg r;
        bool ok = ParsePairResponse(m, r) && r.session_id == session_id_;
        if (ok) {
            std::string expect = HandshakeMac(key_, kAppLabel, nonce_a_, nonce_b_, session_id_);
            ok = ConstantTimeEqual(expect, r.mac_a);
            SecureWipe(expect);
        }
        if (!ok) {
            int left = Fail(now, "wrong pairing response (the app does not know the current code)");
            reply = MsgPairFail(left, "wrong pairing code");
            return Verdict::ReplyAndDrop;
        }
        if (!channel_.Init(key_, nonce_a_, nonce_b_, session_id_, true)) {
            WipeSession();
            Log(3, "cannot derive session keys; refused " + Peer());
            reply = MsgPairFail(AttemptsLeft(), "builder could not derive session keys");
            state_ = State::Idle;
            return Verdict::ReplyAndDrop;
        }
        std::string sid = session_id_;
        reply = MsgPairOk(sid);
        WipeKeyMaterial();
        session_id_ = sid;
        SecureWipe(sid);
        state_ = State::Paired;
        info_received_ = false;
        ++pairings_;
        Log(1, "paired; every further message is authenticated (MAC + sequence number); waiting for client_info");
        Rotate(now, "previous code consumed by the paired app");
        return Verdict::Paired;
    }

    void Rotate(uint64_t now, const std::string& reason) { Rotate(now, reason.c_str()); }

    int Fail(uint64_t now, const std::string& why) {
        ++failures_;
        ++total_failures_;
        last_failure_ = why;
        int left = kMaxFailures - failures_;
        Log(2, "pairing attempt failed from " + Peer() + ": " + why + " (" + std::to_string(failures_) + "/" +
                   std::to_string(kMaxFailures) + ")");
        WipeSession();
        state_ = State::Idle;
        if (left <= 0) {
            Rotate(now, "code burned after 5 failed attempts");
            refuse_until_ = now + kRefuseMs;
            Log(2, "refusing new connections for 3 s");
            left = 0;
        }
        return left;
    }

    void Abandon(const std::string& why) {
        ++abandoned_;
        Log(1, "handshake with " + Peer() + " abandoned: " + why + " (not a failed attempt; only a wrong pair_response counts)");
        WipeSession();
        state_ = State::Idle;
    }

    Verdict Violation(const std::string& why) {
        ++security_events_;
        last_security_event_ = why;
        Log(3, "SECURITY: " + why + " from " + Peer() + "; connection dropped, session keys wiped", true);
        WipeSession();
        state_ = State::Idle;
        return Verdict::Drop;
    }

    Verdict Ignore(const std::string& t) {
        if (ignored_++ == 0)
            Log(0, "ignored '" + SanitizeText(t, 64) + "' (" + (Established() ? "out of sequence" : "not paired") + ")");
        return Verdict::Ignore;
    }

    std::string Peer() const {
        if (peer_app_.empty() && peer_pid_ == 0) return "an unidentified app";
        return (peer_app_.empty() ? std::string("unknown app") : "'" + peer_app_ + "'") + " pid " + std::to_string(peer_pid_);
    }

    void WipeKeyMaterial() {
        SecureWipe(key_, sizeof(key_));
        have_key_ = false;
        SecureWipe(nonce_a_);
        SecureWipe(nonce_b_);
        SecureWipe(session_id_);
    }

    void WipeSession() {
        WipeKeyMaterial();
        channel_.Wipe();
        info_received_ = false;
        peer_app_.clear();
        peer_pid_ = 0;
    }

    void Log(int level, const std::string& text, bool security = false) {
        LogLine l;
        l.level = level;
        l.text = text;
        l.security = security;
        if (log_.size() < 256) log_.push_back(std::move(l));
    }

    State state_ = State::Stopped;
    std::string code_;
    uint64_t issued_ = 0;
    int generation_ = 0;
    std::string file_path_, file_error_;
    bool file_written_ = false;
    int failures_ = 0, total_failures_ = 0, pairings_ = 0, security_events_ = 0, abandoned_ = 0;
    std::string last_failure_, last_security_event_;
    uint64_t refuse_until_ = 0, conn_at_ = 0, deadline_ = 0;
    uint64_t ignored_ = 0;
    unsigned char key_[32] = {};
    bool have_key_ = false;
    std::string nonce_a_, nonce_b_, session_id_;
    Channel channel_;
    bool info_received_ = false;
    std::string peer_app_;
    int peer_pid_ = 0;
    std::vector<LogLine> log_;
};

class ClientHandshake {
public:
    // Rejected: builder sent pair_fail. Failed: builder couldn't prove it knows the code (or confirmed another session).
    enum class Result { Ignore, Reply, Paired, Rejected, Failed };

    ClientHandshake() {}
    ~ClientHandshake() { Wipe(); }
    ClientHandshake(const ClientHandshake&) = delete;
    ClientHandshake& operator=(const ClientHandshake&) = delete;

    bool Begin(const std::string& code) {
        Wipe();
        error_.clear();
        attempts_left_ = -1;
        nonce_a_ = RandomHex(kNonceBytes);
        if (nonce_a_.empty() || !DeriveKey(code, key_)) {
            Wipe();
            error_ = "invalid pairing code";
            return false;
        }
        step_ = Step::AwaitChallenge;
        return true;
    }

    Result OnMessage(const Json& m, Json& reply) {
        reply = Json();
        if (step_ == Step::Idle || step_ == Step::Paired) return Result::Ignore;
        if (IsMsg(m, msg::kPairFail)) {
            PairFailMsg f;
            ParsePairFail(m, f);
            attempts_left_ = f.attempts_left;
            error_ = "builder rejected pairing: " + (f.reason.empty() ? std::string("no reason") : f.reason);
            Wipe();
            return Result::Rejected;
        }
        if (step_ == Step::AwaitChallenge && IsMsg(m, msg::kPairChallenge)) {
            PairChallengeMsg c;
            if (!ParsePairChallenge(m, c)) return Result::Ignore;
            std::string expect = HandshakeMac(key_, kBuilderLabel, nonce_a_, c.nonce_b, c.session_id);
            bool ok = ConstantTimeEqual(expect, c.mac_b);
            SecureWipe(expect);
            if (!ok) {
                error_ = "the builder could not prove it knows the pairing code (stale pairing file, or not the ImForge builder)";
                Wipe();
                return Result::Failed;
            }
            nonce_b_ = c.nonce_b;
            session_id_ = c.session_id;
            reply = MsgPairResponse(session_id_, HandshakeMac(key_, kAppLabel, nonce_a_, nonce_b_, session_id_));
            step_ = Step::AwaitOk;
            return Result::Reply;
        }
        if (step_ == Step::AwaitOk && IsMsg(m, msg::kPairOk)) {
            std::string sid;
            if (!ParsePairOk(m, sid) || sid != session_id_) {
                error_ = "the builder confirmed a different session";
                Wipe();
                return Result::Failed;
            }
            if (!channel_.Init(key_, nonce_a_, nonce_b_, session_id_, false)) {
                error_ = "could not derive session keys";
                Wipe();
                return Result::Failed;
            }
            std::string keep = session_id_;
            WipeHandshake();
            session_id_ = keep;
            SecureWipe(keep);
            step_ = Step::Paired;
            return Result::Paired;
        }
        return Result::Ignore;
    }

    // a failed Open() wipes the keys, drop the link
    bool Seal(const std::string& payload, std::string& out) {
        out.clear();
        return step_ == Step::Paired && channel_.Seal(payload, out);
    }
    Channel::Status Open(const void* frame, size_t len, const unsigned char** payload, size_t* payload_len) {
        if (step_ != Step::Paired) return Channel::Status::Inactive;
        return channel_.Open(frame, len, payload, payload_len);
    }
    std::string Describe(Channel::Status s) const { return channel_.Describe(s); }
    const Channel& SessionChannel() const { return channel_; }

    void Wipe() {
        WipeHandshake();
        channel_.Wipe();
        step_ = Step::Idle;
    }

    bool Active() const { return step_ == Step::AwaitChallenge || step_ == Step::AwaitOk; }
    bool Paired() const { return step_ == Step::Paired; }
    const std::string& NonceA() const { return nonce_a_; }
    const std::string& SessionId() const { return session_id_; }
    int AttemptsLeft() const { return attempts_left_; }
    const std::string& Error() const { return error_; }

private:
    void WipeHandshake() {
        SecureWipe(key_, sizeof(key_));
        SecureWipe(nonce_a_);
        SecureWipe(nonce_b_);
        SecureWipe(session_id_);
    }

    enum class Step { Idle, AwaitChallenge, AwaitOk, Paired };
    Step step_ = Step::Idle;
    Channel channel_;
    unsigned char key_[32] = {};
    std::string nonce_a_, nonce_b_, session_id_;
    int attempts_left_ = -1;
    std::string error_;
};

}  // namespace pairing
}  // namespace imforge
