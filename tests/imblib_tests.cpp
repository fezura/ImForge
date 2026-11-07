#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imforge/imforge_core.h"

#include "../src/builder/imblib.cpp"

using namespace imforge;
using namespace imforge::builder;

namespace {

namespace fs = std::filesystem;

const char* kSliderJson = R"json({
  "schemaVersion": 1, "kind": "component", "name": "LabeledSlider",
  "params": [ { "name": "label", "type": "string", "default": "Value" },
              { "name": "value", "type": "binding<float>" },
              { "name": "min", "type": "float", "default": 0 }, { "name": "max", "type": "float", "default": 1 } ],
  "slots": [ "content" ],
  "root": { "id": "row", "type": "HStack", "children": [
     { "id": "lbl", "type": "Text", "props": { "text": { "param": "label" } } },
     { "id": "sl",  "type": "SliderFloat", "props": { "label": "##v", "min": { "param": "min" }, "max": { "param": "max" } },
       "bind": { "value": { "param": "value" } } },
     { "id": "slot_content", "type": "Slot", "props": { "name": "content" } } ] }
})json";

const char* kCardJson = R"json({
  "schemaVersion": 1, "kind": "component", "name": "Card",
  "params": [ { "name": "title", "type": "string", "default": "Card" }, { "name": "hp", "type": "binding<float>" } ],
  "slots": [ "body" ],
  "root": { "id": "card", "type": "VStack", "children": [
     { "id": "hdr", "type": "Text", "props": { "text": { "param": "title" } } },
     { "id": "inner", "type": "Component", "component": "LabeledSlider", "props": { "label": "HP", "value": { "param": "hp" } } },
     { "id": "slot_body", "type": "Slot", "props": { "name": "body" } } ] }
})json";

const char* kToggleJson = R"json({
  "schemaVersion": 1, "kind": "widget", "name": "Toggle", "category": "Essentials",
  "include": "imb_essentials.h", "minImGuiVersion": 18500,
  "props": [ { "name": "label", "type": "string", "default": "Toggle" },
             { "name": "size", "type": "size", "default": {"w":"auto","h":"auto"} },
             { "name": "onColor", "type": "color", "default": "#2ecc71ff" } ],
  "bindings": [ { "name": "value", "types": ["bool"], "required": true } ],
  "events": [ "onChange" ],
  "container": false,
  "call": "imb::ess::Toggle({label}, {value}, {size}, {onColor})",
  "returns": "changed"
})json";

const char* kHeaderText = "#pragma once\nnamespace imb::ess { inline bool Toggle() { return false; } }\n";

Json P(const char* text) {
    Json j;
    JsonError e;
    REQUIRE_MESSAGE(ParseJson(text, j, &e), e.message);
    return j;
}

Component C(const char* text) {
    Component c;
    Error e;
    REQUIRE_MESSAGE(ComponentFromJson(P(text), c, &e), e.message);
    return c;
}

std::string Slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void Spit(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary);
    out << text;
}

struct TempDir {
    fs::path path;
    TempDir() {
        auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("imforge_imblib_" + std::to_string((long long)ticks));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string Sub(const char* name) const { return (path / name).generic_string(); }
};

struct Source {
    Project project;
    WidgetCatalog catalog;
    explicit Source(const std::string& dir) {
        project.name = "Demo";
        project.dir = dir;
        project.components.push_back(C(kSliderJson));
        project.components.push_back(C(kCardJson));
        project.widget_catalog = &catalog;
        Error e;
        REQUIRE_MESSAGE(catalog.AddManifest(P(kToggleJson), &e), e.message);
        Spit(fs::path(dir) / "include" / "imb_essentials.h", kHeaderText);
    }
    ImblibExportOptions Opts() const {
        ImblibExportOptions o;
        o.name = "kit";
        o.version = "1.2.0";
        o.component_names = {"Card", "LabeledSlider"};
        o.widget_types = {"Toggle"};
        return o;
    }
};

}  // namespace

TEST_CASE("imblib export + import round-trips components, widgets and headers") {
    TempDir tmp;
    Source src(tmp.Sub("src"));
    std::string lib = tmp.Sub("kit.imblib");
    std::string err;
    std::vector<std::string> warnings;
    REQUIRE_MESSAGE(imblib::Export(src.project, &src.catalog, src.Opts(), lib, &err, &warnings), err);
    CHECK(warnings.empty());
    CHECK(fs::is_regular_file(lib));

    LibraryInfo info;
    REQUIRE_MESSAGE(imblib::ReadLibraryInfo(lib, info), info.error);
    CHECK(info.name == "kit");
    CHECK(info.version == "1.2.0");
    CHECK(info.min_imgui_version == 18500);
    CHECK(info.components == std::vector<std::string>{"Card", "LabeledSlider"});
    CHECK(info.widgets == std::vector<std::string>{"Toggle"});
    CHECK(info.headers == std::vector<std::string>{"imb_essentials.h"});

    Project dst;
    dst.dir = tmp.Sub("dst");
    WidgetCatalog cat;
    dst.widget_catalog = &cat;
    ImblibImportResult res;
    REQUIRE_MESSAGE(imblib::Import(dst, &cat, lib, &err, &res), err);
    CHECK(res.library == "kit");
    CHECK(res.warnings.empty());
    REQUIRE(dst.components.size() == 2);
    for (const Component& orig : src.project.components) {
        const Component* got = dst.FindComponent(orig.name);
        REQUIRE_MESSAGE(got != nullptr, orig.name);
        CHECK(DumpJson(ComponentToJson(*got)) == DumpJson(ComponentToJson(orig)));
        CHECK(got->file == "components/" + orig.name + ".imc.json");
        CHECK(fs::is_regular_file(fs::path(dst.dir) / got->file));
        CHECK(Slurp(fs::path(dst.dir) / got->file) == DumpJson(ComponentToJson(orig)) + "\n");
    }
    const CustomWidgetDef* toggle = cat.FindCustom("Toggle");
    REQUIRE(toggle != nullptr);
    CHECK(DumpJson(toggle->manifest) == DumpJson(P(kToggleJson)));
    CHECK(toggle->include == "imb_essentials.h");
    CHECK(Slurp(fs::path(dst.dir) / "libs/kit/imb_essentials.h") == kHeaderText);
    CHECK(fs::is_regular_file(fs::path(dst.dir) / "libs/kit/widgets/Toggle.imw.json"));
    CHECK(fs::is_regular_file(fs::path(dst.dir) / "libs/kit.imblib"));
    CHECK(dst.libraries == std::vector<std::string>{"libs/kit.imblib"});

    std::vector<LibraryInfo> libs = ListLibraries(dst);
    REQUIRE(libs.size() == 1);
    CHECK(libs[0].valid);
    CHECK(libs[0].name == "kit");
    CHECK(libs[0].file == "libs/kit.imblib");
    CHECK(libs[0].components.size() == 2);

    SUBCASE("re-importing the same library is idempotent") {
        REQUIRE_MESSAGE(imblib::Import(dst, &cat, lib, &err, &res), err);
        CHECK(dst.components.size() == 2);
        CHECK(dst.libraries.size() == 1);
        CHECK(res.warnings.size() == 2);
    }
}

TEST_CASE("imblib archives are deterministic") {
    TempDir tmp;
    Source src(tmp.Sub("src"));
    std::string a, b, err;
    REQUIRE_MESSAGE(imblib::BuildArchive(src.project, &src.catalog, src.Opts(), a, &err), err);
    ImblibExportOptions swapped = src.Opts();
    std::swap(swapped.component_names[0], swapped.component_names[1]);
    swapped.component_names.push_back("Card");
    REQUIRE_MESSAGE(imblib::BuildArchive(src.project, &src.catalog, swapped, b, &err), err);
    CHECK(a == b);
    CHECK(a.size() > 100);
    std::string prev_name;
    mz_zip_archive r;
    std::memset(&r, 0, sizeof(r));
    REQUIRE(mz_zip_reader_init_mem(&r, a.data(), a.size(), 0));
    mz_uint n = mz_zip_reader_get_num_files(&r);
    CHECK(n == 5);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st = {};
        REQUIRE(mz_zip_reader_file_stat(&r, i, &st));
        const unsigned char* local = (const unsigned char*)a.data() + st.m_local_header_ofs + 10;
        const unsigned char* central = (const unsigned char*)a.data() + r.m_central_directory_file_ofs + st.m_central_dir_ofs + 12;
        INFO("entry ", std::string(st.m_filename));
        CHECK((local[0] == 0 && local[1] == 0 && local[2] == 0x21 && local[3] == 0));
        CHECK((central[0] == 0 && central[1] == 0 && central[2] == 0x21 && central[3] == 0));
        if (i > 0) CHECK(std::string(st.m_filename) > prev_name);
        prev_name = st.m_filename;
    }
    mz_zip_reader_end(&r);
    std::string out = tmp.Sub("det.imblib");
    REQUIRE(imblib::Export(src.project, &src.catalog, src.Opts(), out, &err));
    CHECK(Slurp(out) == a);
}

TEST_CASE("imblib import namespaces clashing components and rewrites nested references") {
    TempDir tmp;
    Source src(tmp.Sub("src"));
    std::string lib = tmp.Sub("kit.imblib");
    std::string err;
    REQUIRE_MESSAGE(imblib::Export(src.project, &src.catalog, src.Opts(), lib, &err), err);

    Project dst;
    dst.dir = tmp.Sub("dst");
    Component local = C(kSliderJson);
    local.params.push_back(Param{"extra", "int", Json(3)});
    dst.components.push_back(local);
    WidgetCatalog cat;
    ImblibImportResult res;
    REQUIRE_MESSAGE(imblib::Import(dst, &cat, lib, &err, &res), err);
    REQUIRE(dst.components.size() == 3);
    CHECK(dst.FindComponent("LabeledSlider")->params.size() == 5);
    const Component* ns = dst.FindComponent("kit.LabeledSlider");
    REQUIRE(ns != nullptr);
    CHECK(ns->params.size() == 4);
    CHECK(ns->file == "components/kit.LabeledSlider.imc.json");
    CHECK(fs::is_regular_file(fs::path(dst.dir) / ns->file));
    const Component* card = dst.FindComponent("Card");
    REQUIRE(card != nullptr);
    const Node* inner = FindNode(card->root, "inner");
    REQUIRE(inner != nullptr);
    CHECK(inner->component == "kit.LabeledSlider");
    CHECK(res.components == std::vector<std::string>{"Card", "kit.LabeledSlider"});
    REQUIRE(res.warnings.size() == 1);
    CHECK(res.warnings[0].find("kit.LabeledSlider") != std::string::npos);
}

TEST_CASE("imblib import without a project directory registers in memory only") {
    TempDir tmp;
    Source src(tmp.Sub("src"));
    std::string lib = tmp.Sub("kit.imblib");
    std::string err;
    REQUIRE(imblib::Export(src.project, &src.catalog, src.Opts(), lib, &err));
    Project mem;
    WidgetCatalog cat;
    REQUIRE_MESSAGE(imblib::Import(mem, &cat, lib, &err), err);
    CHECK(mem.components.size() == 2);
    CHECK(cat.custom.size() == 1);
    CHECK(mem.libraries == std::vector<std::string>{"libs/kit.imblib"});
    CHECK_FALSE(fs::exists(tmp.path / "components"));
}

TEST_CASE("imblib reports errors") {
    TempDir tmp;
    Source src(tmp.Sub("src"));
    std::string err, bytes;
    ImblibExportOptions bad = src.Opts();
    bad.component_names.push_back("Nope");
    CHECK_FALSE(imblib::BuildArchive(src.project, &src.catalog, bad, bytes, &err));
    CHECK(err.find("Nope") != std::string::npos);
    bad = src.Opts();
    bad.widget_types.push_back("Missing");
    CHECK_FALSE(imblib::BuildArchive(src.project, &src.catalog, bad, bytes, &err));
    CHECK(err.find("Missing") != std::string::npos);
    Project p;
    WidgetCatalog cat;
    CHECK_FALSE(imblib::Import(p, &cat, tmp.Sub("does_not_exist.imblib"), &err));
    CHECK_FALSE(err.empty());
    Spit(tmp.path / "junk.imblib", "not a zip");
    CHECK_FALSE(imblib::Import(p, &cat, tmp.Sub("junk.imblib"), &err));
    LibraryInfo info;
    CHECK_FALSE(imblib::ReadLibraryInfo(tmp.Sub("junk.imblib"), info));
    CHECK_FALSE(info.valid);
    std::vector<LibraryInfo> libs = ListLibraries(p);
    CHECK(libs.empty());
    p.libraries.push_back("libs/junk.imblib");
    p.dir = tmp.path.generic_string();
    libs = ListLibraries(p);
    REQUIRE(libs.size() == 1);
    CHECK_FALSE(libs[0].valid);
    CHECK(libs[0].name == "junk");
}

TEST_CASE("imblib header lookup falls back gracefully") {
    TempDir tmp;
    Source src(tmp.Sub("src"));
    fs::remove(fs::path(src.project.dir) / "include" / "imb_essentials.h");
    std::string bytes, err;
    std::vector<std::string> warnings;
    REQUIRE_MESSAGE(imblib::BuildArchive(src.project, &src.catalog, src.Opts(), bytes, &err, &warnings), err);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("imb_essentials.h") != std::string::npos);
    std::string out = tmp.Sub("nohdr.imblib");
    REQUIRE(imblib::Export(src.project, &src.catalog, src.Opts(), out, &err));
    LibraryInfo info;
    REQUIRE(imblib::ReadLibraryInfo(out, info));
    CHECK(info.headers.empty());
    CHECK(info.widgets == std::vector<std::string>{"Toggle"});
}
