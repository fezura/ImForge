#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "imforge/imforge_core.h"

#include "../src/builder/imblib.cpp"

using namespace imforge;
using namespace imforge::builder;

namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path path;
    TempDir() {
        static int counter = 0;
        auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("imforge_imblib_sec_" + std::to_string((long long)ticks) + "_" + std::to_string(++counter));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string Sub(const std::string& name) const { return (path / name).generic_string(); }
};

using Files = std::vector<std::pair<std::string, std::string>>;

// miniz refuses to write hostile entry names, so write a same-length placeholder and patch the raw bytes.
std::string MakeZip(const Files& files, const std::string& evil_name = std::string()) {
    std::string placeholder(evil_name.size(), 'Q');
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    REQUIRE(mz_zip_writer_init_heap(&zip, 0, 64 * 1024));
    for (const auto& f : files) {
        const std::string& name = (!evil_name.empty() && f.first == evil_name) ? placeholder : f.first;
        REQUIRE(mz_zip_writer_add_mem(&zip, name.c_str(), f.second.data(), f.second.size(), MZ_DEFAULT_LEVEL));
    }
    void* buf = nullptr;
    size_t size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&zip, &buf, &size));
    std::string bytes((const char*)buf, size);
    mz_free(buf);
    mz_zip_writer_end(&zip);
    if (!evil_name.empty()) {
        for (size_t pos = bytes.find(placeholder); pos != std::string::npos; pos = bytes.find(placeholder, pos + 1))
            bytes.replace(pos, placeholder.size(), evil_name);
    }
    return bytes;
}

size_t CentralOffset(const std::string& bytes, mz_uint index) {
    mz_zip_archive r;
    std::memset(&r, 0, sizeof(r));
    REQUIRE(mz_zip_reader_init_mem(&r, bytes.data(), bytes.size(), 0));
    mz_zip_archive_file_stat st;
    REQUIRE(mz_zip_reader_file_stat(&r, index, &st));
    size_t ofs = (size_t)(r.m_central_directory_file_ofs + st.m_central_dir_ofs);
    mz_zip_reader_end(&r);
    return ofs;
}

void PatchSymlink(std::string& bytes, mz_uint index) {
    size_t c = CentralOffset(bytes, index);
    unsigned char made[2] = {0x1E, 0x03};
    std::memcpy(&bytes[c + 4], made, 2);
    unsigned attr = (0120777u << 16);
    unsigned char a[4] = {(unsigned char)(attr & 0xFF), (unsigned char)((attr >> 8) & 0xFF), (unsigned char)((attr >> 16) & 0xFF),
                          (unsigned char)((attr >> 24) & 0xFF)};
    std::memcpy(&bytes[c + 38], a, 4);
}

void PatchDeclaredSize(std::string& bytes, mz_uint index, unsigned size) {
    size_t c = CentralOffset(bytes, index);
    unsigned char a[4] = {(unsigned char)(size & 0xFF), (unsigned char)((size >> 8) & 0xFF), (unsigned char)((size >> 16) & 0xFF),
                          (unsigned char)((size >> 24) & 0xFF)};
    std::memcpy(&bytes[c + 24], a, 4);
}

void Spit(const std::string& path, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), (std::streamsize)bytes.size());
}

std::string Manifest(const std::string& components = "[]", const std::string& headers = "[]") {
    return R"({"schemaVersion":1,"kind":"imblib","name":"evil","version":"1.0.0","components":)" + components +
           R"(,"widgets":[],"headers":)" + headers + R"(,"dependencies":[]})";
}

const char* kComponent = R"({"schemaVersion":1,"kind":"component","name":"NAME","root":{"id":"r","type":"VStack"}})";

std::string ComponentNamed(const std::string& name) {
    std::string s = kComponent;
    s.replace(s.find("NAME"), 4, name);
    return s;
}

bool Imports(const std::string& zip_bytes, const TempDir& tmp, std::string* err, ImblibImportResult* res = nullptr) {
    std::string lib = tmp.Sub("in/evil.imblib");
    Spit(lib, zip_bytes);
    Project project;
    project.dir = tmp.Sub("proj");
    fs::create_directories(project.dir);
    WidgetCatalog catalog;
    project.widget_catalog = &catalog;
    return imblib::Import(project, &catalog, lib, err, res);
}

bool AnyFileOutside(const TempDir& tmp) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(tmp.path, ec), end; !ec && it != end; it.increment(ec)) {
        fs::path rel = it->path().lexically_relative(tmp.path);
        std::string first = rel.begin()->string();
        if (first != "proj" && first != "in") return true;
    }
    return false;
}

}  // namespace

TEST_CASE("imblib import rejects zip-slip, absolute, drive-letter and backslash entry names") {
    const char* bad_names[] = {"../evil.json", "a/../../evil.json", "/abs/evil.json", "C:/evil.json",
                               "dir\\evil.json", "a//b.json", "components/../../../evil.imc.json"};
    for (const char* bad : bad_names) {
        TempDir tmp;
        std::string err;
        std::string zip = MakeZip({{"manifest.json", Manifest()}, {bad, "{}"}}, bad);
        CHECK_FALSE_MESSAGE(Imports(zip, tmp, &err), bad);
        CHECK_MESSAGE(err.find("unsafe entry name") != std::string::npos, bad << ": " << err);
        CHECK_FALSE(AnyFileOutside(tmp));
        LibraryInfo info;
        CHECK_FALSE(imblib::ReadLibraryInfo(tmp.Sub("in/evil.imblib"), info));
        CHECK_FALSE(info.valid);
    }
}

TEST_CASE("imblib import rejects symlink entries") {
    TempDir tmp;
    std::string zip = MakeZip({{"manifest.json", Manifest()}, {"link.txt", "/etc/passwd"}});
    PatchSymlink(zip, 1);
    std::string err;
    CHECK_FALSE(Imports(zip, tmp, &err));
    CHECK(err.find("symlink") != std::string::npos);
}

TEST_CASE("imblib import caps per-entry and total uncompressed size") {
    SUBCASE("single entry larger than 16 MB") {
        TempDir tmp;
        std::string zip = MakeZip({{"manifest.json", Manifest()}, {"big.txt", std::string(size_t(17) << 20, '\0')}});
        std::string err;
        CHECK_FALSE(Imports(zip, tmp, &err));
        CHECK(err.find("too large") != std::string::npos);
    }
    SUBCASE("many entries adding up to more than 64 MB") {
        TempDir tmp;
        Files files = {{"manifest.json", Manifest()}};
        for (int i = 0; i < 5; ++i) files.emplace_back("part" + std::to_string(i) + ".txt", std::string(size_t(14) << 20, 'a'));
        std::string err;
        CHECK_FALSE(Imports(MakeZip(files), tmp, &err));
        CHECK(err.find("64 MB") != std::string::npos);
    }
    SUBCASE("declared size in the directory is not trusted") {
        TempDir tmp;
        std::string zip = MakeZip({{"manifest.json", Manifest()}, {"lie.txt", std::string(1000, 'x')}});
        PatchDeclaredSize(zip, 1, 20u << 20);
        std::string err;
        CHECK_FALSE(Imports(zip, tmp, &err));
        zip = MakeZip({{"manifest.json", Manifest()}, {"lie.txt", std::string(1000, 'x')}});
        PatchDeclaredSize(zip, 1, 10);
        CHECK_FALSE(Imports(zip, tmp, &err));
    }
}

TEST_CASE("imblib import rejects duplicate entries") {
    TempDir tmp;
    std::string err;
    CHECK_FALSE(Imports(MakeZip({{"manifest.json", Manifest()}, {"a.txt", "1"}, {"a.txt", "2"}}), tmp, &err));
    CHECK(err.find("duplicate") != std::string::npos);
}

TEST_CASE("imblib import ignores file types outside the allowlist") {
    TempDir tmp;
    std::string err;
    ImblibImportResult res;
    std::string zip = MakeZip({{"manifest.json", Manifest("[]", R"(["run.exe","ok.h"])")},
                               {"run.exe", "MZ"},
                               {"ok.h", "#pragma once\n"}});
    REQUIRE_MESSAGE(Imports(zip, tmp, &err, &res), err);
    CHECK_FALSE(fs::exists(tmp.path / "proj/libs/evil/run.exe"));
    CHECK(fs::exists(tmp.path / "proj/libs/evil/ok.h"));
    CHECK(res.warnings.size() == 1);
}

TEST_CASE("imblib import derives file names from sanitized identifiers, never raw JSON names") {
    TempDir tmp;
    std::string err;
    ImblibImportResult res;
    std::string zip = MakeZip({{"manifest.json", Manifest(R"(["components/a.imc.json","components/b.imc.json"])")},
                               {"components/a.imc.json", ComponentNamed("../../pwned")},
                               {"components/b.imc.json", ComponentNamed("C:\\\\Windows\\\\evil")}});
    REQUIRE_MESSAGE(Imports(zip, tmp, &err, &res), err);
    CHECK(res.components.size() == 2);
    CHECK_FALSE(AnyFileOutside(tmp));
    for (const std::string& f : res.files) {
        for (const fs::path& part : fs::path(f)) CHECK(part.string() != "..");
        CHECK(f.find('\\') == std::string::npos);
        CHECK(f.find(':') == std::string::npos);
    }
    CHECK(fs::exists(tmp.path / "proj/components/.._.._pwned.imc.json"));
    CHECK_FALSE(fs::exists(tmp.path / "pwned.imc.json"));
    CHECK_FALSE(fs::exists(tmp.path / "proj/pwned.imc.json"));
}

TEST_CASE("imblib import never writes through a symlinked project subdirectory") {
    TempDir tmp;
    TempDir outside;
    fs::create_directories(tmp.path / "proj");
    std::error_code ec;
    fs::create_directory_symlink(outside.path, tmp.path / "proj/components", ec);
    if (ec) return;
    std::string err;
    std::string zip = MakeZip({{"manifest.json", Manifest(R"(["components/a.imc.json"])")},
                               {"components/a.imc.json", ComponentNamed("Card")}});
    CHECK_FALSE(Imports(zip, tmp, &err));
    CHECK(err.find("outside the project") != std::string::npos);
    CHECK(fs::is_empty(outside.path));
}

TEST_CASE("imblib import refuses widget manifests that would inject code into exports") {
    struct Case {
        const char* include;
        const char* call;
    };
    const Case cases[] = {{"../../x.h", "ns::Ok({label})"},
                          {"ok.h", "ns::Ok({label}); system(\\\"x\\\")"},
                          {"ok.h", "ns::Ok({label})\\n#include <x>"},
                          {"ok.h", "ns::Ok([&]() { x(); })"}};
    for (const Case& c : cases) {
        TempDir tmp;
        std::string err;
        std::string widget = std::string(R"({"schemaVersion":1,"kind":"widget","name":"Evil","include":")") + c.include +
                             R"(","call":")" + c.call + R"("})";
        std::string manifest = R"({"schemaVersion":1,"kind":"imblib","name":"evil","version":"1.0.0","components":[],)"
                               R"("widgets":["widgets/Evil.imw.json"],"headers":["ok.h"],"dependencies":[]})";
        std::string zip = MakeZip({{"manifest.json", manifest}, {"widgets/Evil.imw.json", widget}, {"ok.h", "// x"}});
        INFO(c.call);
        Spit(tmp.Sub("in/evil.imblib"), zip);
        Project project;
        project.dir = tmp.Sub("proj");
        fs::create_directories(project.dir);
        CHECK_FALSE(imblib::Import(project, nullptr, tmp.Sub("in/evil.imblib"), &err));
        CHECK_FALSE(fs::exists(tmp.path / "proj/libs"));
    }
}
