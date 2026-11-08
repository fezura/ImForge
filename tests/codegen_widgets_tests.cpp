#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "codegen.h"
#include "imforge/imforge_core.h"
#include "imforge/imforge_widgets.h"

using namespace imforge;
using namespace imforge::codegen;

namespace {

const char* const kTypes = R"json({
  "schemaVersion": 1, "kind": "types",
  "structs": {
    "Player": { "header": "types_stub.h", "fields": [
      {"name":"health","type":"float","ctype":"Float"},
      {"name":"speed","type":"float","ctype":"Float"},
      {"name":"level","type":"int","ctype":"I32"},
      {"name":"vel","type":"float[3]","ctype":"FloatArray","count":3},
      {"name":"pos","type":"ImVec2","ctype":"Vec2"},
      {"name":"col","type":"ImVec4","ctype":"Vec4"},
      {"name":"rgb","type":"float[3]","ctype":"FloatArray","count":3},
      {"name":"rgba","type":"float[4]","ctype":"FloatArray","count":4},
      {"name":"ints","type":"int[2]","ctype":"IntArray","count":2},
      {"name":"name","type":"std::string","ctype":"StdString"},
      {"name":"tag","type":"char[32]","ctype":"CharArray","count":32},
      {"name":"history","type":"float[16]","ctype":"FloatArray","count":16},
      {"name":"samples","type":"std::vector<float>","ctype":"Vector","elem":"float"},
      {"name":"icon","type":"ImTextureID","ctype":"TexID"},
      {"name":"progress","type":"float","ctype":"Float"}
    ] },
    "Settings": { "header": "types_stub.h", "fields": [
      {"name":"esp","type":"Esp","ctype":"Struct"},
      {"name":"mode","type":"Mode","ctype":"Enum"},
      {"name":"weapon","type":"Weapon","ctype":"Enum"},
      {"name":"idx","type":"int","ctype":"I32"},
      {"name":"sel","type":"bool","ctype":"Bool"},
      {"name":"enabled","type":"bool","ctype":"Bool"}
    ] },
    "Esp": { "header": "types_stub.h", "fields": [ {"name":"enabled","type":"bool","ctype":"Bool"} ] }
  },
  "enums": {
    "Mode": { "values": [["Off",0],["Aim",1],["Silent",2]] },
    "Weapon": { "values": [["None",0],["Rifle",2],["Pistol",5]] }
  }
})json";

const char* const kTypesStubHeader = R"cpp(#pragma once
#include <string>
#include <vector>
#include "imgui.h"
enum Mode { Off = 0, Aim = 1, Silent = 2 };
enum Weapon { None = 0, Rifle = 2, Pistol = 5 };
struct Esp { bool enabled = false; };
struct Player {
    float health = 100, speed = 1; int level = 1; float vel[3] = {0, 0, 0}; ImVec2 pos; ImVec4 col;
    float rgb[3] = {0, 0, 0}; float rgba[4] = {0, 0, 0, 1}; int ints[2] = {0, 0}; std::string name;
    char tag[32] = ""; float history[16] = {0}; std::vector<float> samples; ImTextureID icon = ImTextureID();
    float progress = 0;
};
struct Settings { Esp esp; Mode mode = Off; Weapon weapon = None; int idx = 0; bool sel = false; bool enabled = true; };
)cpp";

Json ParseOrFail(const std::string& text) {
    Json j;
    JsonError e;
    REQUIRE_MESSAGE(ParseJson(text, j, &e), e.message << " at " << e.line << ":" << e.col);
    return j;
}

const Json& TypesJson() {
    static Json j = ParseOrFail(kTypes);
    return j;
}

struct Gen {
    Result result;
    std::string cpp, header, all;
    bool Has(const std::string& needle) const { return all.find(needle) != std::string::npos; }
    int Count(const std::string& needle) const {
        int c = 0;
        for (size_t p = all.find(needle); p != std::string::npos; p = all.find(needle, p + needle.size())) ++c;
        return c;
    }
    bool HasError() const {
        for (const Diagnostic& d : result.diagnostics)
            if (d.level == Diagnostic::Level::Error) return true;
        return false;
    }
    std::string Errors() const {
        std::string s;
        for (const Diagnostic& d : result.diagnostics)
            if (d.level == Diagnostic::Level::Error) s += d.node_id + ": " + d.message + "\n";
        return s;
    }
};

Project MakeProject(int imgui_version_num = 19190) {
    Project p;
    p.name = "Test";
    p.imgui_version = Project::VersionString(imgui_version_num);
    p.features.emplace_back("animations", false);
    p.features.emplace_back("drawlistFx", false);
    p.features.emplace_back("shaderFx", false);
    p.features.emplace_back("iconFonts", false);
    p.binding_types = {"Player", "Settings"};
    return p;
}

Panel MakePanel(const std::string& children_json) {
    std::string text = R"json({"schemaVersion":1,"kind":"panel","name":"Main",
      "params":[{"name":"player","type":"Player"},{"name":"cfg","type":"Settings"}],
      "state":[{"name":"open","type":"bool","default":true},{"name":"text","type":"string","default":""},
               {"name":"color","type":"color","default":"#80402080"},{"name":"f","type":"float","default":0},
               {"name":"i","type":"int","default":0},{"name":"v2","type":"vec2"}],
      "root":{"id":"window","type":"Window","props":{"title":"Main"},"children":[)json" +
                       children_json + "]}}";
    Panel panel;
    Error e;
    REQUIRE_MESSAGE(PanelFromJson(ParseOrFail(text), panel, &e), e.message);
    return panel;
}

Gen Generate(const std::string& children_json, int imgui_version_num = 19190,
             const WidgetCatalog* catalog = nullptr, bool std_string = true) {
    Project project = MakeProject(imgui_version_num);
    project.codegen.std_string = std_string;
    project.widget_catalog = catalog;
    Panel panel = MakePanel(children_json);
    Options opts;
    opts.settings = project.codegen;
    opts.imgui_version = imgui_version_num;
    opts.features_animations = opts.features_fx = opts.features_icons = false;
    opts.types = &TypesJson();
    Gen g;
    g.result = GeneratePanel(project, panel, opts);
    for (const FileOut& f : g.result.files) {
        g.all += f.content;
        if (f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".cpp") == 0) g.cpp = f.content;
        else g.header = f.content;
    }
    return g;
}

Gen GenOne(const std::string& node_json, int imgui_version_num = 19190) {
    Gen g = Generate(node_json, imgui_version_num);
    INFO(g.all);
    CHECK_MESSAGE(!g.HasError(), g.Errors());
    return g;
}

}  // namespace

TEST_CASE("codegen widgets: text family") {
    SUBCASE("plain text is TextUnformatted") {
        Gen g = GenOne(R"({"id":"t","type":"Text","props":{"text":"Hello"}})");
        CHECK(g.Has("ImGui::TextUnformatted(\"Hello\");"));
        CHECK_FALSE(g.Has("ImGui::Text(\"%s\""));
    }
    SUBCASE("interpolated text is Text(fmt, args)") {
        Gen g = GenOne(R"({"id":"t","type":"Text","props":{"text":"HP: {player.health:%.0f} / {player.level}"}})");
        CHECK(g.Has("ImGui::Text(\"HP: %.0f / %d\", "));
        CHECK(g.Has("player.health"));
        CHECK(g.Has("player.level"));
    }
    SUBCASE("colored, wrapped, bullet, disabled") {
        Gen c = GenOne(R"({"id":"t","type":"Text","props":{"text":"Hi","color":"#ff0000ff"}})");
        CHECK(c.Has("ImGui::TextColored(ImVec4("));
        CHECK(c.Has(", \"Hi\");"));
        Gen w = GenOne(R"({"id":"t","type":"Text","props":{"text":"Hi","wrapped":true}})");
        CHECK(w.Has("ImGui::TextWrapped(\"Hi\");"));
        Gen b = GenOne(R"({"id":"t","type":"Text","props":{"text":"Hi","bullet":true}})");
        CHECK(b.Has("ImGui::BulletText(\"Hi\");"));
        Gen d = GenOne(R"({"id":"t","type":"Text","props":{"text":"Hi","disabled":true}})");
        CHECK(d.Has("ImGui::TextDisabled(\"Hi\");"));
        Gen dw = GenOne(R"({"id":"t","type":"Text","props":{"text":"Hi","disabled":true,"wrapped":true}})");
        CHECK(dw.Has("ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));"));
        CHECK(dw.Has("ImGui::TextWrapped(\"Hi\");"));
        CHECK(dw.Has("ImGui::PopStyleColor();"));
    }
    SUBCASE("SeparatorText is always version-guarded") {
        Gen g = GenOne(R"({"id":"s","type":"SeparatorText","props":{"text":"Section"}})");
        CHECK(g.Has("ImGui::SeparatorText(\"Section\");"));
        CHECK(g.Has("#if IMGUI_VERSION_NUM >= 18950"));
        Gen old = GenOne(R"({"id":"s","type":"SeparatorText","props":{"text":"Section"}})", 18500);
        CHECK(old.Has("#if IMGUI_VERSION_NUM >= 18950"));
        CHECK(old.Has("ImGui::SeparatorText(\"Section\");"));
        CHECK(old.Has("#else"));
        CHECK(old.Has("ImGui::Separator();"));
        CHECK(old.Has("ImGui::TextUnformatted(\"Section\");"));
        CHECK(old.Has("#endif"));
    }
    SUBCASE("Separator, Spacing, NewLine, Dummy") {
        Gen g = GenOne(R"({"id":"a","type":"Separator"},{"id":"b","type":"Spacing"},
                           {"id":"c","type":"Spacing","props":{"count":3}},{"id":"d","type":"NewLine"},
                           {"id":"e","type":"Dummy"},{"id":"f","type":"Dummy","props":{"size":{"w":20,"h":10}}})");
        CHECK(g.Has("ImGui::Separator();"));
        CHECK(g.Has("ImGui::Spacing();"));
        CHECK(g.Has("for (int k = 0; k < 3; ++k) ImGui::Spacing();"));
        CHECK(g.Has("ImGui::NewLine();"));
        CHECK(g.Has("ImGui::Dummy(ImVec2(0, 0));"));
        CHECK(g.Has("ImGui::Dummy(ImVec2(20 * imb_s, 10 * imb_s));"));
    }
}

TEST_CASE("codegen widgets: buttons") {
    SUBCASE("Button with size, event and handler") {
        Gen g = GenOne(R"({"id":"btn_play","type":"Button","props":{"label":"Play","size":{"w":"fill","h":"auto"}},
                           "events":{"onClick":{"actions":[{"type":"toggle","target":"cfg.esp.enabled"}],"handler":true}}})");
        CHECK(g.Has("if (ImGui::Button(\"Play\", ImVec2(-FLT_MIN, 0))) {"));
        CHECK(g.Has("cfg.esp.enabled = !cfg.esp.enabled;"));
        CHECK(g.Has("Main_btn_play_OnClick("));
    }
    SUBCASE("Button without event is a bare call") {
        Gen g = GenOne(R"({"id":"b","type":"Button","props":{"label":"Ok"}})");
        CHECK(g.Has("ImGui::Button(\"Ok\");"));
        CHECK_FALSE(g.Has("if (ImGui::Button"));
    }
    SUBCASE("Button repeat") {
        Gen g = GenOne(R"({"id":"b","type":"Button","props":{"label":"+","repeat":true}})");
        CHECK(g.Has("ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);"));
        CHECK(g.Has("ImGui::PopItemFlag();"));
        CHECK(g.Has("#if IMGUI_VERSION_NUM >= 19100"));
        CHECK(g.Has("ImGui::PushButtonRepeat(true);"));
        Gen old = GenOne(R"({"id":"b","type":"Button","props":{"label":"+","repeat":true}})", 18500);
        CHECK(old.Has("#if IMGUI_VERSION_NUM >= 19100"));
        CHECK(old.Has("ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);"));
        CHECK(old.Has("ImGui::PushButtonRepeat(true);"));
        CHECK(old.Has("ImGui::PopButtonRepeat();"));
    }
    SUBCASE("SmallButton, ArrowButton, ColorButton") {
        Gen g = GenOne(R"({"id":"sb","type":"SmallButton","props":{"label":"x"},"events":{"onClick":{"handler":true}}},
                           {"id":"ab","type":"ArrowButton","props":{"dir":"Left"}},
                           {"id":"cb","type":"ColorButton","props":{"desc":"swatch","color":"#ff0000ff"}},
                           {"id":"cb2","type":"ColorButton","props":{"desc":"sw2","color":"#00ff00ff","flags":["NoTooltip"],"size":{"w":32,"h":32}}})");
        CHECK(g.Has("if (ImGui::SmallButton(\"x\")) {"));
        CHECK(g.Has("ImGui::ArrowButton(\"ab\", ImGuiDir_Left);"));
        CHECK(g.Has("ImGui::ColorButton(\"swatch\", ImVec4("));
        CHECK(g.Has("ImGuiColorEditFlags_NoTooltip, ImVec2(32 * imb_s, 32 * imb_s));"));
    }
}

TEST_CASE("codegen widgets: toggles and lists") {
    SUBCASE("Checkbox") {
        Gen g = GenOne(R"({"id":"esp","type":"Checkbox","props":{"label":"ESP"},"bind":{"value":"cfg.esp.enabled"},
                           "events":{"onChange":{"handler":true}}})");
        CHECK(g.Has("if (ImGui::Checkbox(\"ESP\", &cfg.esp.enabled)) {"));
        CHECK(g.Has("Main_esp_OnChange("));
        Gen bare = GenOne(R"({"id":"esp","type":"Checkbox","props":{"label":"ESP"},"bind":{"value":"cfg.esp.enabled"}})");
        CHECK(bare.Has("ImGui::Checkbox(\"ESP\", &cfg.esp.enabled);"));
    }
    SUBCASE("RadioButton int and enum") {
        Gen g = GenOne(R"({"id":"r","type":"RadioButton","props":{"label":"Two","value":2},"bind":{"value":"cfg.idx"}})");
        CHECK(g.Has("ImGui::RadioButton(\"Two\", &cfg.idx, 2);"));
        Gen e = GenOne(R"({"id":"r","type":"RadioButton","props":{"label":"Aim","value":1},"bind":{"value":"cfg.mode"}})");
        CHECK(e.Has("= (int)cfg.mode;"));
        CHECK(e.Has("cfg.mode = (Mode)"));
    }
    SUBCASE("Combo with items and int binding") {
        Gen g = GenOne(R"({"id":"cmb","type":"Combo","props":{"label":"Mode","items":["A","B","C"]},"bind":{"value":"cfg.idx"},
                           "events":{"onChange":{"handler":true}}})");
        CHECK(g.Has("static const char* cmb_items[] = {\"A\", \"B\", \"C\"};"));
        CHECK(g.Has("if (ImGui::Combo(\"Mode\", &cfg.idx, cmb_items, IM_ARRAYSIZE(cmb_items))) {"));
    }
    SUBCASE("Combo bound to an enum uses the enum names") {
        Gen g = GenOne(R"({"id":"cmb","type":"Combo","props":{"label":"Mode"},"bind":{"value":"cfg.mode"}})");
        CHECK(g.Has("static const char* cmb_items[] = {\"Off\", \"Aim\", \"Silent\"};"));
        CHECK(g.Has("int cmb_idx = (int)cfg.mode;"));
        CHECK(g.Has("if (ImGui::Combo(\"Mode\", &cmb_idx, cmb_items, IM_ARRAYSIZE(cmb_items))) {"));
        CHECK(g.Has("cfg.mode = (Mode)cmb_idx;"));
    }
    SUBCASE("Combo bound to a non-contiguous enum uses a value table") {
        Gen g = GenOne(R"({"id":"w","type":"Combo","props":{"label":"Weapon"},"bind":{"value":"cfg.weapon"}})");
        CHECK(g.Has("static const int w_values[] = {0, 2, 5};"));
        CHECK(g.Has("if (w_values[k] == (int)cfg.weapon) w_idx = k;"));
        CHECK(g.Has("cfg.weapon = (Weapon)w_values[w_idx];"));
    }
    SUBCASE("Combo with flags uses BeginCombo") {
        Gen g = GenOne(R"({"id":"c","type":"Combo","props":{"label":"M","items":["A","B"],"flags":["NoArrowButton"]},"bind":{"value":"cfg.idx"}})");
        CHECK(g.Has("if (ImGui::BeginCombo(\"M\", (cfg.idx >= 0 && cfg.idx < IM_ARRAYSIZE(c_items)) ? c_items[cfg.idx] : \"\", ImGuiComboFlags_NoArrowButton)) {"));
        CHECK(g.Has("if (ImGui::Selectable(c_items[k], k == cfg.idx)) {"));
        CHECK(g.Has("ImGui::EndCombo();"));
    }
    SUBCASE("ListBox") {
        Gen g = GenOne(R"({"id":"lb","type":"ListBox","props":{"label":"List","items":["x","y"],"heightItems":4},"bind":{"value":"cfg.idx"}})");
        CHECK(g.Has("ImGui::ListBox(\"List\", &cfg.idx, lb_items, IM_ARRAYSIZE(lb_items), 4);"));
        Gen d = GenOne(R"({"id":"lb","type":"ListBox","props":{"label":"List","items":["x","y"]},"bind":{"value":"cfg.idx"}})");
        CHECK(d.Has("ImGui::ListBox(\"List\", &cfg.idx, lb_items, IM_ARRAYSIZE(lb_items));"));
    }
}

TEST_CASE("codegen widgets: sliders, drags, inputs") {
    SUBCASE("SliderFloat scalar, N components, vec binding") {
        Gen g = GenOne(R"({"id":"sl","type":"SliderFloat","props":{"label":"Speed","min":0,"max":10},"bind":{"value":"player.speed"},
                           "events":{"onChange":{"handler":true}}})");
        CHECK(g.Has("if (ImGui::SliderFloat(\"Speed\", &player.speed, 0.0f, 10.0f)) {"));
        Gen v3 = GenOne(R"({"id":"sl","type":"SliderFloat","props":{"label":"Speed","min":0,"max":10,"format":"%.2f","flags":["AlwaysClamp"],"components":3},
                            "bind":{"value":"player.vel"}})");
        CHECK(v3.Has("ImGui::SliderFloat3(\"Speed\", player.vel, 0.0f, 10.0f, \"%.2f\", ImGuiSliderFlags_AlwaysClamp);"));
        Gen v2 = GenOne(R"({"id":"sl","type":"SliderFloat","props":{"label":"Pos","components":2},"bind":{"value":"player.pos"}})");
        CHECK(v2.Has("ImGui::SliderFloat2(\"Pos\", &player.pos.x, 0.0f, 1.0f);"));
        Gen v4 = GenOne(R"({"id":"sl","type":"SliderFloat","props":{"label":"Col","components":4},"bind":{"value":"player.col"}})");
        CHECK(v4.Has("ImGui::SliderFloat4(\"Col\", &player.col.x, 0.0f, 1.0f);"));
    }
    SUBCASE("SliderInt and int arrays") {
        Gen g = GenOne(R"({"id":"si","type":"SliderInt","props":{"label":"Level","min":1,"max":99},"bind":{"value":"player.level"}})");
        CHECK(g.Has("ImGui::SliderInt(\"Level\", &player.level, 1, 99);"));
        Gen a = GenOne(R"({"id":"si","type":"SliderInt","props":{"label":"Ints","components":2,"format":"%03d"},"bind":{"value":"player.ints"}})");
        CHECK(a.Has("ImGui::SliderInt2(\"Ints\", player.ints, 0, 100, \"%03d\");"));
    }
    SUBCASE("VSliderFloat / VSliderInt sizes") {
        Gen g = GenOne(R"({"id":"vs","type":"VSliderFloat","props":{"label":"##v","min":0,"max":1},"bind":{"value":"player.speed"}})");
        CHECK(g.Has("ImGui::VSliderFloat(\"##v\", ImVec2(ImGui::GetFrameHeight(), 100 * imb_s), &player.speed, 0.0f, 1.0f);"));
        Gen s = GenOne(R"({"id":"vs","type":"VSliderInt","props":{"label":"##v","size":{"w":30,"h":120}},"bind":{"value":"player.level"}})");
        CHECK(s.Has("ImGui::VSliderInt(\"##v\", ImVec2(30 * imb_s, 120 * imb_s), &player.level, 0, 100);"));
        Gen h = GenOne(R"({"id":"vs","type":"VSliderInt","props":{"label":"##v","size":{"w":"auto","h":120}},"bind":{"value":"player.level"}})");
        CHECK(h.Has("if (vs_size.x <= 0.0f) vs_size.x = ImGui::GetFrameHeight();"));
        CHECK(h.Has("ImGui::VSliderInt(\"##v\", vs_size, &player.level, 0, 100);"));
    }
    SUBCASE("DragFloat / DragInt") {
        Gen g = GenOne(R"({"id":"d","type":"DragFloat","props":{"label":"Health"},"bind":{"value":"player.health"}})");
        CHECK(g.Has("ImGui::DragFloat(\"Health\", &player.health);"));
        Gen f = GenOne(R"({"id":"d","type":"DragFloat","props":{"label":"Health","speed":0.5,"min":0,"max":100,"format":"%.1f","flags":["Logarithmic"]},"bind":{"value":"player.health"}})");
        CHECK(f.Has("ImGui::DragFloat(\"Health\", &player.health, 0.5f, 0.0f, 100.0f, \"%.1f\", ImGuiSliderFlags_Logarithmic);"));
        Gen i = GenOne(R"({"id":"d","type":"DragInt","props":{"label":"Lvl","max":50,"components":2},"bind":{"value":"player.ints"}})");
        CHECK(i.Has("ImGui::DragInt2(\"Lvl\", player.ints, 1.0f, 0, 50);"));
    }
    SUBCASE("InputInt / InputFloat") {
        Gen g = GenOne(R"({"id":"ii","type":"InputInt","props":{"label":"Level"},"bind":{"value":"player.level"}})");
        CHECK(g.Has("ImGui::InputInt(\"Level\", &player.level);"));
        Gen s = GenOne(R"({"id":"ii","type":"InputInt","props":{"label":"Level","step":5,"stepFast":50,"flags":["ReadOnly"]},"bind":{"value":"player.level"}})");
        CHECK(s.Has("ImGui::InputInt(\"Level\", &player.level, 5, 50, ImGuiInputTextFlags_ReadOnly);"));
        Gen n = GenOne(R"({"id":"ii","type":"InputInt","props":{"label":"Ints","components":2},"bind":{"value":"player.ints"}})");
        CHECK(n.Has("ImGui::InputInt2(\"Ints\", player.ints);"));
        Gen f = GenOne(R"({"id":"if","type":"InputFloat","props":{"label":"HP","step":1,"stepFast":10,"format":"%.1f"},"bind":{"value":"player.health"}})");
        CHECK(f.Has("ImGui::InputFloat(\"HP\", &player.health, 1.0f, 10.0f, \"%.1f\");"));
        Gen f3 = GenOne(R"({"id":"if","type":"InputFloat","props":{"label":"Vel","components":3},"bind":{"value":"player.vel"}})");
        CHECK(f3.Has("ImGui::InputFloat3(\"Vel\", player.vel);"));
    }
    SUBCASE("unbound slider falls back to a static local") {
        Gen g = GenOne(R"({"id":"u","type":"SliderFloat","props":{"label":"U"}})");
        CHECK(g.Has("static float u_value = 0.0f;"));
        CHECK(g.Has("ImGui::SliderFloat(\"U\", &u_value, 0.0f, 1.0f);"));
    }
}

TEST_CASE("codegen widgets: text input") {
    SUBCASE("std::string binding uses imgui_stdlib") {
        Gen g = GenOne(R"({"id":"name","type":"InputText","props":{"label":"Name"},"bind":{"value":"player.name"}})");
        CHECK(g.Has("ImGui::InputText(\"Name\", &player.name);"));
        CHECK(g.Has("misc/cpp/imgui_stdlib.h"));
    }
    SUBCASE("char[N] binding and hint") {
        Gen g = GenOne(R"({"id":"tag","type":"InputText","props":{"label":"Tag","hint":"type here","flags":["CharsNoBlank"]},"bind":{"value":"player.tag"}})");
        CHECK(g.Has("ImGui::InputTextWithHint(\"Tag\", \"type here\", player.tag, IM_ARRAYSIZE(player.tag), ImGuiInputTextFlags_CharsNoBlank);"));
    }
    SUBCASE("onEnter adds EnterReturnsTrue and keeps onChange via IsItemEdited") {
        Gen g = GenOne(R"({"id":"cmd","type":"InputText","props":{"label":"Cmd"},"bind":{"value":"player.tag"},
                           "events":{"onEnter":{"handler":true},"onChange":{"handler":true}}})");
        CHECK(g.Has("if (ImGui::InputText(\"Cmd\", player.tag, IM_ARRAYSIZE(player.tag), ImGuiInputTextFlags_EnterReturnsTrue)) {"));
        CHECK(g.Has("Main_cmd_OnEnter("));
        CHECK(g.Has("if (ImGui::IsItemEdited()) {"));
        CHECK(g.Has("Main_cmd_OnChange("));
    }
    SUBCASE("state string binding") {
        Gen g = GenOne(R"({"id":"st","type":"InputText","props":{"label":"S"},"bind":{"value":"state.text"}})");
        CHECK(g.Has("ImGui::InputText(\"S\", &state.text);"));
    }
    SUBCASE("std::string without stdString setting uses a static buffer") {
        Gen g = Generate(R"({"id":"name","type":"InputText","props":{"label":"Name","maxLength":64},"bind":{"value":"player.name"}})", 19190, nullptr, false);
        CHECK(g.Has("static char name_buf[64] = \"\";"));
        CHECK(g.Has("if (ImGui::InputText(\"Name\", name_buf, IM_ARRAYSIZE(name_buf))) {"));
        CHECK(g.Has("player.name = name_buf;"));
    }
    SUBCASE("InputTextMultiline") {
        Gen g = GenOne(R"({"id":"ml","type":"InputTextMultiline","props":{"label":"Notes","size":{"w":"fill","h":120}},"bind":{"value":"player.tag"}})");
        CHECK(g.Has("ImGui::InputTextMultiline(\"Notes\", player.tag, IM_ARRAYSIZE(player.tag), ImVec2(-FLT_MIN, 120 * imb_s));"));
        Gen s = GenOne(R"({"id":"ml","type":"InputTextMultiline","props":{"label":"Notes","flags":["AllowTabInput"]},"bind":{"value":"player.name"}})");
        CHECK(s.Has("ImGui::InputTextMultiline(\"Notes\", &player.name, ImVec2(0, 0), ImGuiInputTextFlags_AllowTabInput);"));
    }
}

TEST_CASE("codegen widgets: colors") {
    Gen v4 = GenOne(R"({"id":"c","type":"ColorEdit","props":{"label":"Col"},"bind":{"value":"player.col"}})");
    CHECK(v4.Has("ImGui::ColorEdit4(\"Col\", &player.col.x);"));
    Gen f4 = GenOne(R"({"id":"c","type":"ColorEdit","props":{"label":"Col","flags":["NoInputs"]},"bind":{"value":"player.rgba"}})");
    CHECK(f4.Has("ImGui::ColorEdit4(\"Col\", player.rgba, ImGuiColorEditFlags_NoInputs);"));
    Gen f3 = GenOne(R"({"id":"c","type":"ColorEdit","props":{"label":"Col"},"bind":{"value":"player.rgb"}})");
    CHECK(f3.Has("ImGui::ColorEdit3(\"Col\", player.rgb);"));
    Gen na = GenOne(R"({"id":"c","type":"ColorEdit","props":{"label":"Col","alpha":false},"bind":{"value":"player.col"}})");
    CHECK(na.Has("ImGui::ColorEdit4(\"Col\", &player.col.x, ImGuiColorEditFlags_NoAlpha);"));
    Gen pk = GenOne(R"({"id":"c","type":"ColorPicker","props":{"label":"Pick"},"bind":{"value":"state.color"},"events":{"onChange":{"handler":true}}})");
    CHECK(pk.Has("if (ImGui::ColorPicker4(\"Pick\", &state.color.x)) {"));
    Gen p3 = GenOne(R"({"id":"c","type":"ColorPicker","props":{"label":"Pick"},"bind":{"value":"player.rgb"}})");
    CHECK(p3.Has("ImGui::ColorPicker3(\"Pick\", player.rgb);"));
}

TEST_CASE("codegen widgets: display") {
    SUBCASE("ProgressBar") {
        Gen g = GenOne(R"({"id":"p","type":"ProgressBar","props":{"fraction":0.5,"overlay":"half"}})");
        CHECK(g.Has("ImGui::ProgressBar(0.5f, ImVec2(0, 0), \"half\");"));
        Gen b = GenOne(R"({"id":"p","type":"ProgressBar","props":{"size":{"w":"fill","h":"auto"}},"bind":{"value":"player.progress"}})");
        CHECK(b.Has("ImGui::ProgressBar(player.progress, ImVec2(-FLT_MIN, 0));"));
        Gen e = GenOne(R"({"id":"p","type":"ProgressBar","props":{"fraction":{"expr":"player.health / 100"}}})");
        CHECK(e.Has("ImGui::ProgressBar((player.health / 100), ImVec2(0, 0));"));
    }
    SUBCASE("Image plain and with tint/border") {
        Gen g = GenOne(R"({"id":"img","type":"Image","props":{"size":{"w":32,"h":32}},"bind":{"texture":"player.icon"}})");
        CHECK(g.Has("ImGui::Image(player.icon, ImVec2(32 * imb_s, 32 * imb_s));"));
        Gen d = GenOne(R"({"id":"img","type":"Image","bind":{"texture":"player.icon"}})");
        CHECK(d.Has("ImGui::Image(player.icon, ImVec2(64 * imb_s, 64 * imb_s));"));
        Gen t = GenOne(R"({"id":"img","type":"Image","props":{"tint":"#ff0000ff","border":"#ffffffff","uv1":{"x":0.5,"y":0.5}},"bind":{"texture":"player.icon"}})");
        CHECK(t.Has("ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 1.0f);"));
        CHECK(t.Has("ImGui::ImageWithBg(player.icon, ImVec2(64 * imb_s, 64 * imb_s), ImVec2(0, 0), ImVec2(0.5f, 0.5f), ImVec4(0, 0, 0, 0), ImVec4("));
        CHECK(t.Has("#if IMGUI_VERSION_NUM >= 19190"));
        Gen old = GenOne(R"({"id":"img","type":"Image","props":{"tint":"#ff0000ff"},"bind":{"texture":"player.icon"}})", 18500);
        CHECK(old.Has("#if IMGUI_VERSION_NUM >= 19190"));
        CHECK(old.Has("ImGui::ImageWithBg(player.icon"));
        CHECK(old.Has("#else"));
        CHECK(old.Has("ImGui::Image(player.icon, ImVec2(64 * imb_s, 64 * imb_s), ImVec2(0, 0), ImVec2(1, 1), ImVec4("));
    }
    SUBCASE("ImageButton") {
        Gen g = GenOne(R"({"id":"ib","type":"ImageButton","props":{"size":{"w":24,"h":24}},"bind":{"texture":"player.icon"},"events":{"onClick":{"handler":true}}})");
        CHECK(g.Has("const bool ib_clicked = ImGui::ImageButton(\"ib\", player.icon, ImVec2(24 * imb_s, 24 * imb_s));"));
        CHECK(g.Has("const bool ib_clicked = ImGui::ImageButton(player.icon, ImVec2(24 * imb_s, 24 * imb_s));"));
        CHECK(g.Has("if (ib_clicked) {"));
        Gen old = GenOne(R"({"id":"ib","type":"ImageButton","props":{"size":{"w":24,"h":24}},"bind":{"texture":"player.icon"},"events":{"onClick":{"handler":true}}})", 18500);
        CHECK(old.Has("#if IMGUI_VERSION_NUM >= 18900"));
        CHECK(old.Has("const bool ib_clicked = ImGui::ImageButton(\"ib\", player.icon, ImVec2(24 * imb_s, 24 * imb_s));"));
        CHECK(old.Has("const bool ib_clicked = ImGui::ImageButton(player.icon, ImVec2(24 * imb_s, 24 * imb_s));"));
        CHECK(old.Has("if (ib_clicked) {"));
    }
    SUBCASE("Selectable and MenuItem") {
        Gen g = GenOne(R"({"id":"s","type":"Selectable","props":{"label":"Row"},"bind":{"selected":"cfg.sel"},"events":{"onClick":{"handler":true}}})");
        CHECK(g.Has("if (ImGui::Selectable(\"Row\", &cfg.sel)) {"));
        Gen f = GenOne(R"({"id":"s","type":"Selectable","props":{"label":"Row","flags":["SpanAllColumns"],"size":{"w":"fill","h":"auto"}}})");
        CHECK(f.Has("ImGui::Selectable(\"Row\", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(-FLT_MIN, 0));"));
        Gen m = GenOne(R"({"id":"m","type":"MenuItem","props":{"label":"Save","shortcut":"Ctrl+S"},"events":{"onClick":{"handler":true}}})");
        CHECK(m.Has("if (ImGui::MenuItem(\"Save\", \"Ctrl+S\")) {"));
        Gen ms = GenOne(R"({"id":"m","type":"MenuItem","props":{"label":"Show","enabled":false},"bind":{"selected":"cfg.sel"}})");
        CHECK(ms.Has("ImGui::MenuItem(\"Show\", nullptr, &cfg.sel, false);"));
        Gen mp = GenOne(R"({"id":"m","type":"MenuItem","props":{"label":"Quit"}})");
        CHECK(mp.Has("ImGui::MenuItem(\"Quit\");"));
    }
    SUBCASE("PlotLines / PlotHistogram") {
        Gen g = GenOne(R"({"id":"pl","type":"PlotLines","props":{"label":"FPS"},"bind":{"values":"player.history"}})");
        CHECK(g.Has("ImGui::PlotLines(\"FPS\", player.history, IM_ARRAYSIZE(player.history));"));
        Gen v = GenOne(R"({"id":"ph","type":"PlotHistogram","props":{"label":"##h","overlay":"avg","scaleMin":0,"scaleMax":1,"size":{"w":"fill","h":80}},"bind":{"values":"player.samples"}})");
        CHECK(v.Has("ImGui::PlotHistogram(\"##h\", player.samples.data(), (int)player.samples.size(), 0, \"avg\", 0.0f, 1.0f, ImVec2(-FLT_MIN, 80 * imb_s));"));
        Gen s = GenOne(R"({"id":"pl","type":"PlotLines","props":{"label":"L","size":{"w":200,"h":50}},"bind":{"values":"player.history"}})");
        CHECK(s.Has("ImGui::PlotLines(\"L\", player.history, IM_ARRAYSIZE(player.history), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(200 * imb_s, 50 * imb_s));"));
    }
}

TEST_CASE("codegen widgets: visibility, enabled and unknown types") {
    Gen g = GenOne(R"({"id":"b","type":"Button","props":{"label":"Go"},"visibleIf":"cfg.esp.enabled","enabledIf":"player.health > 0"})");
    CHECK(g.Has("if (cfg.esp.enabled) {"));
    CHECK(g.Has("ImGui::BeginDisabled(!(player.health > 0));"));
    CHECK(g.Has("ImGui::Button(\"Go\");"));
    CHECK(g.Has("ImGui::EndDisabled();"));
    Gen u = Generate(R"({"id":"x","type":"NoSuchWidget"})");
    CHECK(u.HasError());
}

TEST_CASE("codegen widgets: custom widgets from manifests") {
    WidgetCatalog catalog;
    Error err;
    REQUIRE_MESSAGE(catalog.AddManifest(ParseOrFail(R"json({
        "schemaVersion":1, "kind":"widget", "name":"Toggle", "category":"Essentials", "include":"imb_essentials.h",
        "props":[{"name":"label","type":"string","default":"Toggle"},{"name":"onColor","type":"color","default":"#2ecc71ff"}],
        "bindings":[{"name":"value","types":["bool"],"required":true}],
        "events":["onChange"], "container":false,
        "call":"imb::ess::Toggle({label}, {value}, {onColor})", "returns":"changed"})json"), &err), err.message);
    REQUIRE_MESSAGE(catalog.AddManifest(ParseOrFail(R"json({
        "schemaVersion":1, "kind":"widget", "name":"SectionCard", "category":"Essentials", "include":"imb_essentials.h",
        "props":[{"name":"title","type":"string","default":"Section"},{"name":"size","type":"size","default":{"w":"auto","h":"auto"}}],
        "container":true, "call":"imb::ess::SectionCard({title}, {size})", "callEnd":"imb::ess::EndSectionCard()", "returns":"void"})json"), &err), err.message);
    REQUIRE_MESSAGE(catalog.AddManifest(ParseOrFail(R"json({
        "schemaVersion":1, "kind":"widget", "name":"Knob", "category":"Essentials", "include":"knob.h",
        "props":[{"name":"label","type":"string","default":"Knob"},{"name":"min","type":"float","default":0},{"name":"max","type":"float","default":1},
                 {"name":"kind","type":"enum","values":["Dot","Wiper"],"default":"Dot"},{"name":"tint","type":"color","default":"#ffffffff"},
                 {"name":"size","type":"size","default":{"w":"auto","h":"auto"}}],
        "bindings":[{"name":"value","types":["float"],"required":true}],
        "events":["onChange"], "enumPrefix":"KnobKind_",
        "call":"Knob({label}, {value}, {min}, {max}, {kind}, {tint:u32}, {size}, {id})", "returns":"changed"})json"), &err), err.message);
    REQUIRE_MESSAGE(catalog.AddManifest(ParseOrFail(R"json({
        "schemaVersion":1, "kind":"widget", "name":"Foldout", "category":"Essentials", "include":"foldout.h",
        "props":[{"name":"label","type":"string","default":"Foldout"}], "events":["onOpen","onClose"],
        "container":true, "call":"BeginFoldout({label})", "callEnd":"EndFoldout()", "returns":"open"})json"), &err), err.message);

    SUBCASE("call template expansion, bindings and events") {
        Gen g = Generate(R"({"id":"tg","type":"Toggle","props":{"label":"ESP","onColor":"#ff0000ff"},"bind":{"value":"cfg.esp.enabled"},
                             "events":{"onChange":{"handler":true}}})", 19190, &catalog);
        INFO(g.all);
        CHECK_MESSAGE(!g.HasError(), g.Errors());
        CHECK(g.Has("ImGui::PushID(\"tg\");\n"));
        CHECK(g.Has("const bool tg_changed = imb::ess::Toggle(\"ESP\", &cfg.esp.enabled, ImVec4("));
        size_t pop = g.all.find("ImGui::PopID();\n", g.all.find("tg_changed = "));
        size_t fire = g.all.find("if (tg_changed) {");
        REQUIRE(pop != std::string::npos);
        REQUIRE(fire != std::string::npos);
        CHECK(pop < fire);
        CHECK(g.Has("Main_tg_OnChange("));
        CHECK(g.Has("imb_essentials.h"));
        Gen bare = Generate(R"({"id":"tg","type":"Toggle","props":{"label":"ESP"},"bind":{"value":"cfg.esp.enabled"}})", 19190, &catalog);
        CHECK(bare.Has("ImGui::PushID(\"tg\");\n"));
        CHECK(bare.Has("imb::ess::Toggle(\"ESP\", &cfg.esp.enabled, ImVec4("));
        CHECK_FALSE(bare.Has("if (imb::ess::Toggle"));
        CHECK_FALSE(bare.Has("tg_changed"));
    }
    SUBCASE("container with callEnd emits children between call and end") {
        Gen g = Generate(R"({"id":"card","type":"SectionCard","props":{"title":"Aim","size":{"w":"fill","h":200}},
                             "children":[{"id":"t","type":"Text","props":{"text":"inside"}}]})", 19190, &catalog);
        INFO(g.all);
        CHECK_MESSAGE(!g.HasError(), g.Errors());
        size_t push = g.all.find("ImGui::PushID(\"card\");");
        size_t a = g.all.find("imb::ess::SectionCard(\"Aim\", ImVec2(-FLT_MIN, 200 * imb_s));");
        size_t b = g.all.find("ImGui::TextUnformatted(\"inside\");");
        size_t c = g.all.find("imb::ess::EndSectionCard();");
        size_t pop = g.all.find("ImGui::PopID();", c);
        REQUIRE(push != std::string::npos);
        REQUIRE(a != std::string::npos);
        REQUIRE(b != std::string::npos);
        REQUIRE(c != std::string::npos);
        REQUIRE(pop != std::string::npos);
        CHECK(push < a);
        CHECK(a < b);
        CHECK(b < c);
    }
    SUBCASE("enum prefix, u32 colors, size and id placeholders") {
        Gen g = Generate(R"({"id":"k1","type":"Knob","props":{"label":"Gain","min":-1,"max":1,"kind":"Wiper","tint":"#ff0000ff"},"bind":{"value":"player.speed"}})", 19190, &catalog);
        INFO(g.all);
        CHECK_MESSAGE(!g.HasError(), g.Errors());
        CHECK(g.Has("Knob(\"Gain\", &player.speed, -1.0f, 1.0f, KnobKind_Wiper, IM_COL32(255, 0, 0, 255), ImVec2(0, 0), \"k1\");"));
    }
    SUBCASE("returns open with onOpen/onClose tracking") {
        Gen g = Generate(R"({"id":"fo","type":"Foldout","props":{"label":"More"},"events":{"onOpen":{"handler":true}},
                             "children":[{"id":"t","type":"Text","props":{"text":"x"}}]})", 19190, &catalog);
        INFO(g.all);
        CHECK_MESSAGE(!g.HasError(), g.Errors());
        CHECK(g.Has("static bool fo_was_open = false;"));
        CHECK(g.Has("const bool fo_open = BeginFoldout(\"More\");"));
        CHECK(g.Has("if (fo_open) {"));
        CHECK(g.Has("EndFoldout();"));
        CHECK(g.Has("ImGui::PushID(\"fo\");\n        static bool fo_was_open = false;"));
        CHECK(g.Has("EndFoldout();\n        }\n        ImGui::PopID();\n        if (fo_open && !fo_was_open) {"));
        CHECK(g.Has("if (fo_open && !fo_was_open) {"));
        CHECK(g.Has("Main_fo_OnOpen("));
        CHECK(g.Has("fo_was_open = fo_open;"));
        Gen plain = Generate(R"({"id":"fo","type":"Foldout","props":{"label":"More"},"children":[{"id":"t","type":"Text","props":{"text":"x"}}]})", 19190, &catalog);
        CHECK(plain.Has("if (BeginFoldout(\"More\")) {"));
        CHECK_FALSE(plain.Has("fo_was_open"));
    }
    SUBCASE("a lambda body in a call template is refused") {
        Error lerr;
        CHECK_FALSE(catalog.AddManifest(ParseOrFail(R"json({
            "schemaVersion":1, "kind":"widget", "name":"Lambda", "category":"Essentials", "include":"lambda.h",
            "props":[{"name":"label","type":"string","default":"L"}],
            "container":true, "call":"WithFrame({label}, [&]() {\n{children}\n})", "returns":"void"})json"), &lerr));
        CHECK(lerr.message.find("call") != std::string::npos);
    }
}

namespace {

const char* const kSinkNodes = R"json(
  {"id":"t1","type":"Text","props":{"text":"HP: {player.health:%.0f} / {player.level}","color":"#ff0000ff"}},
  {"id":"t2","type":"Text","props":{"text":"100% wrapped","wrapped":true,"disabled":true}},
  {"id":"t3","type":"Text","props":{"text":"bullet","bullet":true}},
  {"id":"st","type":"SeparatorText","props":{"text":"Section"}},
  {"id":"sep","type":"Separator"},{"id":"sp","type":"Spacing","props":{"count":2}},{"id":"nl","type":"NewLine"},
  {"id":"dm","type":"Dummy","props":{"size":{"w":10,"h":"50%"}}},
  {"id":"btn","type":"Button","props":{"label":"Play","repeat":true,"size":{"w":"fill","h":"auto"}},
   "events":{"onClick":{"actions":[{"type":"toggle","target":"cfg.esp.enabled"}],"handler":true}}},
  {"id":"sb","type":"SmallButton","props":{"label":"x"},"events":{"onClick":{"handler":true}}},
  {"id":"ab","type":"ArrowButton","props":{"dir":"Up"},"events":{"onClick":{"handler":true}}},
  {"id":"cb","type":"ColorButton","props":{"desc":"sw","color":"#00ff00ff","flags":["NoTooltip"],"size":{"w":20,"h":20}},"events":{"onClick":{"handler":true}}},
  {"id":"chk","type":"Checkbox","props":{"label":"ESP"},"bind":{"value":"cfg.esp.enabled"},"events":{"onChange":{"handler":true}}},
  {"id":"rd","type":"RadioButton","props":{"label":"One","value":1},"bind":{"value":"cfg.idx"}},
  {"id":"rde","type":"RadioButton","props":{"label":"Aim","value":1},"bind":{"value":"cfg.mode"},"events":{"onChange":{"handler":true}}},
  {"id":"cmb","type":"Combo","props":{"label":"Idx","items":["A","B"]},"bind":{"value":"cfg.idx"},"events":{"onChange":{"handler":true}}},
  {"id":"cme","type":"Combo","props":{"label":"Mode"},"bind":{"value":"cfg.mode"}},
  {"id":"cmw","type":"Combo","props":{"label":"Weapon","flags":["HeightLarge"]},"bind":{"value":"cfg.weapon"},"events":{"onChange":{"handler":true}}},
  {"id":"lb","type":"ListBox","props":{"label":"List","items":["x","y"],"heightItems":3},"bind":{"value":"cfg.idx"}},
  {"id":"lbe","type":"ListBox","props":{"label":"LW"},"bind":{"value":"cfg.weapon"}},
  {"id":"sf","type":"SliderFloat","props":{"label":"Speed","min":0,"max":10,"format":"%.2f","flags":["AlwaysClamp"]},"bind":{"value":"player.speed"},"events":{"onChange":{"handler":true}}},
  {"id":"sf3","type":"SliderFloat","props":{"label":"Vel","components":3},"bind":{"value":"player.vel"}},
  {"id":"sf2","type":"SliderFloat","props":{"label":"Pos","components":2},"bind":{"value":"player.pos"}},
  {"id":"sf4","type":"SliderFloat","props":{"label":"Col","components":4},"bind":{"value":"player.col"}},
  {"id":"si","type":"SliderInt","props":{"label":"Lvl","min":1,"max":99},"bind":{"value":"player.level"}},
  {"id":"si2","type":"SliderInt","props":{"label":"Ints","components":2},"bind":{"value":"player.ints"}},
  {"id":"vsf","type":"VSliderFloat","props":{"label":"##vsf"},"bind":{"value":"player.speed"}},
  {"id":"vsi","type":"VSliderInt","props":{"label":"##vsi","size":{"w":"auto","h":80}},"bind":{"value":"player.level"}},
  {"id":"df","type":"DragFloat","props":{"label":"HP","speed":0.5,"min":0,"max":100},"bind":{"value":"player.health"}},
  {"id":"df3","type":"DragFloat","props":{"label":"V3","components":3},"bind":{"value":"player.vel"}},
  {"id":"di","type":"DragInt","props":{"label":"DI"},"bind":{"value":"player.level"}},
  {"id":"di2","type":"DragInt","props":{"label":"DI2","components":2,"max":9},"bind":{"value":"player.ints"}},
  {"id":"it","type":"InputText","props":{"label":"Name","hint":"name"},"bind":{"value":"player.name"},"events":{"onEnter":{"handler":true},"onChange":{"handler":true}}},
  {"id":"itc","type":"InputText","props":{"label":"Tag"},"bind":{"value":"player.tag"}},
  {"id":"its","type":"InputText","props":{"label":"State"},"bind":{"value":"state.text"}},
  {"id":"itm","type":"InputTextMultiline","props":{"label":"Notes","size":{"w":"fill","h":100}},"bind":{"value":"player.tag"}},
  {"id":"itm2","type":"InputTextMultiline","props":{"label":"Notes2"},"bind":{"value":"player.name"}},
  {"id":"ii","type":"InputInt","props":{"label":"Lvl","step":2,"stepFast":20},"bind":{"value":"player.level"}},
  {"id":"ii2","type":"InputInt","props":{"label":"Ints","components":2},"bind":{"value":"player.ints"}},
  {"id":"if","type":"InputFloat","props":{"label":"HP","step":1},"bind":{"value":"player.health"}},
  {"id":"if3","type":"InputFloat","props":{"label":"Vel","components":3,"format":"%.1f"},"bind":{"value":"player.vel"}},
  {"id":"ce","type":"ColorEdit","props":{"label":"Col"},"bind":{"value":"player.col"}},
  {"id":"ce3","type":"ColorEdit","props":{"label":"Rgb"},"bind":{"value":"player.rgb"}},
  {"id":"ce4","type":"ColorEdit","props":{"label":"Rgba","alpha":false},"bind":{"value":"player.rgba"}},
  {"id":"cp","type":"ColorPicker","props":{"label":"Pick"},"bind":{"value":"state.color"},"events":{"onChange":{"handler":true}}},
  {"id":"pb","type":"ProgressBar","props":{"fraction":0.5,"overlay":"half"}},
  {"id":"pb2","type":"ProgressBar","props":{"size":{"w":"fill","h":"auto"},"overlay":"{player.health:%.0f} hp"},"bind":{"value":"player.progress"}},
  {"id":"img","type":"Image","props":{"size":{"w":32,"h":32}},"bind":{"texture":"player.icon"}},
  {"id":"img2","type":"Image","props":{"tint":"#ff0000ff","border":"#ffffffff"},"bind":{"texture":"player.icon"}},
  {"id":"ib","type":"ImageButton","props":{"size":{"w":24,"h":24},"bg":"#000000ff"},"bind":{"texture":"player.icon"},"events":{"onClick":{"handler":true}}},
  {"id":"sel","type":"Selectable","props":{"label":"Row","flags":["SpanAllColumns"]},"bind":{"selected":"cfg.sel"},"events":{"onClick":{"handler":true}}},
  {"id":"sel2","type":"Selectable","props":{"label":"Row2","size":{"w":"fill","h":"auto"}}},
  {"id":"mi","type":"MenuItem","props":{"label":"Save","shortcut":"Ctrl+S"},"bind":{"selected":"cfg.sel"},"events":{"onClick":{"handler":true}}},
  {"id":"mi2","type":"MenuItem","props":{"label":"Quit","enabled":false}},
  {"id":"pl","type":"PlotLines","props":{"label":"FPS","overlay":"avg","scaleMin":0,"scaleMax":1,"size":{"w":"fill","h":80}},"bind":{"values":"player.history"}},
  {"id":"ph","type":"PlotHistogram","props":{"label":"##h"},"bind":{"values":"player.samples"}},
  {"id":"un","type":"SliderFloat","props":{"label":"Unbound"}},
  {"id":"vis","type":"Button","props":{"label":"Go"},"visibleIf":"cfg.esp.enabled && player.health > 0"}
)json";

std::string TempDir() {
    const char* t = std::getenv("TMPDIR");
    if (!t || !*t) t = std::getenv("TEMP");
    if (!t || !*t) t = "/tmp";
    return t;
}

bool WriteFile(const std::string& path, const std::string& content) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return true;
}

bool FileExists(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool CompileCheck(const Gen& g, const std::string& imgui_dir, const std::string& tag) {
    std::string dir = TempDir() + "/imforge_codegen_widgets_" + tag;
#ifdef _WIN32
    std::string mk = "mkdir \"" + dir + "\" >nul 2>&1";
#else
    std::string mk = "mkdir -p \"" + dir + "\"";
#endif
    int mk_rc = std::system(mk.c_str());
    (void)mk_rc;
    REQUIRE(WriteFile(dir + "/types_stub.h", kTypesStubHeader));
    std::string cpp_path;
    for (const FileOut& f : g.result.files) {
        std::string p = dir + "/" + f.path;
        REQUIRE(WriteFile(p, f.content));
        if (f.path.size() > 4 && f.path.compare(f.path.size() - 4, 4, ".cpp") == 0) cpp_path = p;
    }
    if (cpp_path.empty()) {
        cpp_path = dir + "/all.cpp";
        std::string tu;
        for (const FileOut& f : g.result.files) tu += "#include \"" + f.path + "\"\n";
        REQUIRE(WriteFile(cpp_path, tu));
    }
    std::string cmd = "g++ -std=c++17 -fsyntax-only -Wall -Wextra -I\"" + dir + "\" -I\"" + imgui_dir + "\" \"" + cpp_path +
                      "\" 2> \"" + dir + "/errors.txt\"";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        FILE* f = std::fopen((dir + "/errors.txt").c_str(), "rb");
        std::string err;
        if (f) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) err.append(buf, n);
            std::fclose(f);
        }
        MESSAGE("compile check failed for " << tag << ":\n" << err.substr(0, 6000));
    }
    return rc == 0;
}

}  // namespace

TEST_CASE("codegen widgets: generated panel compiles against pinned ImGui versions") {
    if (std::system("g++ --version > /dev/null 2>&1") != 0) {
        MESSAGE("g++ not available; compile check skipped");
        return;
    }
    const char* home = std::getenv("HOME");
    if (!home) {
        MESSAGE("HOME not set; compile check skipped");
        return;
    }
    std::string deps = std::string(home) + "/deps";
    Gen modern = Generate(kSinkNodes, 19190);
    INFO(modern.all);
    REQUIRE_MESSAGE(!modern.HasError(), modern.Errors());
    Gen legacy = Generate(kSinkNodes, 18500);
    REQUIRE_MESSAGE(!legacy.HasError(), legacy.Errors());
    int checked = 0;
    if (FileExists(deps + "/imgui-v1.91.9b/imgui.h")) {
        CHECK(CompileCheck(modern, deps + "/imgui-v1.91.9b", "modern_19191"));
        CHECK(CompileCheck(legacy, deps + "/imgui-v1.91.9b", "legacy_on_19191"));
        ++checked;
    }
    if (FileExists(deps + "/imgui-v1.85/imgui.h")) {
        CHECK(CompileCheck(legacy, deps + "/imgui-v1.85", "legacy_on_18500"));
        ++checked;
    }
    for (const char* ver : {"imgui-v1.89.9", "imgui-v1.92.1", "imgui-v1.93.0"}) {
        if (!FileExists(deps + "/" + ver + "/imgui.h")) continue;
        CHECK(CompileCheck(modern, deps + "/" + ver, std::string("modern_") + ver));
        CHECK(CompileCheck(legacy, deps + "/" + ver, std::string("legacy_") + ver));
        ++checked;
    }
    if (checked == 0) MESSAGE("no pinned ImGui checkout under " << deps << "; compile check skipped");
}
