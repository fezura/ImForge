#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "imforge/imb_motion.h"
#include "imforge/imb_motion_presets.h"
#include "imforge/imforge_core.h"

#include "../src/builder/design_apply.cpp"

#ifndef IMFORGE_SOURCE_DIR
#define IMFORGE_SOURCE_DIR "."
#endif

using namespace imforge;
using namespace imforge::builder;

namespace {

const char* kDesign = R"({
  "schemaVersion": 1, "kind": "design", "name": "NeonCyber", "title": "Neon Cyber",
  "description": "test", "tags": ["dark", "neon"],
  "theme": {
    "colors": { "WindowBg": "#0b0d14ff", "Button": "#7c5cffff", "Text": "#e8e8f0ff" },
    "vars": { "WindowRounding": 10, "FramePadding": {"x": 10, "y": 6} }
  },
  "styleClasses": {
    "accent-button": { "colors": { "Button": "#7c5cffff" }, "vars": { "FrameRounding": 8 } },
    "muted": { "colors": { "Text": "#8a8fa0ff" } }
  },
  "fonts": [
    { "role": "ui", "family": "Inter", "size": 15 },
    { "role": "heading", "family": "Inter", "weight": "Bold", "size": 21 },
    { "role": "mono", "family": "JetBrains Mono", "size": 13 }
  ],
  "fx": { "windowBlur": 0, "windowShadow": 0.6, "accentGlow": 0.4 },
  "motions": { "window": "FadeInUp", "button.hover": "Lift", "button.press": "Squish", "list": "FadeInStagger" },
  "widgetStyle": { "accent": "#7c5cffff" }
})";

Json Design() {
    Json j;
    JsonError je;
    REQUIRE(ParseJson(kDesign, j, &je));
    return j;
}

Node MakeNode(const char* id, const char* type) {
    Node n;
    n.id = id;
    n.type = type;
    return n;
}

Project SampleProject() {
    Project p;
    p.name = "P";
    Theme dark;
    dark.name = "Dark";
    dark.file = "themes/Dark.theme.json";
    dark.colors.emplace_back("WindowBg", ParseColor("#202020ff"));
    p.themes.push_back(dark);
    p.active_theme = "Dark";
    FontSpec f;
    f.name = "Default";
    f.file = "fonts/Inter-Regular.ttf";
    f.merge_icons = "fontawesome6";
    p.fonts.push_back(f);
    StyleOverride old;
    old.colors.emplace_back("Text", ParseColor("#ff0000ff"));
    p.style_classes.emplace_back("muted", old);
    p.style_classes.emplace_back("keep", old);

    Panel panel;
    panel.name = "Main";
    panel.root = MakeNode("root", "Window");
    Node list = MakeNode("list", "VStack");
    list.children.push_back(MakeNode("a", "Text"));
    list.children.push_back(MakeNode("b", "Text"));
    Node ok = MakeNode("ok", "Button");
    ok.style.classes.push_back("accent-button");
    list.children.push_back(ok);
    panel.root.children.push_back(list);
    Node locked = MakeNode("locked", "Button");
    locked.locked = true;
    panel.root.children.push_back(locked);
    panel.root.children.push_back(MakeNode("glow", "GlowButton"));
    p.panels.push_back(panel);
    return p;
}

const Theme* FindTheme(const Project& p, const char* name) {
    for (const Theme& t : p.themes)
        if (t.name == name) return &t;
    return nullptr;
}

const StyleOverride* FindClass(const Project& p, const char* name) {
    for (const auto& kv : p.style_classes)
        if (kv.first == name) return &kv.second;
    return nullptr;
}

const MotionSpec* FindMotion(const Node& n, const char* trigger) {
    for (const MotionSpec& m : n.anim.motions)
        if (m.trigger == trigger) return &m;
    return nullptr;
}

const FxSpec* FindFx(const Node& n, const char* type) {
    for (const FxSpec& f : n.fx)
        if (f.type == type) return &f;
    return nullptr;
}

}  // namespace

TEST_CASE("theme only adds the design theme and makes it active") {
    Project p = SampleProject();
    DesignApplyResult r;
    Error err;
    REQUIRE(ApplyDesign(p, Design(), kApplyThemeOnly, &r, &err));
    CHECK(r.theme == "NeonCyber");
    CHECK(p.active_theme == "NeonCyber");
    REQUIRE(p.themes.size() == 2);
    const Theme* t = FindTheme(p, "NeonCyber");
    REQUIRE(t);
    CHECK(t->file == "themes/NeonCyber.theme.json");
    CHECK(t->colors.size() == 3);
    CHECK(t->vars.size() == 2);
    CHECK(FindTheme(p, "Dark")->colors.size() == 1);
    CHECK(FindClass(p, "accent-button") == nullptr);
    CHECK(p.fonts.size() == 1);
    CHECK(p.panels[0].root.anim.motions.empty());
}

TEST_CASE("applying twice replaces the theme instead of duplicating it") {
    Project p = SampleProject();
    REQUIRE(ApplyDesign(p, Design(), kApplyThemeOnly));
    p.themes.back().file = "themes/custom.theme.json";
    REQUIRE(ApplyDesign(p, Design(), kApplyThemeOnly));
    CHECK(p.themes.size() == 2);
    CHECK(FindTheme(p, "NeonCyber")->file == "themes/custom.theme.json");
}

TEST_CASE("theme and classes merges classes by name") {
    Project p = SampleProject();
    DesignApplyResult r;
    REQUIRE(ApplyDesign(p, Design(), kApplyThemeAndClasses, &r));
    CHECK(r.classes == 2);
    CHECK(p.style_classes.size() == 3);
    const StyleOverride* muted = FindClass(p, "muted");
    REQUIRE(muted);
    REQUIRE(muted->colors.size() == 1);
    CHECK(ColorToHex(muted->colors[0].second) == "#8a8fa0ff");
    CHECK(FindClass(p, "keep") != nullptr);
    const StyleOverride* accent = FindClass(p, "accent-button");
    REQUIRE(accent);
    CHECK(accent->vars.size() == 1);
    CHECK(p.panels[0].root.fx.empty());
}

TEST_CASE("everything sets fonts by role and keeps icon merging") {
    Project p = SampleProject();
    DesignApplyResult r;
    REQUIRE(ApplyDesign(p, Design(), kApplyEverything, &r));
    CHECK(r.fonts == 3);
    REQUIRE(p.fonts.size() == 3);
    CHECK(p.fonts[0].name == "Default");
    CHECK(p.fonts[0].size == doctest::Approx(15));
    CHECK(p.fonts[0].merge_icons == "fontawesome6");
    CHECK(p.fonts[1].name == "Heading");
    CHECK(p.fonts[1].file == "fonts/Inter-Bold.ttf");
    CHECK(p.fonts[1].merge_icons == "fontawesome6");
    CHECK(p.fonts[2].name == "Mono");
    CHECK(p.fonts[2].file == "fonts/JetBrainsMono-Regular.ttf");
}

TEST_CASE("everything adds default motions to windows, buttons and lists") {
    Project p = SampleProject();
    DesignApplyResult r;
    REQUIRE(ApplyDesign(p, Design(), kApplyEverything, &r));
    const Node& root = p.panels[0].root;
    const MotionSpec* win = FindMotion(root, "enter");
    REQUIRE(win);
    CHECK(win->preset == "FadeInUp");

    const Node* list = FindNode(root, "list");
    REQUIRE(list);
    const MotionSpec* lm = FindMotion(*list, "enter");
    REQUIRE(lm);
    CHECK(lm->preset == "FadeInStagger");
    CHECK(lm->stagger > 0.0f);

    const Node* ok = FindNode(root, "ok");
    REQUIRE(ok);
    REQUIRE(FindMotion(*ok, "hover"));
    CHECK(FindMotion(*ok, "hover")->preset == "Lift");
    CHECK(FindMotion(*ok, "press")->preset == "Squish");
    CHECK(FindMotion(*FindNode(root, "glow"), "hover") != nullptr);
    CHECK(FindNode(root, "locked")->anim.motions.empty());
    CHECK(FindNode(root, "a")->anim.motions.empty());
    CHECK(r.motions == 6);

    REQUIRE(ApplyDesign(p, Design(), kApplyEverything, &r));
    CHECK(FindNode(root, "ok")->anim.motions.size() == 2);
}

TEST_CASE("everything adds window shadow and accent glow fx") {
    Project p = SampleProject();
    p.panels[0].root.fx.push_back(FxSpec{"blur", Json::Object()});
    REQUIRE(ApplyDesign(p, Design(), kApplyEverything));
    const Node& root = p.panels[0].root;
    const FxSpec* shadow = FindFx(root, "shadow");
    REQUIRE(shadow);
    CHECK(shadow->params["size"].AsDouble() > 6.0);
    CHECK(FindFx(root, "blur") == nullptr);
    const FxSpec* glow = FindFx(*FindNode(root, "ok"), "glow");
    REQUIRE(glow);
    CHECK(glow->params["color"].AsString().compare(0, 7, "#7c5cff") == 0);
}

TEST_CASE("bad designs are rejected without touching the project") {
    Project p = SampleProject();
    Error err;
    Json not_design = Json::Object();
    not_design.Set("kind", Json("theme"));
    CHECK_FALSE(ApplyDesign(p, not_design, kApplyEverything, nullptr, &err));
    CHECK(!err.message.empty());

    Json bad = Design();
    Json theme = Json::Object();
    Json colors = Json::Object();
    colors.Set("WindowBg", Json("not a color"));
    theme.Set("colors", colors);
    bad.Set("theme", theme);
    CHECK_FALSE(ApplyDesign(p, bad, kApplyEverything, nullptr, &err));
    CHECK(err.message.find("WindowBg") != std::string::npos);
    CHECK(p.themes.size() == 1);
    CHECK(p.active_theme == "Dark");
}

TEST_CASE("theme from design reads colors and vars") {
    Theme t;
    REQUIRE(ThemeFromDesign(Design(), t));
    CHECK(t.name == "NeonCyber");
    CHECK(DesignTitle(Design()) == "Neon Cyber");
    CHECK(DesignFontFile("Fira Code", "") == "fonts/JetBrainsMono-Regular.ttf");
    CHECK(DesignFontFile("Inter", "700") == "fonts/Inter-Bold.ttf");
}

TEST_CASE("every shipped design applies cleanly") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::u8path(IMFORGE_SOURCE_DIR) / "presets" / "designs";
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename().u8string().find(".imdesign.json") != std::string::npos) files.push_back(it->path());
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() >= 36);
    std::vector<std::string> names;
    for (const fs::path& f : files) {
        CAPTURE(f.filename().u8string());
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        Json d;
        JsonError je;
        REQUIRE(ParseJson(ss.str(), d, &je));
        CHECK(d["name"].AsString() + ".imdesign.json" == f.filename().u8string());
        CHECK(std::find(names.begin(), names.end(), d["name"].AsString()) == names.end());
        names.push_back(d["name"].AsString());

        Project p = SampleProject();
        DesignApplyResult r;
        Error err;
        REQUIRE_MESSAGE(ApplyDesign(p, d, kApplyEverything, &r, &err), err.message);
        CHECK(r.warnings.empty());
        CHECK(p.active_theme == d["name"].AsString());
        const Theme* t = FindTheme(p, d["name"].AsString().c_str());
        REQUIRE(t);
        CHECK(t->colors.size() >= 50);
        CHECK(r.classes >= 3);
        CHECK(FindClass(p, "accent-button") != nullptr);
        CHECK(r.fonts >= 1);
        CHECK(r.motions > 0);
        CHECK(!FindTheme(p, "Dark")->colors.empty());
        for (const auto& kv : d["motions"].obj) {
            std::string preset = kv.second.IsString() ? kv.second.AsString() : kv.second["preset"].AsString();
            CAPTURE(kv.first);
            CHECK(imb::FindAnim(preset.c_str()) != nullptr);
        }
    }
}

TEST_CASE("unknown motion triggers are skipped with a warning") {
    Project p = SampleProject();
    Json d = Design();
    Json motions = Json::Object();
    motions.Set("button.hoverr", Json("Lift"));
    motions.Set("window", Json("FadeInUp"));
    d.Set("motions", motions);
    DesignApplyResult r;
    Error err;
    REQUIRE(ApplyDesign(p, d, kApplyEverything, &r, &err));
    CHECK(r.warnings.size() == 1);
    CHECK(r.motions == 1);
    CHECK(p.panels[0].root.anim.motions.size() == 1);
    for (const Node& c : p.panels[0].root.children[0].children) CHECK(c.anim.motions.empty());
}

TEST_CASE("theme files do not collide between designs") {
    Project p = SampleProject();
    Json a = Design();
    a.Set("name", Json("Neon Cyber"));
    Json b = Design();
    b.Set("name", Json("Neon/Cyber"));
    Json c = Design();
    c.Set("name", Json("neon cyber"));
    REQUIRE(ApplyDesign(p, a, kApplyThemeOnly));
    REQUIRE(ApplyDesign(p, b, kApplyThemeOnly));
    REQUIRE(ApplyDesign(p, c, kApplyThemeOnly));
    REQUIRE(p.themes.size() == 4);
    for (size_t i = 0; i < p.themes.size(); ++i)
        for (size_t j = i + 1; j < p.themes.size(); ++j) CHECK(Lower(p.themes[i].file) != Lower(p.themes[j].file));
}

TEST_CASE("a design with only a theme applies in every mode") {
    Project p = SampleProject();
    Json d = Json::Object();
    d.Set("kind", Json("design"));
    d.Set("name", Json("Bare"));
    Json theme = Json::Object();
    d.Set("theme", theme);
    DesignApplyResult r;
    Error err;
    REQUIRE(ApplyDesign(p, d, kApplyEverything, &r, &err));
    CHECK(r.classes == 0);
    CHECK(r.fonts == 0);
    CHECK(r.fx == 0);
    CHECK(r.motions == 0);
    CHECK(p.active_theme == "Bare");
}

TEST_CASE("odd font and motion values do not break an apply") {
    Project p = SampleProject();
    Json d = Design();
    Json fonts = Json::Array();
    fonts.Push(Json("not an object"));
    Json f = Json::Object();
    f.Set("role", Json("ui"));
    f.Set("size", Json(5000));
    fonts.Push(f);
    d.Set("fonts", fonts);
    Json motions = Json::Object();
    motions.Set("window", Json(7));
    d.Set("motions", motions);
    DesignApplyResult r;
    Error err;
    REQUIRE(ApplyDesign(p, d, kApplyEverything, &r, &err));
    CHECK(r.fonts == 1);
    CHECK(p.fonts.front().size == doctest::Approx(16.0f));
    CHECK(r.warnings.size() == 2);
}

TEST_CASE("font files outside the project are replaced by the default") {
    for (const char* bad : {"/etc/passwd.ttf", "C:/Windows/Fonts/x.ttf", "\\\\evil\\share\\x.ttf", "../../x.ttf", "fonts/../../x.ttf"}) {
        Project p = SampleProject();
        Json d = Design();
        Json fonts = Json::Array();
        Json f = Json::Object();
        f.Set("role", Json("ui"));
        f.Set("file", Json(bad));
        fonts.Push(f);
        d.Set("fonts", fonts);
        DesignApplyResult r;
        Error err;
        REQUIRE(ApplyDesign(p, d, kApplyEverything, &r, &err));
        INFO(bad);
        CHECK(p.fonts.front().file == "fonts/Inter-Regular.ttf");
        CHECK(r.warnings.size() == 1);
    }
    Project p = SampleProject();
    Json d = Design();
    Json fonts = Json::Array();
    Json f = Json::Object();
    f.Set("role", Json("ui"));
    f.Set("file", Json("fonts/My Font.ttf"));
    fonts.Push(f);
    d.Set("fonts", fonts);
    Error err;
    REQUIRE(ApplyDesign(p, d, kApplyEverything, nullptr, &err));
    CHECK(p.fonts.front().file == "fonts/My Font.ttf");
}

TEST_CASE("style class names and motion numbers from a design are checked") {
    Project p = SampleProject();
    Json d = Design();
    Json classes = Json::Object();
    classes.Set("fine-one", Json::Object());
    classes.Set("evil(){system(1);}", Json::Object());
    d.Set("styleClasses", classes);
    Json motions = Json::Object();
    Json big = Json::Object();
    big.Set("preset", Json("Lift"));
    big.Set("durationScale", Json(1e30));
    motions.Set("button.hover", big);
    d.Set("motions", motions);
    DesignApplyResult r;
    Error err;
    REQUIRE(ApplyDesign(p, d, kApplyEverything, &r, &err));
    CHECK(r.classes == 1);
    CHECK(r.motions == 0);
    CHECK(r.warnings.size() == 2);
}
