#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "codegen.h"
#include "imforge/imb_motion.h"
#include "imforge/imforge_core.h"
#include "imforge/imforge_motion_json.h"
#include "imforge/imforge_protocol.h"
#include "imforge/imforge_widgets.h"

using namespace imforge;
using namespace imforge::codegen;

namespace {

Json J(const std::string& text) {
    Json j;
    JsonError e;
    REQUIRE_MESSAGE(ParseJson(text, j, &e), e.message);
    return j;
}

bool HasLevel(const Result& r, Diagnostic::Level level, const std::string& needle) {
    for (const Diagnostic& d : r.diagnostics)
        if (d.level == level && d.message.find(needle) != std::string::npos) return true;
    return false;
}

std::string Manifest(const std::string& include, const std::string& call, const std::string& call_end = std::string()) {
    std::string s = R"({"kind":"widget","name":"Evil","include":")" + include + R"(","call":")" + call + "\"";
    if (!call_end.empty()) s += R"(,"callEnd":")" + call_end + "\"";
    return s + "}";
}

}  // namespace

TEST_CASE("include names allow plain headers only") {
    CHECK(IsSafeIncludeName("imb_gaming.h"));
    CHECK(IsSafeIncludeName("sub/dir/my-widget.hpp"));
    CHECK_FALSE(IsSafeIncludeName(""));
    CHECK_FALSE(IsSafeIncludeName("../evil.h"));
    CHECK_FALSE(IsSafeIncludeName("a/../b.h"));
    CHECK_FALSE(IsSafeIncludeName("/etc/passwd.h"));
    CHECK_FALSE(IsSafeIncludeName("C:/x.h"));
    CHECK_FALSE(IsSafeIncludeName("a\\b.h"));
    CHECK_FALSE(IsSafeIncludeName("a\".h"));
    CHECK_FALSE(IsSafeIncludeName("a.h\n#define X"));
    CHECK_FALSE(IsSafeIncludeName("<vector>"));
    CHECK_FALSE(IsSafeIncludeName("noext"));
    CHECK_FALSE(IsSafeIncludeName("x.cpp"));
}

TEST_CASE("call templates are one call with placeholders") {
    CHECK(IsSafeCallTemplate("imb::ctl::Knob({label}, {value}, {min}, {max}, {format})"));
    CHECK(IsSafeCallTemplate("Knob({label}, {items:array}, {items:count})"));
    CHECK(IsSafeCallTemplate("imb::disp::LoadingBar({label}, {indeterminate} ? -1.0f : (float)({fraction}), {size})"));
    CHECK(IsSafeCallTemplate("imb::ess::EndSectionCard()"));
    CHECK_FALSE(IsSafeCallTemplate(""));
    CHECK_FALSE(IsSafeCallTemplate("f(); system(\"x\")"));
    CHECK_FALSE(IsSafeCallTemplate("f(1)\nsystem(\"x\")"));
    CHECK_FALSE(IsSafeCallTemplate("f([&]() { evil(); })"));
    CHECK_FALSE(IsSafeCallTemplate("f(1) // trailing"));
    CHECK_FALSE(IsSafeCallTemplate("f(/* x */ 1)"));
    CHECK_FALSE(IsSafeCallTemplate("f(1)\n#include <x>"));
    CHECK_FALSE(IsSafeCallTemplate("f(`x`)"));
    CHECK_FALSE(IsSafeCallTemplate("f(1), g(2)"));
    CHECK_FALSE(IsSafeCallTemplate("f(1)) + (g(2)"));
    CHECK_FALSE(IsSafeCallTemplate("f(\"a\\\"b\")"));
    CHECK_FALSE(IsSafeCallTemplate("f(?" "?/)"));
    CHECK_FALSE(IsSafeCallTemplate("f({})"));
    CHECK_FALSE(IsSafeCallTemplate("f({a b})"));
    CHECK_FALSE(IsSafeCallTemplate("1 + f()"));
    CHECK_FALSE(IsSafeCallTemplate("f"));
}

TEST_CASE("manifests with unsafe include or call are refused") {
    WidgetCatalog catalog;
    Error err;
    CHECK(catalog.AddManifest(J(Manifest("imb_ok.h", "ns::Ok({label})")), &err));
    WidgetCatalog c2;
    CHECK_FALSE(c2.AddManifest(J(Manifest("../../evil.h", "ns::Ok({label})")), &err));
    CHECK(err.message.find("include") != std::string::npos);
    CHECK_FALSE(c2.AddManifest(J(Manifest("ok.h", "ns::Ok(); system(\\\"x\\\")")), &err));
    CHECK(err.message.find("call") != std::string::npos);
    CHECK_FALSE(c2.AddManifest(J(Manifest("ok.h", "ns::Ok({label})", "End(); evil()")), &err));
    CHECK(err.message.find("callEnd") != std::string::npos);
    CHECK_FALSE(c2.AddManifest(J(Manifest("ok.h", "ns::Ok({label})\\n#define X 1")), &err));
}

TEST_CASE("every shipped pack manifest still passes") {
    namespace fs = std::filesystem;
    fs::path packs = fs::path(IMFORGE_SOURCE_DIR) / "packs";
    int seen = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(packs, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (name.size() < 10 || name.compare(name.size() - 9, 9, ".imw.json") != 0) continue;
        std::string text;
        {
            std::FILE* f = std::fopen(it->path().string().c_str(), "rb");
            REQUIRE(f);
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        WidgetCatalog catalog;
        Error err;
        INFO(name);
        CHECK_MESSAGE(catalog.AddManifest(J(text), &err), err.message);
        ++seen;
    }
    CHECK(seen > 20);
}

TEST_CASE("style classes that share a C++ name are an error") {
    Project p;
    p.name = "T";
    p.features.emplace_back("animations", false);
    StyleOverride a, b;
    p.style_classes.emplace_back("accent-button", a);
    p.style_classes.emplace_back("accent_button", b);
    p.style_classes.emplace_back("other", b);
    Options opts = OptionsFromProject(p);
    Result r = GenerateStyles(p, opts);
    CHECK(HasLevel(r, Diagnostic::Level::Error, "Style_accent_button"));
    std::string text = r.files.empty() ? std::string() : r.files[0].content;
    size_t first = text.find("inline void Style_accent_button_Push");
    REQUIRE(first != std::string::npos);
    CHECK(text.find("inline void Style_accent_button_Push", first + 1) == std::string::npos);
    CHECK(text.find("Style_other_Push") != std::string::npos);
}

TEST_CASE("namespace and runtime header from the project cannot carry code") {
    Project p;
    p.name = "T";
    p.codegen.nmspace = "ui { } int main() { system(\"x\"); } namespace z";
    p.codegen.runtime_header = "x.h\"\n#include \"/etc/passwd";
    Options opts = OptionsFromProject(p);
    CHECK(opts.settings.nmspace == "ui");
    CHECK(opts.settings.runtime_header == "imb_runtime.h");
    Result r = GenerateAll(p, opts);
    CHECK(HasLevel(r, Diagnostic::Level::Warning, "codegen.namespace"));
    p.codegen.nmspace = "game::ui";
    CHECK(OptionsFromProject(p).settings.nmspace == "game::ui");
}

TEST_CASE("panel and theme names cannot break out of the file marker comment") {
    Project p;
    p.name = "T";
    p.features.emplace_back("animations", false);
    Theme t;
    t.name = "Dark\nint evil = system(\"x\");\\";
    Options opts = OptionsFromProject(p);
    Result r = GenerateTheme(p, t, opts);
    REQUIRE(r.files.size() == 1);
    const std::string& text = r.files[0].content;
    size_t at = text.find("@imb:file");
    REQUIRE(at != std::string::npos);
    size_t eol = text.find('\n', at);
    std::string line = text.substr(at, eol - at);
    CHECK(line.find("system") != std::string::npos);
    CHECK(text.find("\nint evil") == std::string::npos);
    CHECK(line.find('\\') == std::string::npos);
}

TEST_CASE("unknown motion presets are skipped, not pasted into code") {
    Project p;
    p.name = "T";
    Panel panel;
    Error e;
    // kept out of the macro, msvc's preprocessor eats the // inside the raw string
    const char* json = R"({"schemaVersion":1,"kind":"panel","name":"Main","root":{"id":"window","type":"Window",
        "children":[{"id":"b","type":"Button","props":{"label":"x"},
        "anim":{"motions":[{"preset":"Fade());system(\"x\");//","trigger":"enter"}]}}]}})";
    REQUIRE_MESSAGE(PanelFromJson(J(json), panel, &e), e.message);
    Options opts = OptionsFromProject(p);
    Result r = GeneratePanel(p, panel, opts);
    std::string all;
    for (const FileOut& f : r.files) all += f.content;
    CHECK(all.find("system") == std::string::npos);
    CHECK(HasLevel(r, Diagnostic::Level::Warning, "unknown motion preset"));
}

TEST_CASE("generated animations only carry validated names") {
    Project p;
    p.name = "T";
    p.animations.push_back(J(R"({"name":"Good","category":"attention","duration":0.5,"tracks":{"opacity":[{"t":0,"v":1},{"t":1,"v":1}]},
        "description":"a\nint evil;"})"));
    p.animations.push_back(J(R"({"name":"Bad(){system(\"x\");}","category":"attention","duration":0.5,
        "tracks":{"opacity":[{"t":0,"v":1},{"t":1,"v":1}]}})"));
    Options opts = OptionsFromProject(p);
    Result r = GenerateAnimations(p, opts);
    REQUIRE(r.files.size() == 1);
    CHECK(r.files[0].content.find("system") == std::string::npos);
    CHECK(r.files[0].content.find("int evil") == std::string::npos);
    CHECK(r.files[0].content.find("Good") != std::string::npos);
    CHECK(HasLevel(r, Diagnostic::Level::Warning, "custom animation skipped"));
}

TEST_CASE("hostile animation numbers are refused") {
    auto parse = [](const std::string& duration, const std::string& extra = std::string()) {
        AnimStorage st;
        Error err;
        std::string text = R"({"name":"Probe","category":"attention","duration":)" + duration + extra +
                           R"(,"tracks":{"opacity":[{"t":0,"v":1},{"t":1,"v":1}]}})";
        Json j;
        JsonError je;
        if (!ParseJson(text, j, &je)) return false;
        return AnimFromJson(j, st, &err);
    };
    CHECK(parse("0.5"));
    CHECK_FALSE(parse("1e999"));
    CHECK_FALSE(parse("-1"));
    CHECK_FALSE(parse("0"));
    CHECK_FALSE(parse("61"));
    CHECK_FALSE(parse("0.5", R"(,"iterations":1e9)"));
    CHECK_FALSE(parse("0.5", R"(,"delay":1e30)"));
    CHECK_FALSE(parse("0.5", R"x(,"ease":"steps(1000000000)")x"));
    CHECK_FALSE(parse("0.5", R"x(,"ease":"spring(1e30,1)")x"));
}

TEST_CASE("a tiny or huge duration cannot produce NaN poses") {
    static const imb::Keyframe keys[] = {{0.0f, {0, 0, 0, 0}, {}}, {1.0f, {1, 0, 0, 0}, {}}};
    static const imb::Track tracks[] = {{imb::Prop::Opacity, keys, 2}};
    for (float dur : {1e-30f, 1e-8f, 1e30f, 3e38f}) {
        imb::AnimDef def;
        def.name = "T";
        def.duration = dur;
        def.iterations = 0;
        def.tracks = tracks;
        def.track_count = 1;
        for (float t : {0.0f, 0.5f, 100.0f, 1e9f}) {
            imb::Pose pose = imb::SampleAnim(def, t);
            CHECK(std::isfinite(pose.opacity));
        }
        def.iterations = 1000000;
        CHECK(std::isfinite(imb::SampleAnim(def, 5.0f, 1e30f).opacity));
        CHECK(std::isfinite(imb::SampleAnim(def, 5.0f, INFINITY).opacity));
        CHECK(std::isfinite(imb::SampleAnim(def, 5.0f, NAN).opacity));
    }
    imb::AnimDef nan_def;
    nan_def.name = "N";
    nan_def.duration = NAN;
    nan_def.delay = NAN;
    nan_def.tracks = tracks;
    nan_def.track_count = 1;
    CHECK(std::isfinite(imb::SampleAnim(nan_def, 1.0f).opacity));
}

TEST_CASE("motion specs reject out of range numbers") {
    Panel panel;
    Error e;
    Json inf;
    JsonError je;
    if (ParseJson(R"({"schemaVersion":1,"kind":"panel","name":"Main","root":{"id":"window","type":"Window",
        "children":[{"id":"b","type":"Button","anim":{"motions":[{"preset":"Fade","delay":1e999}]}}]}})", inf, &je))
        CHECK_FALSE(PanelFromJson(inf, panel, &e));
    Project p;
    Panel ok;
    REQUIRE(PanelFromJson(J(R"({"schemaVersion":1,"kind":"panel","name":"Main","root":{"id":"window","type":"Window",
        "children":[{"id":"b","type":"Button","anim":{"motions":[{"preset":"Fade","iterations":5000,"durationScale":5000}]}}]}})"), ok, &e));
    std::vector<Diagnostic> diags;
    CHECK_FALSE(ValidatePanel(p, ok, diags));
}

TEST_CASE("animation messages carry caps on text and tags") {
    Json a = J(R"({"name":"Probe","category":"attention","duration":1,"tracks":{"opacity":[{"t":0,"v":1},{"t":1,"v":1}]}})");
    CHECK(AnimationWithinCaps(a));
    Json big = a;
    big.Set("description", Json(std::string(kProtocolMaxAnimText + 1, 'x')));
    CHECK_FALSE(AnimationWithinCaps(big));
    Json tags = a;
    Json arr = Json::Array();
    for (size_t i = 0; i <= kProtocolMaxAnimTags; ++i) arr.Push(Json("t" + std::to_string(i)));
    tags.Set("tags", arr);
    CHECK_FALSE(AnimationWithinCaps(tags));
}
