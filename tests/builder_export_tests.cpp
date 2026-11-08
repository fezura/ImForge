#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "imforge/imforge_transport.h"
#include "codegen.h"
#include "roundtrip.h"

#include "../src/builder/prefs.cpp"
#include "../src/builder/project_io.cpp"
#include "../src/builder/live_server.cpp"
#include "../src/builder/imblib.cpp"
#include "../src/builder/editor_context.cpp"
#include "../src/builder/editor_commands.cpp"
#include "../src/builder/ui/tree_ops.cpp"
#include "../src/builder/panels/components_ops.cpp"
#include "../src/builder/panels/components_actions.cpp"
#include "../src/builder/bundled_packs.cpp"
#define Fail ExportFail
#include "../src/builder/export.cpp"
#undef Fail

#include "codegen_test_util.h"

namespace imforge::builder {
std::string ExeDir() { return std::filesystem::current_path().generic_string(); }
void RequestOverwriteConfirm(EditorContext&, const std::vector<std::string>&) {}
}  // namespace imforge::builder

using namespace imforge;
using namespace imforge::builder;

namespace {

namespace fs = std::filesystem;

std::string SourceDir() {
#ifdef IMFORGE_SOURCE_DIR
    return IMFORGE_SOURCE_DIR;
#else
    return ".";
#endif
}

std::string TempPath(const std::string& tag) {
#ifdef IMFORGE_BINARY_DIR
    fs::path p = fs::path(IMFORGE_BINARY_DIR) / "test_tmp" / ("builder_export_" + tag);
#else
    fs::path p = fs::temp_directory_path() / ("imforge_builder_export_" + tag);
#endif
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.generic_string();
}

// keep the user's prefs.json (recent projects) out of it
void UseTempConfigDir() {
    static std::string dir = TempPath("config");
#ifdef _WIN32
    _putenv_s("APPDATA", dir.c_str());
#else
    setenv("XDG_CONFIG_HOME", dir.c_str(), 1);
#endif
}

// copy of an example project with the live server off, so nothing listens on its port
std::string CopyExample(const std::string& name, const std::string& tag) {
    std::string dst = TempPath(tag) + "/" + name;
    std::error_code ec;
    fs::copy(fs::path(SourceDir()) / "examples" / "projects" / name, dst, fs::copy_options::recursive, ec);
    REQUIRE_MESSAGE(!ec, ec.message());
    fs::remove_all(fs::path(dst) / "generated", ec);
    Json j;
    Error err;
    REQUIRE_MESSAGE(ReadJsonFile(dst + "/imforge.json", j, &err), err.message);
    Json live = j["live"];
    live.Set("enabled", Json(false));
    j.Set("live", live);
    REQUIRE(WriteTextFile(dst + "/imforge.json", DumpJson(j) + "\n", &err));
    return dst;
}

std::vector<std::string> FilesUnder(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) out.push_back(fs::relative(it->path(), dir).generic_string());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> QuotedIncludes(const std::string& text) {
    std::vector<std::string> out;
    size_t at = 0;
    while ((at = text.find("#include \"", at)) != std::string::npos) {
        size_t start = at + 10, end = text.find('"', start);
        if (end == std::string::npos) break;
        out.push_back(text.substr(start, end - start));
        at = end;
    }
    return out;
}

struct Exported {
    std::string project_dir, out_dir;
    Json types;
    int cpp_standard = 17;
};

Exported ExportExample(const std::string& name, int cpp_standard) {
    UseTempConfigDir();
    Exported e;
    e.cpp_standard = cpp_standard;
    e.project_dir = CopyExample(name, name + "_" + std::to_string(cpp_standard));
    EditorContext ctx;
    InitBundledPacks(ctx);
    std::string err;
    REQUIRE_MESSAGE(ctx.LoadProjectDir(e.project_dir, &err), err);
    CHECK_FALSE(ctx.live.IsRunning());
    ctx.project.codegen.cpp_standard = cpp_standard;
    REQUIRE_MESSAGE(ExportProject(ctx, &err, true), err);
    for (const Diagnostic& d : ctx.diagnostics)
        CHECK_MESSAGE(d.level != Diagnostic::Level::Error, name << ": " << d.panel << "/" << d.node_id << ": " << d.message);
    CHECK(ctx.conflicts.empty());
    e.out_dir = ExportOutputDir(ctx.project);
    Error jerr;
    REQUIRE_MESSAGE(ReadJsonFile(JoinPath(e.project_dir, ctx.project.types_json), e.types, &jerr), jerr.message);
    return e;
}

// dev-build files, these need the ImForge client headers on purpose
bool IsDevFile(const std::string& rel) { return rel == "imforge_reflect.h" || rel == "imforge_dev.h"; }

// headers the app brings: imgui, the icon font header and its own types
std::set<std::string> AppHeaders(const Exported& e) {
    std::set<std::string> out = {"imgui.h", "imgui_internal.h", "misc/cpp/imgui_stdlib.h", "IconsFontAwesome6.h"};
    for (const char* group : {"structs", "enums"})
        for (const auto& kv : e.types[group].obj) out.insert(kv.second["header"].AsString());
    return out;
}

void CheckSelfContained(const Exported& e) {
    std::set<std::string> app = AppHeaders(e);
    std::vector<std::string> files = FilesUnder(e.out_dir);
    REQUIRE(!files.empty());
    for (const std::string& rel : files) {
        if (IsDevFile(rel)) continue;
        std::string text;
        REQUIRE(ReadTextFile(JoinPath(e.out_dir, rel), text));
        CHECK_MESSAGE(text.find("#include \"imforge/") == std::string::npos, rel << " still includes from imforge/");
        for (const std::string& inc : QuotedIncludes(text)) {
            if (app.count(inc) || inc.compare(0, 10, "imgui_impl") == 0) continue;
            bool here = fs::exists(fs::path(e.out_dir) / fs::path(rel).parent_path() / inc);
            bool top = fs::exists(fs::path(e.out_dir) / inc);
            CHECK_MESSAGE((here || top), rel << " includes \"" << inc << "\", which the export does not ship");
        }
    }
}

#ifndef _WIN32
// g++ on every exported .cpp plus one TU with all the headers; include path is the export, the app
// stubs and imgui, nothing from this repo
void CompileStandalone(const Exported& e, const std::string& imgui_dir, const std::string& tag, const std::string& prelude) {
    using codegen_test::ReadFile;
    using codegen_test::WriteFile;
    std::string stubs = TempPath(tag + "_stubs");
    std::string icons = "#pragma once\n";
    std::set<std::string> seen;
    std::vector<std::string> units;
    std::string headers_tu;
    for (const std::string& rel : FilesUnder(e.out_dir)) {
        std::string text = ReadFile(JoinPath(e.out_dir, rel));
        for (size_t pos = 0; (pos = text.find("ICON_FA_", pos)) != std::string::npos;) {
            size_t end = pos;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) ++end;
            if (seen.insert(text.substr(pos, end - pos)).second) icons += "#define " + text.substr(pos, end - pos) + " \"\\xef\\x80\\x80\"\n";
            pos = end;
        }
        std::string ext = fs::path(rel).extension().generic_string();
        if (ext == ".cpp") units.push_back(JoinPath(e.out_dir, rel));
        // renderer backends (fx/imb_fx_dx11.h and co.) only build on their platform
        else if (ext == ".h" && !IsDevFile(rel) && rel.compare(0, 10, "fx/imb_fx_") != 0) headers_tu += "#include \"" + rel + "\"\n";
    }
    REQUIRE(WriteFile(stubs + "/IconsFontAwesome6.h", icons));
    for (const imforge::codegen::FileOut& f : codegen_test::StubsFromTypes(e.types, prelude, {})) {
        fs::path p = fs::path(stubs) / f.path;
        fs::create_directories(p.parent_path());
        REQUIRE(WriteFile(p.generic_string(), f.content));
    }
    REQUIRE(WriteFile(stubs + "/headers_tu.cpp", headers_tu + "int main() { return 0; }\n"));
    units.push_back(stubs + "/headers_tu.cpp");
    std::string gxx = "g++ -std=c++" + std::to_string(e.cpp_standard) +
                      " -O2 -Wall -Wextra -Werror -c -o /dev/null -I'" + e.out_dir + "' -I'" + stubs + "' -I'" + imgui_dir + "' ";
    std::string script;
    for (size_t i = 0; i < units.size(); ++i) {
        std::string log = stubs + "/" + std::to_string(i);
        script += "(" + gxx + "'" + units[i] + "' >'" + log + ".err' 2>&1; echo $? >'" + log + ".rc') & ";
        if (i % 3 == 2) script += "wait; ";
    }
    script += "wait";
    REQUIRE(std::system(script.c_str()) == 0);
    for (size_t i = 0; i < units.size(); ++i) {
        std::string log = stubs + "/" + std::to_string(i);
        int rc = std::atoi(ReadFile(log + ".rc").c_str());
        std::string err = ReadFile(log + ".err");
        CHECK_MESSAGE(rc == 0, fs::path(units[i]).filename().generic_string() << " [" << tag << "]:\n" << err);
        CHECK_MESSAGE(err.find("warning:") == std::string::npos, fs::path(units[i]).filename().generic_string() << " [" << tag << "]:\n" << err);
    }
}
#endif

}  // namespace

TEST_CASE("exported examples only include what the export ships") {
    for (const char* name : {"Showcase", "CheatMenu"}) {
        CAPTURE(name);
        Exported e = ExportExample(name, 14);
        CheckSelfContained(e);
    }
}

#ifndef _WIN32
TEST_CASE("exported Showcase and CheatMenu build as C++14 with -Werror on 1.85 and 1.91.9b") {
    if (!codegen_test::HaveGxx()) {
        MESSAGE("no g++, skipping");
        return;
    }
    int ran = 0;
    // CheatMenu has a locked block that calls into the app
    const std::pair<const char*, const char*> examples[] = {
        {"Showcase", ""}, {"CheatMenu", "int GetPing();\nconst char* GetServerName();\n"}};
    for (const auto& ex : examples) {
        const char* name = ex.first;
        Exported e = ExportExample(name, 14);
        for (const char* v : {"imgui-v1.85", "imgui-v1.91.9b"}) {
            std::string dir = codegen_test::DepsDir() + "/" + v;
            if (!codegen_test::DirExists(dir)) {
                MESSAGE(v << " not found");
                continue;
            }
            CompileStandalone(e, dir, std::string(name) + "_" + v, ex.second);
            ++ran;
        }
    }
    if (ran == 0) MESSAGE("no pinned ImGui checkout found");
}
#endif

TEST_CASE("self-check export loads the bundled packs") {
    UseTempConfigDir();
    std::string proj = CopyExample("Showcase", "selfcheck");
    std::string out = proj + "_out";
    std::string report;
    CHECK_MESSAGE(ExportSelfCheck(proj, out, report), report);
    int pack_calls = 0;
    for (const std::string& rel : FilesUnder(out)) {
        if (fs::path(rel).extension() != ".cpp") continue;
        std::string text;
        REQUIRE(ReadTextFile(JoinPath(out, rel), text));
        CHECK_MESSAGE(text.find("ImGui::TextDisabled(\"[") == std::string::npos, rel << " has placeholder widgets");
        for (const char* ns : {"imb::disp::", "imb::game::", "imb::ctl::", "imb::ess::"})
            for (size_t at = text.find(ns); at != std::string::npos; at = text.find(ns, at + 1)) ++pack_calls;
    }
    CHECK(pack_calls > 0);
}
