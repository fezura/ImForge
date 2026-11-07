#pragma once

// The builder keeps the current one-time code in a per-user file named after the endpoint:
//   Windows: %LOCALAPPDATA%\ImForge\live\<kind>-<name-or-port>.pair  (DACL: current user only)
//   Linux:   $XDG_RUNTIME_DIR/imforge/<kind>-<name>.pair, else ~/.cache/imforge/...  (dir 0700, file 0600)
//   {"code":"...","endpoint":"tcp://127.0.0.1:7777","issued":<unix time>,"pid":<builder pid>,"v":1}

#include "imforge_json.h"
#include "imforge_pairing.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
#include <aclapi.h>
#include <sddl.h>
#ifdef _MSC_VER
#pragma comment(lib, "advapi32.lib")
#endif
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace imforge {
namespace pairing {

constexpr size_t kPairingFileMaxBytes = 4096;

struct PairingFileData {
    int version = 0;
    std::string code;
    std::string endpoint;
    long long pid = 0;
    long long issued = 0;
    ~PairingFileData() { SecureWipe(code); }
};

namespace file_detail {

inline std::string& DirOverride() {
    static std::string dir;
    return dir;
}

inline bool IsNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

inline int CurrentPid() {
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)::getpid();
#endif
}

#ifdef _WIN32

inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n > 0 ? (size_t)n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? (size_t)n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

inline bool GetEnv(const char* name, std::string& out) {
    std::wstring wname = Widen(name);
    DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (n == 0) return false;
    std::wstring buf(n, L'\0');
    DWORD got = GetEnvironmentVariableW(wname.c_str(), &buf[0], n);
    if (got == 0 || got >= n) return false;
    buf.resize(got);
    out = Narrow(buf);
    return true;
}

struct UserSid {
    std::vector<unsigned char> buf;
    PSID sid = nullptr;
    std::wstring text;
    bool Load() {
        HANDLE tok = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
        DWORD n = 0;
        GetTokenInformation(tok, TokenUser, nullptr, 0, &n);
        buf.assign(n ? n : 1, 0);
        bool ok = n != 0 && GetTokenInformation(tok, TokenUser, buf.data(), n, &n) != 0;
        CloseHandle(tok);
        if (!ok) return false;
        sid = ((TOKEN_USER*)buf.data())->User.Sid;
        LPWSTR s = nullptr;
        if (!ConvertSidToStringSidW(sid, &s)) return false;
        text = s;
        LocalFree(s);
        return true;
    }
};

// owner and only ACE = current user, protected DACL so nothing is inherited
struct OwnerOnlySecurity {
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR sd = nullptr;
    OwnerOnlySecurity() { std::memset(&sa, 0, sizeof(sa)); }
    ~OwnerOnlySecurity() {
        if (sd) LocalFree(sd);
    }
    OwnerOnlySecurity(const OwnerOnlySecurity&) = delete;
    OwnerOnlySecurity& operator=(const OwnerOnlySecurity&) = delete;
    bool Init(bool directory) {
        UserSid me;
        if (!me.Load()) return false;
        std::wstring sddl = L"O:" + me.text + (directory ? L"D:P(A;OICI;FA;;;" : L"D:P(A;;FA;;;") + me.text + L")";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) return false;
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        sa.bInheritHandle = FALSE;
        return true;
    }
};

inline bool OwnedByCurrentUser(HANDLE h) {
    UserSid me;
    if (!me.Load()) return false;
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetSecurityInfo(h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS)
        return false;
    bool ok = owner && IsValidSid(owner) && EqualSid(owner, me.sid);
    LocalFree(sd);
    return ok;
}

inline bool EnsurePrivateDir(const std::string& dir, std::string* err) {
    std::wstring w = Widen(dir);
    OwnerOnlySecurity sec;
    if (!sec.Init(true)) {
        if (err) *err = "cannot build the owner-only security descriptor";
        return false;
    }
    if (!CreateDirectoryW(w.c_str(), &sec.sa) && GetLastError() != ERROR_ALREADY_EXISTS) {
        if (err) *err = "cannot create " + dir;
        return false;
    }
    HANDLE h = CreateFileW(w.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION info;
    bool plain = h != INVALID_HANDLE_VALUE && GetFileInformationByHandle(h, &info) != 0 &&
                 (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    if (!plain) {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        if (err) *err = dir + " is not a plain directory";
        return false;
    }
    bool mine = OwnedByCurrentUser(h);
    CloseHandle(h);
    if (!mine) {
        if (err) *err = dir + " is not owned by the current user";
        return false;
    }
    return true;
}

#else

inline bool GetEnv(const char* name, std::string& out) {
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    out = v;
    return true;
}

inline bool EnsurePrivateDir(const std::string& dir, std::string* err) {
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        if (err) *err = "cannot create " + dir;
        return false;
    }
    struct stat st;
    if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::geteuid()) {
        if (err) *err = dir + " is not a directory owned by the current user";
        return false;
    }
    if ((st.st_mode & 077) != 0 && ::chmod(dir.c_str(), 0700) != 0) {
        if (err) *err = "cannot restrict " + dir + " to mode 0700";
        return false;
    }
    return true;
}

#endif

inline char Sep() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

inline std::string Join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    return (last == '/' || last == '\\') ? dir + name : dir + Sep() + name;
}

}  // namespace file_detail

// for tests or a dir shared with a sandboxed app. both sides must agree, null/empty resets
inline void SetPairingDir(const char* dir) { file_detail::DirOverride() = dir ? dir : ""; }

// "tcp://127.0.0.1:7777" -> "tcp-7777", "pipe://ImForge" -> "pipe-ImForge", "shm://X" -> "shm-X"
inline std::string EndpointKey(const char* url) {
    if (!url) return std::string();
    std::string u = url;
    std::string kind, rest;
    size_t scheme = u.find("://");
    if (scheme == std::string::npos) return std::string();
    kind = u.substr(0, scheme);
    rest = u.substr(scheme + 3);
    if (kind == "tcp") {
        size_t colon = rest.rfind(':');
        std::string port = colon == std::string::npos ? std::string() : rest.substr(colon + 1);
        if (port.empty() || port.size() > 5) return std::string();
        for (char c : port)
            if (c < '0' || c > '9') return std::string();
        return "tcp-" + port;
    }
    if (kind != "pipe" && kind != "shm") return std::string();
    if (rest.empty()) return std::string();
    std::string name;
    for (size_t i = 0; i < rest.size() && name.size() < 64; ++i) name.push_back(file_detail::IsNameChar(rest[i]) ? rest[i] : '_');
    return kind + "-" + name;
}

// WritePairingFile creates it on demand
inline std::string PairingDir() {
    if (!file_detail::DirOverride().empty()) return file_detail::DirOverride();
    std::string base;
#ifdef _WIN32
    if (!file_detail::GetEnv("LOCALAPPDATA", base)) {
        if (!file_detail::GetEnv("USERPROFILE", base)) return std::string();
        base = file_detail::Join(base, "AppData\\Local");
    }
    return file_detail::Join(file_detail::Join(base, "ImForge"), "live");
#else
    if (file_detail::GetEnv("XDG_RUNTIME_DIR", base) && base[0] == '/') return file_detail::Join(base, "imforge");
    if (!file_detail::GetEnv("HOME", base)) return std::string();
    return file_detail::Join(file_detail::Join(base, ".cache"), "imforge");
#endif
}

inline std::string PairingFilePath(const char* url) {
    std::string key = EndpointKey(url);
    std::string dir = PairingDir();
    if (key.empty() || dir.empty()) return std::string();
    return file_detail::Join(dir, key + ".pair");
}

inline bool EnsurePairingDir(const std::string& dir, std::string* err) {
    if (!file_detail::DirOverride().empty()) return file_detail::EnsurePrivateDir(dir, err);
#ifdef _WIN32
    size_t cut = dir.find_last_of("\\/");
    std::wstring parent = cut == std::string::npos ? std::wstring() : file_detail::Widen(dir.substr(0, cut));
    if (parent.empty() || (!CreateDirectoryW(parent.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)) {
        if (err) *err = "cannot create " + dir.substr(0, cut == std::string::npos ? 0 : cut);
        return false;
    }
#else
    std::string xdg;
    if (!(file_detail::GetEnv("XDG_RUNTIME_DIR", xdg) && xdg[0] == '/')) {
        size_t cut = dir.find_last_of('/');
        std::string cache = cut == std::string::npos ? std::string() : dir.substr(0, cut);
        if (cache.empty() || (::mkdir(cache.c_str(), 0700) != 0 && errno != EEXIST)) {
            if (err) *err = "cannot create " + cache;
            return false;
        }
    }
#endif
    return file_detail::EnsurePrivateDir(dir, err);
}

inline std::string PairingFileText(const std::string& code, const std::string& endpoint) {
    Json j = Json::Object();
    j.Set("v", 1);
    j.Set("code", code);
    j.Set("endpoint", endpoint);
    j.Set("pid", file_detail::CurrentPid());
    j.Set("issued", (double)std::time(nullptr));
    std::string text = DumpJson(j, 0, true) + "\n";
    if (Json* c = j.Find("code")) SecureWipe(c->str);
    return text;
}

// temp file + rename so readers never see a half-written file
inline bool WritePairingFile(const std::string& path, const std::string& code, const std::string& endpoint, std::string* err) {
    if (path.empty()) {
        if (err) *err = "no pairing file location for " + endpoint;
        return false;
    }
    size_t cut = path.find_last_of("\\/");
    if (cut == std::string::npos || !EnsurePairingDir(path.substr(0, cut), err)) return false;
    std::string text = PairingFileText(code, endpoint);
    std::string tmp = path + "." + std::to_string(file_detail::CurrentPid()) + ".tmp";
    bool ok = false;
#ifdef _WIN32
    std::wstring wtmp = file_detail::Widen(tmp), wpath = file_detail::Widen(path);
    file_detail::OwnerOnlySecurity sec;
    DeleteFileW(wtmp.c_str());
    HANDLE h = sec.Init(false) ? CreateFileW(wtmp.c_str(), GENERIC_WRITE, 0, &sec.sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)
                               : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        ok = WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr) && written == (DWORD)text.size();
        CloseHandle(h);
        for (int i = 0; ok && i < 50; ++i) {
            if (MoveFileExW(wtmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING)) break;
            if (i == 49) ok = false;
            Sleep(10);
        }
        if (!ok) DeleteFileW(wtmp.c_str());
    }
#else
    ::unlink(tmp.c_str());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0) {
        ok = ::fchmod(fd, 0600) == 0;
        size_t off = 0;
        while (ok && off < text.size()) {
            ssize_t n = ::write(fd, text.data() + off, text.size() - off);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) ok = false;
            else off += (size_t)n;
        }
        ok = (::close(fd) == 0) && ok;
        ok = ok && ::rename(tmp.c_str(), path.c_str()) == 0;
        if (!ok) ::unlink(tmp.c_str());
    }
#endif
    SecureWipe(text);
    if (!ok && err) *err = "cannot write " + path;
    return ok;
}

// rejects links, files owned by someone else, group/world access (POSIX), oversized or malformed content
inline bool ReadPairingFile(const std::string& path, PairingFileData& out, std::string* err) {
    std::string text;
    auto fail = [&](const std::string& why) {
        SecureWipe(text);
        if (err) *err = why;
        return false;
    };
    if (path.empty()) return fail("no pairing file location");
#ifdef _WIN32
    std::wstring w = file_detail::Widen(path);
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail("no pairing file at " + path);
    BY_HANDLE_FILE_INFORMATION info;
    bool plain = GetFileInformationByHandle(h, &info) != 0 &&
                 !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                 info.nFileSizeHigh == 0 && info.nFileSizeLow <= kPairingFileMaxBytes;
    if (!plain) {
        CloseHandle(h);
        return fail(path + " is not a small regular file");
    }
    if (!file_detail::OwnedByCurrentUser(h)) {
        CloseHandle(h);
        return fail(path + " is not owned by the current user");
    }
    text.resize(info.nFileSizeLow);
    DWORD got = 0;
    bool read_ok = text.empty() || (ReadFile(h, &text[0], (DWORD)text.size(), &got, nullptr) && got == (DWORD)text.size());
    CloseHandle(h);
    if (!read_ok) return fail("cannot read " + path);
#else
    int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return fail(errno == ELOOP ? path + " is a symlink" : "no pairing file at " + path);
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (size_t)st.st_size > kPairingFileMaxBytes) {
        ::close(fd);
        return fail(path + " is not a small regular file");
    }
    if (st.st_uid != ::geteuid()) {
        ::close(fd);
        return fail(path + " is not owned by the current user");
    }
    if ((st.st_mode & 077) != 0) {
        ::close(fd);
        return fail(path + " is accessible by other users (expected mode 0600)");
    }
    text.resize((size_t)st.st_size);
    size_t off = 0;
    while (off < text.size()) {
        ssize_t n = ::read(fd, &text[off], text.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        off += (size_t)n;
    }
    ::close(fd);
    if (off != text.size()) return fail("cannot read " + path);
#endif
    Json j;
    if (!ParseJson(text, j) || !j.IsObject()) return fail(path + " is not valid JSON");
    if (j["v"].AsInt() != 1) return fail(path + " has an unsupported version");
    std::string normalized;
    if (!j["code"].IsString() || !NormalizeCode(j["code"].AsString(), normalized)) return fail(path + " holds no valid code");
    out.version = 1;
    SecureWipe(out.code);
    out.code = FormatCode(normalized);
    SecureWipe(normalized);
    out.endpoint = j["endpoint"].IsString() ? j["endpoint"].AsString().substr(0, 256) : std::string();
    out.pid = j["pid"].IsNumber() ? j["pid"].AsInt64() : 0;
    out.issued = j["issued"].IsNumber() ? j["issued"].AsInt64() : 0;
    if (Json* c = j.Find("code")) SecureWipe(c->str);
    SecureWipe(text);
    return true;
}

inline bool DeletePairingFile(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    return DeleteFileW(file_detail::Widen(path).c_str()) != 0;
#else
    return ::unlink(path.c_str()) == 0;
#endif
}

inline bool PairingFileExists(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    return GetFileAttributesW(file_detail::Widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return ::lstat(path.c_str(), &st) == 0;
#endif
}

}  // namespace pairing
}  // namespace imforge
