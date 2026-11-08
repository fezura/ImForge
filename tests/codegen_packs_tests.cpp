#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>
#include <vector>

#include "codegen.h"
#include "codegen_test_util.h"
#include "imforge/imforge_core.h"
#include "imforge/imforge_widgets.h"

using namespace imforge;
using namespace imforge::codegen;
using namespace codegen_test;

namespace {

const char* const kTypes = R"json({
  "schemaVersion": 1, "kind": "types",
  "structs": {
    "Data": { "header": "types_stub.h", "fields": [
      {"name":"b","type":"bool","ctype":"Bool"},
      {"name":"i","type":"int","ctype":"I32"},
      {"name":"f","type":"float","ctype":"Float"},
      {"name":"col","type":"ImVec4","ctype":"Vec4"},
      {"name":"pos","type":"ImVec2","ctype":"Vec2"},
      {"name":"name","type":"std::string","ctype":"StdString"},
      {"name":"icon","type":"ImTextureID","ctype":"TexID"},
      {"name":"hist","type":"float[16]","ctype":"FloatArray","count":16},
      {"name":"f2","type":"float[2]","ctype":"FloatArray","count":2},
      {"name":"f3","type":"float[3]","ctype":"FloatArray","count":3},
      {"name":"f4","type":"float[4]","ctype":"FloatArray","count":4},
      {"name":"i2","type":"int[2]","ctype":"IntArray","count":2},
      {"name":"keys","type":"int[4]","ctype":"IntArray","count":4},
      {"name":"stops","type":"ImVec4[4]","ctype":"Array","count":4},
      {"name":"samples","type":"std::vector<float>","ctype":"Vector","elem":"float"},
      {"name":"keys_v","type":"std::vector<int>","ctype":"Vector","elem":"int"},
      {"name":"names","type":"std::vector<std::string>","ctype":"Vector","elem":"std::string"}
    ] }
  },
  "enums": {}
})json";

Json Parse(const std::string& text) {
    Json j;
    JsonError e;
    REQUIRE_MESSAGE(ParseJson(text, j, &e), e.message << " at " << e.line << ":" << e.col);
    return j;
}

std::string Field(const std::string& type) {
    if (type == "bool") return "data.b";
    if (type == "int" || type == "enum") return "data.i";
    if (type == "float") return "data.f";
    if (type == "vec4") return "data.col";
    if (type == "vec2") return "data.pos";
    if (type == "string") return "data.name";
    if (type == "texture") return "data.icon";
    if (type == "float[N]") return "data.hist";
    if (type == "vector<float>") return "data.samples";
    if (type == "int[N]") return "data.keys";
    if (type == "vector<int>") return "data.keys_v";
    if (type == "vector<string>") return "data.names";
    if (type == "float[2]") return "data.f2";
    if (type == "float[3]") return "data.f3";
    if (type == "float[4]") return "data.f4";
    if (type == "int[2]") return "data.i2";
    if (type == "vec4[N]") return "data.stops";
    return std::string();
}

struct PackWidgets {
    std::vector<Json> manifests;
    std::vector<std::string> nodes;
};

// every widget once unbound, once with its bindings set and once more with the std::vector forms
// where a widget takes both
void LoadPack(const std::string& pack, PackWidgets& out) {
    std::string dir = RepoRoot() + "/packs/" + pack + "/";
    Json manifest;
    REQUIRE(LoadJsonFile(dir + "manifest.json", manifest));
    for (size_t i = 0; i < manifest["widgets"].Size(); ++i) {
        Json w;
        REQUIRE_MESSAGE(LoadJsonFile(dir + manifest["widgets"][i].AsString(), w), manifest["widgets"][i].AsString());
        out.manifests.push_back(w);
        std::string type = w["name"].AsString();
        std::string tag = pack + "_" + type;
        out.nodes.push_back("{\"id\":\"" + tag + "_u\",\"type\":\"" + type + "\"}");
        for (int variant = 0; variant < 2; ++variant) {
            std::string bind;
            bool vectors = false;
            for (size_t b = 0; b < w["bindings"].Size(); ++b) {
                const Json& bd = w["bindings"][b];
                std::string t = bd["types"][0].AsString();
                if (variant == 1) {
                    for (size_t k = 0; k < bd["types"].Size(); ++k) {
                        if (bd["types"][k].AsString().compare(0, 6, "vector") == 0) {
                            t = bd["types"][k].AsString();
                            vectors = true;
                        }
                    }
                }
                std::string f = Field(t);
                if (f.empty()) continue;
                if (!bind.empty()) bind += ",";
                bind += "\"" + bd["name"].AsString() + "\":\"" + f + "\"";
            }
            if ((variant == 1 && !vectors) || bind.empty()) continue;
            out.nodes.push_back("{\"id\":\"" + tag + (variant ? "_v" : "_b") + "\",\"type\":\"" + type + "\",\"bind\":{" + bind +
                                "}}");
        }
    }
}

struct Generated {
    Result result;
    std::vector<FileOut> stubs;
    size_t widgets = 0;
};

Generated Generate(const std::vector<std::string>& packs, int imgui_version, int cpp_standard) {
    static Json types = Parse(kTypes);
    static WidgetCatalog catalog;
    catalog = WidgetCatalog();
    std::string nodes;
    size_t widgets = 0;
    for (const std::string& pack : packs) {
        PackWidgets pw;
        LoadPack(pack, pw);
        for (const Json& m : pw.manifests) {
            Error err;
            REQUIRE_MESSAGE(catalog.AddManifest(m, &err), err.message);
            ++widgets;
        }
        for (const std::string& n : pw.nodes) nodes += (nodes.empty() ? "" : ",") + n;
    }
    std::string text = R"json({"schemaVersion":1,"kind":"panel","name":"Packs",
      "params":[{"name":"data","type":"Data"}],
      "root":{"id":"window","type":"Window","props":{"title":"Packs"},"children":[)json" +
                       nodes + "]}}";
    Panel panel;
    Error e;
    REQUIRE_MESSAGE(PanelFromJson(Parse(text), panel, &e), e.message);
    Project project;
    project.name = "Packs";
    project.imgui_version = Project::VersionString(imgui_version);
    project.features.emplace_back("animations", false);
    project.features.emplace_back("drawlistFx", false);
    project.features.emplace_back("shaderFx", false);
    project.features.emplace_back("iconFonts", false);
    project.binding_types = {"Data"};
    project.widget_catalog = &catalog;
    project.codegen.cpp_standard = cpp_standard;
    Options opts;
    opts.settings = project.codegen;
    opts.imgui_version = imgui_version;
    opts.features_animations = opts.features_fx = opts.features_icons = false;
    opts.types = &types;
    Generated g;
    g.result = GeneratePanel(project, panel, opts);
    g.widgets = widgets;
#ifndef _WIN32
    g.stubs = StubsFromTypes(types, "", {});
#endif
    return g;
}

void NoProblems(const Generated& g) {
    for (const Diagnostic& d : g.result.diagnostics) {
        CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.node_id << ": " << d.message);
        CHECK_MESSAGE(d.message.find("unknown placeholder") == std::string::npos, d.node_id << ": " << d.message);
        CHECK_MESSAGE(d.message.find("cannot receive") == std::string::npos, d.node_id << ": " << d.message);
        bool unbound_node = d.node_id.size() >= 2 && d.node_id.compare(d.node_id.size() - 2, 2, "_u") == 0;
        bool required_warning = d.message.find("is not bound") != std::string::npos;
        bool fine = unbound_node || !required_warning;
        CHECK_MESSAGE(fine, d.node_id << ": " << d.message);
    }
}

}  // namespace

TEST_CASE("generated code for the display, gaming and controls packs has no diagnostics") {
    Generated g = Generate({"display", "gaming", "controls"}, 19190, 17);
    CHECK(g.widgets == 20 + 16 + 27);
    NoProblems(g);
    std::string all;
    for (const FileOut& f : g.result.files) all += f.content;
    for (const char* inc : {"imb_display.h", "imb_gaming.h", "imb_controls.h"})
        CHECK_MESSAGE(all.find(inc) != std::string::npos, inc);
}

#ifndef _WIN32
TEST_CASE("pack widgets compile as C++14 and C++17 on 1.85, 1.91.9b and 1.93.0") {
    if (!HaveGxx()) {
        MESSAGE("no compiler, skipping");
        return;
    }
    std::string root = RepoRoot();
    std::string inc;
    for (const char* pack : {"display", "gaming", "controls", "essentials"}) inc += "-I'" + root + "/packs/" + pack + "' ";
    int ran = 0;
    for (int std_ver : {14, 17}) {
        for (const char* v : {"imgui-v1.85", "imgui-v1.91.9b", "imgui-v1.93.0"}) {
            std::string dir = DepsDir() + "/" + v;
            if (!DirExists(dir)) {
                MESSAGE(v << " not found");
                continue;
            }
            Generated g = Generate({"display", "gaming", "controls"}, 19190, std_ver);
            NoProblems(g);
            std::string flags = inc + "-std=c++" + std::to_string(std_ver) + " -fno-exceptions -fno-rtti";
            CompileResult(g.result, g.stubs, std::string("packs_") + v + "_" + std::to_string(std_ver), dir, "ui", flags);
            ++ran;
        }
    }
    if (ran == 0) MESSAGE("no pinned ImGui checkout found");
}
#endif
