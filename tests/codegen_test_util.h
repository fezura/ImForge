#pragma once

// codegen_tests.cpp helpers: types.json stubs and g++ -fsyntax-only runs against pinned ImGui checkouts

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "codegen.h"
#include "doctest.h"
#include "imforge/imforge_core.h"

namespace codegen_test {

using imforge::Json;
using imforge::codegen::FileOut;
using imforge::codegen::Result;

inline bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

inline std::string ReadFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

inline bool WriteFile(const std::string& path, const std::string& content) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return true;
}

inline std::string RepoRoot() {
    std::string f = __FILE__;
    size_t p = f.rfind("/tests/");
    if (p == std::string::npos) p = f.rfind("\\tests\\");
    return p == std::string::npos ? std::string(".") : f.substr(0, p);
}

inline bool LoadJsonFile(const std::string& path, Json& out) {
    std::string text = ReadFile(path);
    return !text.empty() && imforge::ParseJson(text, out);
}

inline bool LoadProjectDir(const std::string& dir, imforge::Project& p) {
    Json j;
    imforge::Error err;
    if (!LoadJsonFile(dir + "/imforge.json", j) || !imforge::ProjectFromJson(j, p, &err)) return false;
    p.dir = dir;
    for (const std::string& f : p.panel_files) {
        imforge::Panel panel;
        if (!LoadJsonFile(dir + "/" + f, j) || !imforge::PanelFromJson(j, panel, &err)) return false;
        p.panels.push_back(panel);
    }
    for (const std::string& f : p.component_files) {
        imforge::Component c;
        if (!LoadJsonFile(dir + "/" + f, j) || !imforge::ComponentFromJson(j, c, &err)) return false;
        p.components.push_back(c);
    }
    for (const std::string& f : p.theme_files) {
        imforge::Theme t;
        if (!LoadJsonFile(dir + "/" + f, j) || !imforge::ThemeFromJson(j, t, &err)) return false;
        p.themes.push_back(t);
    }
    return true;
}

#ifndef _WIN32
inline bool HaveGxx() { return std::system("g++ --version >/dev/null 2>&1") == 0; }
inline bool DirExists(const std::string& p) { return std::system(("test -d '" + p + "'").c_str()) == 0; }
inline std::string TempRoot() { return std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp"; }
inline std::string DepsDir() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/deps";
}

// pinned checkouts found on this machine, oldest first
inline std::vector<std::string> ImGuiCheckouts() {
    static const char* const kAll[] = {"imgui-v1.85",     "imgui-v1.87",         "imgui-v1.89.9",  "imgui-v1.91.9b",
                                       "imgui-v1.91.9b-docking", "imgui-v1.92.1", "imgui-v1.93.0", "imgui-v1.93.0-docking"};
    std::vector<std::string> out;
    for (const char* d : kAll)
        if (DirExists(DepsDir() + "/" + d)) out.push_back(DepsDir() + "/" + d);
    return out;
}

// g++ -fsyntax-only -Wall -Wextra on every .cpp plus one TU that includes all generated headers.
// imforge_dev.h only warns, it needs imforge_client.h which lives in another module
inline void CompileResult(const Result& r, const std::vector<FileOut>& stubs, const std::string& tag,
                          const std::string& imgui_dir, const std::string& ns, const std::string& extra_flags = std::string()) {
    std::string dir = TempRoot() + "/imforge_codegen_" + tag;
    REQUIRE(std::system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "'").c_str()) == 0);
    std::string icons = "#pragma once\n";
    std::vector<std::string> seen;
    std::string headers_tu;
    std::vector<std::string> units;
    for (const FileOut& f : r.files) {
        REQUIRE(WriteFile(dir + "/" + f.path, f.content));
        size_t pos = 0;
        while ((pos = f.content.find("ICON_FA_", pos)) != std::string::npos) {
            size_t e = pos;
            while (e < f.content.size() && (std::isalnum(static_cast<unsigned char>(f.content[e])) || f.content[e] == '_')) ++e;
            std::string name = f.content.substr(pos, e - pos);
            bool have = false;
            for (const std::string& x : seen) have = have || x == name;
            if (!have) {
                seen.push_back(name);
                icons += "#define " + name + " \"\\xef\\x80\\x80\"\n";
            }
            pos = e;
        }
        bool is_cpp = f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".cpp") == 0;
        if (is_cpp) units.push_back(f.path);
        else if (f.path != "imforge_dev.h") headers_tu += "#include \"" + f.path + "\"\n";
    }
    REQUIRE(WriteFile(dir + "/IconsFontAwesome6.h", icons));
    for (const FileOut& f : stubs) {
        std::string path = dir + "/" + f.path;
        size_t slash = path.rfind('/');
        REQUIRE(std::system(("mkdir -p '" + path.substr(0, slash) + "'").c_str()) == 0);
        REQUIRE(WriteFile(path, f.content));
    }
    headers_tu += "int main() { return 0; }\n";
    REQUIRE(WriteFile(dir + "/headers_tu.cpp", headers_tu));
    units.push_back("headers_tu.cpp");
    if (r.Find("imforge_dev.h")) {
        REQUIRE(WriteFile(dir + "/dev_tu.cpp",
                          "#include \"imforge_dev.h\"\nint main() { " + (ns.empty() ? std::string() : ns + "::") + "ImForgeRegisterAll(); }\n"));
        units.push_back("dev_tu.cpp");
    }
    std::string root = RepoRoot();
    std::string gxx = "g++ -std=c++17 -fsyntax-only -Wall -Wextra -I'" + dir + "' -I'" + root + "/include' -I'" + root +
                      "/include/imforge' -I'" + imgui_dir + "' " + extra_flags + " ";
    std::string script;
    for (const std::string& u : units)
        script += "(" + gxx + "'" + dir + "/" + u + "' 2>'" + dir + "/" + u + ".err'; echo $? >'" + dir + "/" + u + ".rc') & ";
    script += "wait";
    REQUIRE(std::system(script.c_str()) == 0);
    for (const std::string& u : units) {
        int rc = std::atoi(ReadFile(dir + "/" + u + ".rc").c_str());
        std::string log = ReadFile(dir + "/" + u + ".err");
        if (u == "dev_tu.cpp") {
            WARN_MESSAGE(rc == 0, "imforge_dev.h did not compile against imforge_client.h [" << tag << "]:\n" << log);
            continue;
        }
        CHECK_MESSAGE(rc == 0, "compile failed for " << u << " [" << tag << "]:\n" << log);
        CHECK_MESSAGE(!Contains(log, "warning:"), "warnings in generated " << u << " [" << tag << "]:\n" << log);
    }
}

// one shared stub file for the types.json types (`prelude` first, `members[struct]` go inside the
// struct); every listed `header` just includes it
inline std::vector<FileOut> StubsFromTypes(const Json& types, const std::string& prelude,
                                           const std::vector<std::pair<std::string, std::string>>& members) {
    std::string defs = "#pragma once\n#include \"imgui.h\"\n#include <string>\n#include <vector>\n" + prelude;
    std::vector<std::string> headers;
    auto add_header = [&](const std::string& h) {
        if (h.empty()) return;
        for (const std::string& x : headers)
            if (x == h) return;
        headers.push_back(h);
    };
    for (const auto& e : types["enums"].obj) {
        add_header(e.second["header"].AsString());
        defs += std::string(e.second["scoped"].AsBool(true) ? "enum class " : "enum ") + e.first + " { ";
        for (size_t i = 0; i < e.second["values"].Size(); ++i)
            defs += e.second["values"][i][0].AsString() + " = " + std::to_string(e.second["values"][i][1].AsInt()) + ", ";
        defs += "};\n";
    }
    std::vector<std::string> done;
    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto& st : types["structs"].obj) {
            bool emitted = false;
            for (const std::string& d : done) emitted = emitted || d == st.first;
            if (emitted) continue;
            bool ready = true;
            for (size_t i = 0; i < st.second["fields"].Size(); ++i) {
                const Json& f = st.second["fields"][i];
                if (f["ctype"].AsString() != "Struct") continue;
                bool have = false;
                for (const std::string& d : done) have = have || d == f["type"].AsString();
                ready = ready && have;
            }
            if (!ready) continue;
            add_header(st.second["header"].AsString());
            defs += "struct " + st.first + " {\n";
            for (size_t i = 0; i < st.second["fields"].Size(); ++i) {
                const Json& f = st.second["fields"][i];
                std::string type = f["type"].AsString();
                std::string name = f["name"].AsString();
                size_t br = type.find('[');
                if (br != std::string::npos) defs += "    " + type.substr(0, br) + " " + name + type.substr(br) + "{};\n";
                else defs += "    " + type + " " + name + "{};\n";
            }
            for (const auto& m : members)
                if (m.first == st.first) defs += "    " + m.second + "\n";
            defs += "};\n";
            done.push_back(st.first);
            progress = true;
        }
    }
    std::vector<FileOut> out;
    out.push_back({"imforge_test_types.h", defs});
    for (const std::string& h : headers) out.push_back({h, "#pragma once\n#include \"imforge_test_types.h\"\n"});
    return out;
}
#endif

}  // namespace codegen_test
