#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <string>
#include <vector>

#include "codegen.h"
#include "codegen_test_util.h"
#include "imforge/imforge_core.h"

using namespace imforge;
using namespace imforge::codegen;
using namespace codegen_test;

namespace {

const char* kProjectJson = R"({
  "schemaVersion": 1, "kind": "project", "name": "CheatMenu",
  "imgui": { "version": "1.91.9", "branch": "docking" },
  "platform": { "window": "win32", "renderer": "dx11" },
  "features": { "animations": true, "drawlistFx": true, "shaderFx": true, "iconFonts": true, "dpiScaling": true, "docking": false },
  "codegen": { "cppStandard": 17, "namespace": "ui", "splitHeader": true, "functionCase": "pascal", "variableCase": "snake",
               "indent": 4, "useTabs": false, "outputDir": "generated", "markers": true, "dpiMode": "fontSize", "baseFontSize": 13,
               "runtimeHeader": "imb_runtime.h", "stdString": true },
  "binding": { "types": ["Player", "Settings"], "typesJson": "types.json" },
  "live": { "enabled": true, "transport": "tcp", "tcpPort": 7777 },
  "fonts": [ { "name": "Default", "file": "fonts/Inter-Regular.ttf", "size": 16, "ranges": "default", "mergeIcons": "fontawesome6" },
             { "name": "Bold", "file": "fonts/Inter-Bold.ttf", "size": 16 } ],
  "themes": [ "themes/Dark.theme.json" ], "activeTheme": "Dark",
  "styleClasses": { "danger": { "colors": { "Button": "#c0392bff" }, "vars": { "FrameRounding": 6 } } },
  "panels": [ "panels/MainMenu.imb.json" ], "components": [ "components/LabeledSlider.imc.json" ]
})";

const char* kPanelJson = R"({
  "schemaVersion": 1, "kind": "panel", "name": "MainMenu",
  "params": [ { "name": "player", "type": "Player" }, { "name": "cfg", "type": "Settings" } ],
  "state": [ { "name": "selectedTab", "type": "int", "default": 0 }, { "name": "open", "type": "bool", "default": true },
             { "name": "accent", "type": "color", "default": "#ff0000ff" }, { "name": "offset", "type": "vec2", "default": { "x": 1.5, "y": 2 } } ],
  "root": { "id": "window", "type": "Window",
    "props": { "title": "Main Menu", "flags": ["NoCollapse"], "initialSize": {"x": 520, "y": 380}, "sizeCond": "FirstUseEver" },
    "bind": { "open": "state.open" },
    "children": [
      { "id": "btn_play", "type": "Button", "props": { "label": "Play", "size": { "w": "fill", "h": "auto" } },
        "visibleIf": "cfg.esp.enabled", "enabledIf": "player.health > 0", "tooltip": "Starts the game",
        "events": { "onClick": { "actions": [ { "type": "toggle", "target": "cfg.esp.enabled" } ], "handler": true } } },
      { "id": "hp_text", "type": "Text", "props": { "text": "HP: {player.health:%.0f} / {player.max_hp}" } },
      { "id": "row", "type": "HStack", "children": [
          { "id": "a", "type": "Button", "props": { "label": "A", "size": { "w": "fill", "h": "auto" } } },
          { "id": "b", "type": "Button", "props": { "label": "B", "size": { "w": 100, "h": "auto" } } } ] },
      { "id": "hp", "type": "Component", "component": "LabeledSlider", "props": { "label": "Health", "value": "player.health", "min": 0, "max": 100 } },
      { "id": "card", "type": "Component", "component": "Card", "props": { "title": "Stats" },
        "slots": { "content": [ { "id": "hint", "type": "Text", "props": { "text": "0-100" } } ] } },
      { "id": "tabs", "type": "TabBar", "children": [
          { "id": "tab_a", "type": "Tab", "props": { "label": "Aim" }, "children": [
              { "id": "chk", "type": "Checkbox", "props": { "label": "Enabled" }, "bind": { "value": "cfg.esp.enabled" } } ] },
          { "id": "tab_b", "type": "Tab", "props": { "label": "Visuals" }, "children": [
              { "id": "sl", "type": "SliderFloat", "props": { "label": "Health", "min": 0, "max": 100 }, "bind": { "value": "player.health" } },
              { "id": "sel", "type": "Button", "props": { "label": "Select" },
                "events": { "onClick": { "actions": [ { "type": "setTab", "target": "tabs", "value": "tab_a" } ] } } } ] } ] },
      { "id": "grid", "type": "Grid", "props": { "columns": 2, "flags": ["Borders", "HighlightHoveredColumn"] }, "children": [
          { "id": "g1", "type": "Text", "props": { "text": "x" } }, { "id": "g2", "type": "Text", "props": { "text": "y" } } ] },
      { "id": "child", "type": "Child", "props": { "border": true, "size": { "w": "fill", "h": 120 } }, "children": [
          { "id": "name", "type": "InputText", "props": { "label": "Name" }, "bind": { "value": "player.name" } } ] },
      { "id": "custom_1", "type": "Code", "props": { "code": "ImGui::Text(\"hand written\");" } },
      { "id": "styled", "type": "Button", "props": { "label": "Danger" },
        "style": { "colors": { "Button": "#c0392bff" }, "vars": { "FrameRounding": 6 }, "classes": ["danger"] }, "font": "Bold",
        "anim": { "transitions": [ { "prop": "color.Button", "on": "hover", "to": "#27ae60ff", "duration": 0.15, "ease": "OutCubic" } ],
                  "enter": { "preset": "FadeSlideUp", "duration": 0.25 } },
        "fx": [ { "type": "shadow", "size": 12, "color": "#00000080" } ] },
      { "id": "icon_btn", "type": "Button", "props": { "label": "{icon:fa-play} Play" } },
      { "id": "dup1", "type": "Button", "props": { "label": "Same" } },
      { "id": "dup2", "type": "Button", "props": { "label": "Same" } }
    ] }
})";

const char* kSliderJson = R"({
  "schemaVersion": 1, "kind": "component", "name": "LabeledSlider",
  "params": [ { "name": "label", "type": "string", "default": "Value" }, { "name": "value", "type": "binding<float>" },
              { "name": "min", "type": "float", "default": 0 }, { "name": "max", "type": "float", "default": 1 } ],
  "root": { "id": "row", "type": "HStack", "children": [
     { "id": "lbl", "type": "Text", "props": { "text": { "param": "label" } } },
     { "id": "sl", "type": "SliderFloat", "props": { "label": "##v", "min": { "param": "min" }, "max": { "param": "max" } },
       "bind": { "value": { "param": "value" } } } ] }
})";

const char* kCardJson = R"({
  "schemaVersion": 1, "kind": "component", "name": "Card",
  "params": [ { "name": "title", "type": "string", "default": "Card" } ],
  "slots": [ "content" ],
  "root": { "id": "box", "type": "Child", "props": { "border": true, "size": { "w": "fill", "h": 100 } }, "children": [
     { "id": "head", "type": "Text", "props": { "text": { "param": "title" } } },
     { "id": "slot_content", "type": "Slot", "props": { "name": "content" } },
     { "id": "foot", "type": "Separator" } ] }
})";

const char* kThemeJson = R"({ "schemaVersion": 1, "kind": "theme", "name": "Dark",
  "colors": { "Text": "#ffffffff", "TabSelected": "#336699ff", "WindowBg": "#101010ff" },
  "vars": { "FrameRounding": 4, "WindowPadding": { "x": 8, "y": 8 } } })";

const char* kTypesJson = R"({ "schemaVersion": 1, "kind": "types",
  "structs": {
    "Player": { "header": "Player.h", "fields": [ { "name": "health", "type": "float", "ctype": "Float" },
                                                  { "name": "max_hp", "type": "int", "ctype": "I32" },
                                                  { "name": "name", "type": "std::string", "ctype": "StdString" } ] },
    "Esp": { "header": "Settings.h", "fields": [ { "name": "enabled", "type": "bool", "ctype": "Bool" } ] },
    "Settings": { "header": "Settings.h", "fields": [ { "name": "esp", "type": "Esp", "ctype": "Struct" } ] }
  }, "enums": {} })";

struct Fixture {
    Project project;
    Json types;
    Options opts;
    Fixture() {
        Error err;
        Json j;
        REQUIRE(ParseJson(kProjectJson, j));
        REQUIRE_MESSAGE(ProjectFromJson(j, project, &err), err.message);
        Panel panel;
        REQUIRE(ParseJson(kPanelJson, j));
        REQUIRE_MESSAGE(PanelFromJson(j, panel, &err), err.message);
        project.panels.push_back(panel);
        Component slider, card;
        REQUIRE(ParseJson(kSliderJson, j));
        REQUIRE_MESSAGE(ComponentFromJson(j, slider, &err), err.message);
        REQUIRE(ParseJson(kCardJson, j));
        REQUIRE_MESSAGE(ComponentFromJson(j, card, &err), err.message);
        project.components.push_back(slider);
        project.components.push_back(card);
        Theme theme;
        REQUIRE(ParseJson(kThemeJson, j));
        REQUIRE_MESSAGE(ThemeFromJson(j, theme, &err), err.message);
        project.themes.push_back(theme);
        REQUIRE(ParseJson(kTypesJson, types));
        opts = OptionsFromProject(project);
        opts.types = &types;
    }
};

std::vector<std::string> Lines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        out.push_back(s.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b);
}

struct MarkerLine {
    std::string kind, id, hash;
};

bool ParseMarker(const std::string& line, MarkerLine& m) {
    std::string t = Trim(line);
    if (t.compare(0, 8, "// @imb:") != 0) return false;
    size_t sp = t.find(' ', 8);
    m.kind = t.substr(8, sp == std::string::npos ? std::string::npos : sp - 8);
    size_t idp = t.find("id=");
    if (idp != std::string::npos) {
        size_t e = t.find(' ', idp);
        m.id = t.substr(idp + 3, e == std::string::npos ? std::string::npos : e - idp - 3);
    }
    size_t hp = t.find("h=");
    if (hp != std::string::npos) m.hash = t.substr(hp + 2, 8);
    return true;
}

// every begin/locked hash must match its normalized body (children become @child:<id>)
int CheckHashes(const std::string& text) {
    std::vector<std::string> lines = Lines(text);
    int checked = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        MarkerLine m;
        if (!ParseMarker(lines[i], m) || (m.kind != "begin" && m.kind != "locked")) continue;
        std::string body;
        int depth = 0;
        size_t j = i + 1;
        for (; j < lines.size(); ++j) {
            MarkerLine x;
            bool is_marker = ParseMarker(lines[j], x);
            if (is_marker && (x.kind == "begin" || x.kind == "locked")) {
                if (depth == 0) body += "@child:" + x.id + "\n";
                ++depth;
                continue;
            }
            if (is_marker && x.kind == "end") {
                if (depth == 0) break;
                --depth;
                continue;
            }
            if (depth == 0) body += lines[j] + "\n";
        }
        REQUIRE(j < lines.size());
        CHECK_MESSAGE(Fnv1a32Hex(NormalizeForHash(body)) == m.hash, "hash mismatch for block " << m.id);
        ++checked;
    }
    return checked;
}

}  // namespace

TEST_CASE("hash helpers") {
    CHECK(Fnv1a32Hex("") == "811c9dc5");
    CHECK(Fnv1a32Hex("a") == "e40c292c");
    CHECK(NormalizeForHash("  a\r\n\tb\n   c") == "a\nb\nc");
}

TEST_CASE("tokenizer") {
    std::vector<Token> t = Tokenize("ImGui::Button(\"Play\", ImVec2(-FLT_MIN, 0)); // c\n#if X\nx = -5.0f; @child:btn");
    REQUIRE(t.size() >= 14);
    CHECK(t[0].kind == Token::Kind::Ident);
    CHECK(t[1].text == "::");
    CHECK(t[4].kind == Token::Kind::String);
    CHECK(t[4].text == "\"Play\"");
    bool saw_directive = false, saw_child = false, saw_neg = false;
    for (const Token& x : t) {
        saw_directive = saw_directive || (x.kind == Token::Kind::Directive && x.text == "#if X");
        saw_child = saw_child || (x.kind == Token::Kind::ChildRef && x.text == "@child:btn");
        saw_neg = saw_neg || (x.kind == Token::Kind::Number && x.text == "-5.0f");
    }
    CHECK(saw_directive);
    CHECK(saw_child);
    CHECK(saw_neg);
}

TEST_CASE("EmitSize follows the sizing table") {
    Options o;
    SizeSpec a, px, fill, pct, fm;
    px.mode = SizeSpec::Mode::Px;
    px.value = 120;
    fill.mode = SizeSpec::Mode::Fill;
    pct.mode = SizeSpec::Mode::Percent;
    pct.value = 50;
    fm.mode = SizeSpec::Mode::FillMinus;
    fm.value = 20;
    CHECK(EmitSize(a, a, o) == "ImVec2(0, 0)");
    CHECK(EmitSize(px, fill, o) == "ImVec2(120 * imb_s, -FLT_MIN)");
    CHECK(EmitSize(pct, fm, o) == "ImVec2(ImGui::GetContentRegionAvail().x * (50 / 100.0f), ImGui::GetContentRegionAvail().y - 20 * imb_s)");
    CHECK(EmitSize(fm, pct, o) == "ImVec2(std::max(ImGui::GetContentRegionAvail().x - 20 * imb_s, 1.0f), ImGui::GetContentRegionAvail().y * (50 / 100.0f))");
    o.settings.dpi_mode = "none";
    CHECK(EmitSize(px, px, o) == "ImVec2(120, 120)");
    o.settings.dpi_mode = "variable";
    CHECK(EmitSize(px, a, o) == "ImVec2(120 * imb_scale, 0)");
}

TEST_CASE("panel generation") {
    Fixture fx;
    Result r = GeneratePanel(fx.project, fx.project.panels[0], fx.opts);
    for (const Diagnostic& d : r.diagnostics) CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.message);
    REQUIRE(r.files.size() == 2);
    const FileOut* h = r.Find("MainMenu.h");
    const FileOut* cpp = r.Find("MainMenu.cpp");
    REQUIRE(h);
    REQUIRE(cpp);
    const std::string& src = cpp->content;

    SUBCASE("header shape") {
        CHECK(Contains(h->content, "// @imb:file panel=MainMenu schema=1"));
        CHECK(Contains(h->content, "#pragma once"));
        CHECK(Contains(h->content, "struct Player;"));
        CHECK(Contains(h->content, "struct Settings;"));
        CHECK(Contains(h->content, "namespace ui {"));
        CHECK(Contains(h->content, "struct MainMenuState {"));
        CHECK(Contains(h->content, "int selected_tab = 0;"));
        CHECK(Contains(h->content, "bool open = true;"));
        CHECK(Contains(h->content, "ImVec4 accent = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);"));
        CHECK(Contains(h->content, "ImVec2 offset = ImVec2(1.5f, 2);"));
        CHECK(Contains(h->content, "int imb_tab_tabs = -1;"));
        CHECK(Contains(h->content, "void DrawMainMenu(MainMenuState& state, Player& player, Settings& cfg);"));
        CHECK(Contains(h->content, "// @imb:user id=header_extra"));
        CHECK(Contains(h->content, "// @imb:end id=header_extra"));
    }
    SUBCASE("markers: every node has begin/end and hashes verify") {
        std::vector<std::string> ids;
        CollectIds(fx.project.panels[0].root, ids);
        for (const std::string& id : ids) {
            CHECK_MESSAGE(Contains(src, "// @imb:begin id=" + id + " h="), id);
            CHECK_MESSAGE(Contains(src, "// @imb:end id=" + id + "\n"), id);
        }
        CHECK(Contains(src, "// @imb:locked id=custom_1 h="));
        CHECK(Contains(src, "// @imb:user id=includes"));
        CHECK(Contains(src, "// @imb:user id=file_top"));
        CHECK(Contains(src, "// @imb:user id=before_draw"));
        CHECK(Contains(src, "// @imb:user id=after_draw"));
        CHECK(Contains(src, "// @imb:user id=btn_play.onClick"));
        CHECK(CheckHashes(src) >= static_cast<int>(ids.size()));
    }
    SUBCASE("determinism") {
        Result again = GeneratePanel(fx.project, fx.project.panels[0], fx.opts);
        REQUIRE(again.files.size() == r.files.size());
        for (size_t i = 0; i < r.files.size(); ++i) CHECK(again.files[i].content == r.files[i].content);
        CHECK(again.literals.size() == r.literals.size());
    }
    SUBCASE("handler, state struct, actions") {
        CHECK(Contains(src, "void MainMenu_btn_play_OnClick(MainMenuState& state, Player& player, Settings& cfg) {"));
        CHECK(Contains(src, "cfg.esp.enabled = !cfg.esp.enabled;"));
        CHECK(Contains(src, "MainMenu_btn_play_OnClick(state, player, cfg);"));
        CHECK(Contains(src, "const float imb_s = ImGui::GetFontSize() / 13.0f; (void)imb_s;"));
        CHECK(Contains(src, "ImGui::SetNextWindowSize(ImVec2(520 * imb_s, 380 * imb_s), ImGuiCond_FirstUseEver);"));
        CHECK(Contains(src, "if (ImGui::Begin(\"Main Menu\", &state.open, ImGuiWindowFlags_NoCollapse)) {"));
        CHECK(Contains(src, "ImGui::End();"));
        CHECK(Contains(src, "state.imb_tab_tabs = 0;"));
        CHECK(Contains(src, "const int tabs_pending = state.imb_tab_tabs;\n                state.imb_tab_tabs = -1;"));
        CHECK(Contains(src, "(tabs_pending == 0 ? ImGuiTabItemFlags_SetSelected : 0)"));
    }
    SUBCASE("visibleIf / enabledIf / tooltip") {
        CHECK(Contains(src, "if (cfg.esp.enabled) {"));
        CHECK(Contains(src, "ImGui::BeginDisabled(!(player.health > 0));"));
        CHECK(Contains(src, "ImGui::EndDisabled();"));
        CHECK(Contains(src, "if (ImGui::IsItemHovered()) ImGui::SetTooltip(\"Starts the game\");"));
        CHECK(Contains(src, "ImVec2(-FLT_MIN, 0)"));
    }
    SUBCASE("interpolation") {
        CHECK(Contains(src, "ImGui::Text(\"HP: %.0f / %d\", (double)player.health, (int)player.max_hp);"));
        CHECK(Contains(src, "ICON_FA_PLAY \" Play\""));
        CHECK(Contains(src, "#include \"IconsFontAwesome6.h\""));
    }
    SUBCASE("HStack fill math and Grid cells") {
        CHECK(Contains(src, "const float row_fill_w = std::max((ImGui::GetContentRegionAvail().x - 100 * imb_s - ImGui::GetStyle().ItemSpacing.x * 1) / 1.0f, 1.0f);"));
        CHECK(Contains(src, "ImVec2(row_fill_w, 0)"));
        CHECK(Contains(src, "ImGui::SameLine();"));
        CHECK(Contains(src, "int grid_flags = ImGuiTableFlags_Borders;\n#if IMGUI_VERSION_NUM >= 19000\n            grid_flags |= ImGuiTableFlags_HighlightHoveredColumn;\n#endif\n"));
        CHECK(Contains(src, "if (ImGui::BeginTable(\"grid\", 2, grid_flags)) {"));
        CHECK(Contains(src, "ImGui::TableNextColumn();"));
        CHECK(Contains(src, "ImGui::PushID(1);"));
        CHECK(Contains(src, "ImGui::EndTable();"));
    }
    SUBCASE("child, tabs, locked block, label collisions") {
        CHECK(Contains(src, "ImGui::BeginChild(\"child\", ImVec2(-FLT_MIN, 120 * imb_s), ImGuiChildFlags_Borders, 0)"));
        CHECK(Contains(src, "ImGui::EndChild();"));
        CHECK(Contains(src, "if (ImGui::BeginTabBar(\"tabs\", 0)) {"));
        CHECK(Contains(src, "ImGui::BeginTabItem(\"Aim\", nullptr,"));
        CHECK(Contains(src, "ImGui::EndTabBar();"));
        CHECK(Contains(src, "void MainMenu_custom_1_Block(MainMenuState& state, Player& player, Settings& cfg) {"));
        CHECK(Contains(src, "ImGui::Text(\"hand written\");"));
        CHECK(Contains(src, "MainMenu_custom_1_Block(state, player, cfg);"));
        CHECK(Contains(src, "\"Same\" \"###dup1\""));
        CHECK(Contains(src, "\"Same\" \"###dup2\""));
    }
    SUBCASE("style, font, anim, fx") {
        CHECK(Contains(src, "ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.753f, 0.224f, 0.169f, 1.0f));"));
        CHECK(Contains(src, "ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * imb_s);"));
        CHECK(Contains(src, "Style_danger_Push();"));
        CHECK(Contains(src, "Style_danger_Pop();"));
        CHECK(Contains(src, "ImGui::PushFont(Font_Bold);"));
        CHECK(Contains(src, "ImGui::PopFont();"));
        CHECK(Contains(src, "imb::Transition(ImGui::GetID(\"styled/t0\"), ImGui::GetStateStorage()->GetBool(ImGui::GetID(\"styled/hover\")), 0.15f, imb::Ease::OutCubic)"));
        CHECK(Contains(src, "if (imb::BeginEnter(ImGui::GetID(\"styled\"), true, 0.25f, imb::Preset::FadeSlideUp)) {"));
        CHECK(Contains(src, "imb::EndEnter();"));
        CHECK(Contains(src, "imb::Shadow(fx_dl, fx_min, fx_max, ImGui::GetStyle().FrameRounding, IM_COL32(0, 0, 0, 128), 12 * imb_s, ImVec2(0, 2));"));
        CHECK(Contains(src, "#include \"imb_runtime.h\""));
        CHECK(Contains(src, "#include \"imforge_styles.h\""));
        CHECK(Contains(src, "#include \"imforge_fonts.h\""));
    }
    SUBCASE("component instances") {
        CHECK(Contains(src, "ImGui::PushID(\"hp\");"));
        CHECK(Contains(src, "LabeledSlider(\"Health\", &player.health, 0.0f, 100.0f);"));
        CHECK(Contains(src, "const bool card_open = BeginCard(\"Stats\");"));
        CHECK(Contains(src, "EndCard(card_open, \"Stats\");"));
        CHECK(Contains(src, "ImGui::TextUnformatted(\"0-100\");"));
        CHECK(Contains(src, "#include \"imforge_components.h\""));
    }
    SUBCASE("literal refs point at literal tokens") {
        std::vector<Token> toks = Tokenize(src);
        REQUIRE(!r.literals.empty());
        bool saw_label = false, saw_title = false, saw_flags = false, saw_min = false;
        for (const LiteralRef& ref : r.literals) {
            CHECK(ref.file == "MainMenu.cpp");
            REQUIRE(ref.token_index >= 0);
            REQUIRE(ref.token_index < static_cast<int>(toks.size()));
            const Token& t = toks[static_cast<size_t>(ref.token_index)];
            bool literal = t.kind == Token::Kind::String || t.kind == Token::Kind::Number ||
                           (t.kind == Token::Kind::Ident && (t.text.compare(0, 5, "ImGui") == 0 || t.text == "true" || t.text == "false"));
            CHECK_MESSAGE(literal, ref.node_id << "." << ref.prop << " -> '" << t.text << "'");
            CHECK(ref.block_token_index >= 0);
            if (ref.node_id == "btn_play" && ref.prop == "label") saw_label = t.text == "\"Play\"";
            if (ref.node_id == "window" && ref.prop == "title") saw_title = t.text == "\"Main Menu\"";
            if (ref.node_id == "window" && ref.prop == "flags") saw_flags = t.text == "ImGuiWindowFlags_NoCollapse";
            if (ref.node_id == "sl" && ref.prop == "min") saw_min = t.text == "0.0f";
        }
        CHECK(saw_label);
        CHECK(saw_title);
        CHECK(saw_flags);
        CHECK(saw_min);
    }
}

TEST_CASE("flags newer than the target are omitted with a warning") {
    Fixture fx;
    Options o = fx.opts;
    o.imgui_version = 18500;
    Result r = GeneratePanel(fx.project, fx.project.panels[0], o);
    const FileOut* cpp = r.Find("MainMenu.cpp");
    REQUIRE(cpp);
    CHECK(Contains(cpp->content, "grid_flags |= ImGuiTableFlags_HighlightHoveredColumn;"));
    bool noted = false;
    for (const Diagnostic& d : r.diagnostics)
        noted = noted || (d.level == Diagnostic::Level::Info && Contains(d.message, "HighlightHoveredColumn"));
    CHECK(noted);
    CHECK(Contains(cpp->content, "#if IMGUI_VERSION_NUM >= 18600"));
    CHECK(Contains(cpp->content, "ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !(player.health > 0));"));
    size_t end_if = cpp->content.find("#if IMGUI_VERSION_NUM >= 18600\n                ImGui::EndDisabled();\n#else\n");
    REQUIRE(end_if != std::string::npos);
    size_t pop_flag = cpp->content.find("ImGui::PopItemFlag();\n#endif\n", end_if);
    CHECK(pop_flag != std::string::npos);
    CHECK(!Contains(cpp->content, "#endif\n                ImGui::PopItemFlag();"));
    CHECK(Contains(cpp->content, "#include \"imgui_internal.h\""));
    CHECK(Contains(cpp->content, "#if IMGUI_VERSION_NUM >= 19110"));
    CHECK(Contains(cpp->content, "ImGui::BeginChild(\"child\", ImVec2(-FLT_MIN, 120 * imb_s), true, 0);"));
}

TEST_CASE("single header mode uses inline functions") {
    Fixture fx;
    Options o = fx.opts;
    o.settings.split_header = false;
    Result r = GeneratePanel(fx.project, fx.project.panels[0], o);
    REQUIRE(r.files.size() == 1);
    CHECK(r.files[0].path == "MainMenu.h");
    CHECK(Contains(r.files[0].content, "#pragma once"));
    CHECK(Contains(r.files[0].content, "inline void DrawMainMenu(MainMenuState& state, Player& player, Settings& cfg) {"));
    CHECK(Contains(r.files[0].content, "inline void MainMenu_btn_play_OnClick("));
    CHECK(CheckHashes(r.files[0].content) > 0);
}

TEST_CASE("naming cases") {
    Fixture fx;
    Options o = fx.opts;
    o.settings.function_case = "snake";
    o.settings.variable_case = "camel";
    Result r = GeneratePanel(fx.project, fx.project.panels[0], o);
    const FileOut* h = r.Find("MainMenu.h");
    REQUIRE(h);
    CHECK(Contains(h->content, "void draw_main_menu(MainMenuState& state, Player& player, Settings& cfg);"));
    CHECK(Contains(h->content, "int selectedTab = 0;"));
    CHECK(Contains(r.Find("MainMenu.cpp")->content, "main_menu_btn_play_on_click(state, player, cfg);"));
}

TEST_CASE("components") {
    Fixture fx;
    Result r = GenerateComponents(fx.project, fx.opts);
    for (const Diagnostic& d : r.diagnostics) CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.message);
    const FileOut* h = r.Find("imforge_components.h");
    const FileOut* cpp = r.Find("imforge_components.cpp");
    REQUIRE(h);
    REQUIRE(cpp);
    CHECK(Contains(h->content, "void LabeledSlider(const char* label, float* value, float min, float max);"));
    CHECK(Contains(h->content, "bool BeginCard(const char* title);"));
    CHECK(Contains(h->content, "void EndCard(bool open, const char* title);"));
    CHECK(Contains(cpp->content, "void LabeledSlider(const char* label, float* value, float min, float max) {"));
    CHECK(Contains(cpp->content, "ImGui::Text(\"%s\", label);"));
    CHECK(Contains(cpp->content, "ImGui::SliderFloat(\"##v\", "));
    CHECK(Contains(cpp->content, ", min, max"));
    CHECK(Contains(cpp->content, "bool BeginCard(const char* title) {"));
    CHECK(Contains(cpp->content, "return false;"));
    CHECK(Contains(cpp->content, "return true;"));
    CHECK(Contains(cpp->content, "void EndCard(bool open, const char* title) {"));
    CHECK(Contains(cpp->content, "if (!open) return;"));
    CHECK(Contains(cpp->content, "ImGui::Separator();"));
    CHECK(Contains(cpp->content, "ImGui::EndChild();"));
}

TEST_CASE("theme and styles") {
    Fixture fx;
    Result r = GenerateTheme(fx.project, fx.project.themes[0], fx.opts);
    REQUIRE(r.files.size() == 1);
    CHECK(r.files[0].path == "imforge_theme_Dark.h");
    const std::string& t = r.files[0].content;
    CHECK(Contains(t, "inline void ApplyTheme_Dark(ImGuiStyle& style) {"));
    CHECK(Contains(t, "colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);"));
    CHECK(Contains(t, "colors[ImGuiCol_TabSelected] = ImVec4(0.2f, 0.4f, 0.6f, 1.0f);"));
    CHECK(Contains(t, "#if IMGUI_VERSION_NUM >= 19090\n    colors[ImGuiCol_TabSelected]"));
    CHECK(Contains(t, "#else\n    colors[ImGuiCol_TabActive]"));
    CHECK(Contains(t, "style.FrameRounding = 4.0f;"));
    CHECK(Contains(t, "style.WindowPadding = ImVec2(8, 8);"));
    Options old = fx.opts;
    old.imgui_version = 18500;
    Result r2 = GenerateTheme(fx.project, fx.project.themes[0], old);
    const std::string& t2 = r2.files[0].content;
    CHECK(Contains(t2, "#if IMGUI_VERSION_NUM >= 19090\n    colors[ImGuiCol_TabSelected]"));
    CHECK(Contains(t2, "#else\n    colors[ImGuiCol_TabActive]"));
    Result s = GenerateStyles(fx.project, fx.opts);
    REQUIRE(s.files.size() == 1);
    CHECK(Contains(s.files[0].content, "inline void Style_danger_Push() {"));
    CHECK(Contains(s.files[0].content, "ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.753f, 0.224f, 0.169f, 1.0f));"));
    CHECK(Contains(s.files[0].content, "inline void Style_danger_Pop() {"));
    CHECK(Contains(s.files[0].content, "ImGui::PopStyleVar();"));
    CHECK(Contains(s.files[0].content, "ImGui::PopStyleColor();"));
}

TEST_CASE("fonts, reflection, dev glue") {
    Fixture fx;
    Result f = GenerateFonts(fx.project, fx.opts);
    REQUIRE(f.files.size() == 1);
    CHECK(Contains(f.files[0].content, "inline ImFont* Font_Default = nullptr;"));
    CHECK(Contains(f.files[0].content, "inline ImFont* Font_Bold = nullptr;"));
    CHECK(Contains(f.files[0].content, "inline void LoadFonts(ImGuiIO& io, float scale = 1.0f) {"));
    CHECK(Contains(f.files[0].content, "static const ImWchar icon_ranges_fa[] = { 0xe005, 0xf8ff, 0 };"));
    CHECK(Contains(f.files[0].content, "#if IMGUI_VERSION_NUM >= 19200"));
    CHECK(Contains(f.files[0].content, "io.Fonts->AddFontFromFileTTF(\"fonts/fa-solid-900.ttf\", 16.0f * scale, &icons, icon_ranges_fa);"));
    Result rf = GenerateReflection(fx.project, fx.opts);
    REQUIRE(rf.files.size() == 1);
    const std::string& rt = rf.files[0].content;
    CHECK(Contains(rt, "IMFORGE_REFLECT(Esp, enabled)"));
    CHECK(Contains(rt, "IMFORGE_REFLECT(Player, health, max_hp, name)"));
    CHECK(Contains(rt, "IMFORGE_REFLECT(Settings, esp)"));
    CHECK(rt.find("IMFORGE_REFLECT(Esp") < rt.find("IMFORGE_REFLECT(Settings"));
    Result dg = GenerateDevGlue(fx.project, fx.opts);
    REQUIRE(dg.files.size() == 1);
    CHECK(Contains(dg.files[0].content, "inline void ImForgeRegisterAll() {"));
    CHECK(Contains(dg.files[0].content, "imforge::live::RegisterHandler(\"MainMenu.btn_play.onClick\", +[](imforge::DrawCtx& c) { MainMenu_btn_play_OnClick(imforge::live::State<ui::MainMenuState>(c), imforge::live::Root<Player>(c, \"player\"), imforge::live::Root<Settings>(c, \"cfg\")); });"));
    CHECK(Contains(dg.files[0].content, "imforge::live::RegisterBlock(\"MainMenu.custom_1\""));
}

TEST_CASE("GenerateAll is sorted and deterministic") {
    Fixture fx;
    Result a = GenerateAll(fx.project, fx.opts);
    Result b = GenerateAll(fx.project, fx.opts);
    REQUIRE(a.files.size() == b.files.size());
    for (size_t i = 0; i < a.files.size(); ++i) {
        CHECK(a.files[i].path == b.files[i].path);
        CHECK(a.files[i].content == b.files[i].content);
        if (i) CHECK(a.files[i - 1].path < a.files[i].path);
    }
    CHECK(a.Find("MainMenu.cpp"));
    CHECK(a.Find("imforge_components.cpp"));
    CHECK(a.Find("imforge_theme_Dark.h"));
    CHECK(a.Find("imforge_styles.h"));
    CHECK(a.Find("imforge_fonts.h"));
    CHECK(a.Find("imforge_reflect.h"));
    CHECK(a.Find("imforge_dev.h"));
}

TEST_CASE("EmitExpression maps paths") {
    Fixture fx;
    CHECK(EmitExpression(fx.project.panels[0], "player.health > 0 && cfg.esp.enabled", fx.opts) == "(player.health > 0 && cfg.esp.enabled)");
    CHECK(EmitExpression(fx.project.panels[0], "state.selectedTab == 1", fx.opts) == "(state.selected_tab == 1)");
}

#ifndef _WIN32
// code generated for the project target (1.91.9) has to build on every pinned checkout >= 1.85,
// code generated for 1.85 only on 1.85
TEST_CASE("generated output compiles on every ImGui version (g++ -fsyntax-only)") {
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping");
        return;
    }
    Fixture fx;
    std::vector<FileOut> stubs = {
        {"Player.h", "#pragma once\n#include <string>\nstruct Player { float health = 100; int max_hp = 100; std::string name; };\n"},
        {"Settings.h", "#pragma once\nstruct Esp { bool enabled = true; };\nstruct Settings { Esp esp; };\n"}};
    Result r = GenerateAll(fx.project, fx.opts);
    for (const Diagnostic& d : r.diagnostics) CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.message);
    std::vector<std::string> dirs = ImGuiCheckouts();
    for (const std::string& dir : dirs) {
        std::string tag = "fixture_" + dir.substr(dir.rfind('/') + 1);
        CompileResult(r, stubs, tag, dir, "ui");
    }
    if (!dirs.empty() && Contains(dirs[0], "imgui-v1.85")) {
        Options o = fx.opts;
        o.imgui_version = 18500;
        Result r85 = GenerateAll(fx.project, o);
        CompileResult(r85, stubs, "fixture_for_18500", dirs[0], "ui");
    }
    MESSAGE("ImGui checkouts compiled against: " << dirs.size());
}
#endif

namespace {

struct ExampleSpec {
    const char* name;
    const char* ns;
    std::string prelude;
    std::vector<std::pair<std::string, std::string>> members;
};

void CheckExampleProject(const ExampleSpec& spec, Project& p, Result& r) {
    std::string dir = RepoRoot() + "/examples/projects/" + spec.name;
    REQUIRE(LoadProjectDir(dir, p));
    Options o = OptionsFromProject(p);
    r = GenerateAll(p, o);
    for (const Diagnostic& d : r.diagnostics)
        CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.panel << "/" << d.node_id << ": " << d.message);
    for (const Panel& panel : p.panels) {
        const FileOut* cpp = r.Find(panel.name + ".cpp");
        REQUIRE(cpp);
        std::vector<std::string> ids;
        CollectIds(panel.root, ids);
        for (const std::string& id : ids) {
            bool has = Contains(cpp->content, "// @imb:begin id=" + id + " h=");
            CHECK_MESSAGE(has, panel.name << ": no begin marker for " << id);
        }
        CHECK(CheckHashes(cpp->content) >= static_cast<int>(ids.size()));
        std::vector<Token> toks = Tokenize(cpp->content);
        for (const LiteralRef& ref : r.literals) {
            if (ref.file != cpp->path) continue;
            REQUIRE(ref.token_index >= 0);
            REQUIRE(ref.token_index < static_cast<int>(toks.size()));
            const Token& t = toks[static_cast<size_t>(ref.token_index)];
            bool literal = t.kind == Token::Kind::String || t.kind == Token::Kind::Number ||
                           (t.kind == Token::Kind::Ident && (t.text.compare(0, 5, "ImGui") == 0 || t.text == "true" || t.text == "false"));
            CHECK_MESSAGE(literal, ref.node_id << "." << ref.prop << " -> '" << t.text << "'");
        }
    }
    Result again = GenerateAll(p, o);
    REQUIRE(again.files.size() == r.files.size());
    for (size_t i = 0; i < r.files.size(); ++i) CHECK(again.files[i].content == r.files[i].content);
#ifndef _WIN32
    if (!HaveGxx()) return;
    Json types;
    REQUIRE(LoadJsonFile(dir + "/types.json", types));
    std::vector<FileOut> stubs = StubsFromTypes(types, spec.prelude, spec.members);
    // hand-written Code blocks may use ImVec2 operators, that's user code built with the user's defines
    const std::string user_flags = "-DIMGUI_DEFINE_MATH_OPERATORS -include imgui.h -include imgui_internal.h";
    std::vector<std::string> dirs = ImGuiCheckouts();
    for (const std::string& d : dirs) {
        std::string tag = std::string(spec.name) + "_" + d.substr(d.rfind('/') + 1);
        CompileResult(r, stubs, tag, d, spec.ns, user_flags);
    }
    if (!dirs.empty() && Contains(dirs[0], "imgui-v1.85")) {
        Options old = o;
        old.imgui_version = 18500;
        Result r85 = GenerateAll(p, old);
        for (const Diagnostic& d : r85.diagnostics) CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.message);
        CompileResult(r85, stubs, std::string(spec.name) + "_for_18500", dirs[0], spec.ns, user_flags);
    }
#endif
}

}  // namespace

TEST_CASE("CheatMenu example project") {
    ExampleSpec spec{"CheatMenu", "menu", "int GetPing();\nconst char* GetServerName();\n", {}};
    Project p;
    Result r;
    CheckExampleProject(spec, p, r);
    const FileOut* main_cpp = r.Find("MainMenu.cpp");
    REQUIRE(main_cpp);
    const std::string& mm = main_cpp->content;
    CHECK(Contains(mm, "BeginLabeledSlider(\"FOV\", &cfg.aim.fov, 1.0f, 180.0f, \"%.0f deg\")"));
    CHECK(!Contains(mm, "%%.0f deg"));
    CHECK(Contains(mm, "const bool aim_adv_card_visible = state.show_advanced;"));
    CHECK(Contains(mm, "imb::BeginEnter(ImGui::GetID(\"aim_adv_card\"), aim_adv_card_visible, aim_adv_card_visible ? 0.25f : 0.2f, "
                       "imb::Preset::FadeSlideUp, 0.04f)"));
    CHECK(Contains(mm, "imb::Preset::FadeSlideUp, 0.09f)"));
    CHECK(Contains(mm, "imb::BeginEnter(ImGui::GetID(\"header_sep\"), true, 0.3f, imb::Preset::FadeSlideUp, 0.04f)"));
    CHECK(!Contains(mm, "imb::BeginEnter(ImGui::GetID(\"aim_row\")"));
    CHECK(Contains(mm, "status_text_visible ? imb::Preset::FadeSlideUp : imb::Preset::Fade"));
    CHECK(Contains(mm, "state.imb_tab_tabs = 1;"));
    CHECK(Contains(mm, "#if IMGUI_VERSION_NUM >= 18950"));
    const FileOut* theme = r.Find("imforge_theme_Dark.h");
    REQUIRE(theme);
    CHECK(Contains(theme->content, "#if IMGUI_VERSION_NUM >= 19200\n    colors[ImGuiCol_InputTextCursor]"));
}

TEST_CASE("ToolDock example project") {
    ExampleSpec spec{"ToolDock", "tools",
                     "struct LogLine { int level = 0; std::string text; };\n"
                     "inline ImVec4 LevelColor(int) { return ImVec4(1, 1, 1, 1); }\n",
                     {{"LogBuffer", "std::vector<LogLine> Filtered(const char*, int) const { return {}; }"}}};
    Project p;
    Result r;
    CheckExampleProject(spec, p, r);
}

#ifndef _WIN32
namespace {

const char* kStressTypes = R"({ "schemaVersion": 1, "kind": "types",
  "structs": { "Cfg": { "header": "Cfg.h", "fields": [
      { "name": "mode", "type": "Mode", "ctype": "Enum" }, { "name": "flag", "type": "bool", "ctype": "Bool" },
      { "name": "f", "type": "float", "ctype": "Float" }, { "name": "title", "type": "std::string", "ctype": "StdString" } ] } },
  "enums": { "Mode": { "header": "Cfg.h", "values": [["Off", 0], ["Aim", 1]] } } })";

const char* kStressPanel = R"({
  "schemaVersion": 1, "kind": "panel", "name": "Stress",
  "params": [ { "name": "cfg", "type": "Cfg" } ],
  "state": [ { "name": "open", "type": "bool", "default": true } ],
  "root": { "id": "window", "type": "Window", "props": { "title": "S" }, "bind": { "open": "state.open" }, "children": [
    { "id": "row", "type": "HStack", "children": [
        { "id": "hid", "type": "Text", "props": { "text": "x" }, "visibleIf": "cfg.flag" },
        { "id": "pct", "type": "Button", "props": { "label": "P", "size": { "w": "25%", "h": "auto" } } },
        { "id": "fm", "type": "Button", "props": { "label": "F", "size": { "w": "fill-20", "h": "auto" } } },
        { "id": "fl", "type": "Button", "props": { "label": "Fill", "size": { "w": "fill", "h": "auto" } } } ] },
    { "id": "cols", "type": "HStack", "children": [
        { "id": "v1", "type": "VStack", "children": [ { "id": "t1", "type": "Text", "props": { "text": "a" } } ] },
        { "id": "v2", "type": "VStack", "children": [ { "id": "t2", "type": "Text", "props": { "text": "b" } } ] } ] },
    { "id": "cv", "type": "Canvas", "props": { "size": { "w": 200, "h": 100 } }, "children": [
        { "id": "cb", "type": "Button", "props": { "label": "C", "size": { "w": "50%", "h": 24 } },
          "layout": { "anchor": "Center", "offset": { "x": 0, "y": 0 } } },
        { "id": "ch", "type": "Text", "props": { "text": "gone" }, "visibleIf": "cfg.flag", "layout": { "anchor": "TopLeft" } } ] },
    { "id": "tabs", "type": "TabBar", "children": [
        { "id": "tab1", "type": "Tab", "props": { "label": "One", "closable": true } },
        { "id": "tab2", "type": "Tab", "props": { "label": "Two" }, "children": [
            { "id": "go", "type": "Button", "props": { "label": "Go" },
              "events": { "onClick": { "actions": [ { "type": "openPopup", "target": "dlg" },
                                                    { "type": "set", "target": "cfg.mode", "value": "Aim" } ] } } } ] } ] },
    { "id": "tn", "type": "TreeNode", "props": { "label": "T", "flags": ["NoTreePushOnOpen"] }, "children": [
        { "id": "tt", "type": "Text", "props": { "text": "in" } } ] },
    { "id": "dlg", "type": "Modal", "props": { "title": { "expr": "cfg.title" } }, "children": [
        { "id": "mt", "type": "Text", "props": { "text": "100% \u003f\u003f= \"q\" \\ end" } } ] },
    { "id": "grid", "type": "Grid", "props": { "columns": 0 }, "children": [ { "id": "gc", "type": "Text", "props": { "text": "c" } } ] },
    { "id": "huge", "type": "SliderFloat", "props": { "label": "H", "min": -1e999, "max": 1e999 }, "bind": { "value": "cfg.f" } },
    { "id": "utf", "type": "Text", "props": { "text": "h\u00e9llo \u2713 tab\tend" } }
  ] }
})";

}  // namespace

TEST_CASE("generated code matches interpreter layout rules and compiles") {
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping");
        return;
    }
    Fixture fx;
    Json types, pj;
    REQUIRE(ParseJson(kStressTypes, types));
    JsonError je;
    REQUIRE_MESSAGE(ParseJson(kStressPanel, pj, &je), je.message);
    Panel panel;
    Error err;
    REQUIRE_MESSAGE(PanelFromJson(pj, panel, &err), err.message);
    fx.project.panels.clear();
    fx.project.panels.push_back(panel);
    fx.project.live.enabled = false;
    fx.opts.types = &types;
    Result r = GeneratePanel(fx.project, panel, fx.opts);
    Result comps = GenerateComponents(fx.project, fx.opts);
    for (const Diagnostic& d : r.diagnostics) CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.node_id << ": " << d.message);
    const FileOut* cpp = r.Find("Stress.cpp");
    REQUIRE(cpp);
    const std::string& s = cpp->content;
    CHECK(Contains(s, "if (state.open) {"));
    CHECK(Contains(s, "bool row_any = false;"));
    CHECK(Contains(s, "if (row_any) ImGui::SameLine();"));
    CHECK(Contains(s, "const float row_fill_w = std::max("));
    CHECK(Contains(s, "ImGui::GetContentRegionAvail().x * (25 / 100.0f)"));
    CHECK(Contains(s, "std::max(ImGui::GetContentRegionAvail().x - 20 * imb_s, 1.0f)"));
    CHECK(Contains(s, "ImGui::BeginGroup();"));
    CHECK(Contains(s, "ImGui::EndGroup();"));
    CHECK(Contains(s, "ImGui::Dummy(ImVec2(0, 0));"));
    CHECK(Contains(s, "- ImGui::GetContentRegionAvail().x * (25 / 100.0f)"));
    CHECK(Contains(s, "static bool tab1_open = true;"));
    CHECK(Contains(s, "if (tab1_open) {"));
    CHECK(Contains(s, "ImGui::OpenPopup(\"###dlg\");"));
    CHECK(Contains(s, "cfg.mode = Mode::Aim;"));
    CHECK(Contains(s, "ImGui::BeginTable(\"grid\", 1,"));
    CHECK(!Contains(s, "TreePop"));
    CHECK(!Contains(s, "inff"));
    CHECK(!Contains(s, "nanf"));
    CHECK(!Contains(s, "?" "?="));
    CHECK(Contains(s, "?\\?="));
    CHECK(Contains(s, "100%% "));
    CHECK(Contains(s, "BeginPopupModal(dlg_modal_title"));
    Result all;
    all.files = r.files;
    for (const FileOut& f : comps.files) all.files.push_back(f);
    std::vector<FileOut> stubs = StubsFromTypes(types, "", {});
    stubs.push_back({"Player.h", "#pragma once" "\n" "struct Player {};" "\n"});
    for (const std::string& dir : ImGuiCheckouts()) CompileResult(all, stubs, "stress_" + dir.substr(dir.rfind('/') + 1), dir, "ui");
}

TEST_CASE("2000-node panel generates in reasonable time with unique, balanced markers") {
    std::string kids;
    for (int i = 0; i < 2000; ++i) {
        if (i) kids += ",";
        kids += "{\"id\":\"b" + std::to_string(i) + "\",\"type\":\"Button\",\"props\":{\"label\":\"Same\"},\"events\":{\"onClick\":{\"handler\":true}}}";
    }
    std::string text = "{\"schemaVersion\":1,\"kind\":\"panel\",\"name\":\"Big\",\"params\":[],\"state\":[],\"root\":{\"id\":\"window\",\"type\":\"Window\",\"props\":{\"title\":\"B\"},\"children\":[" + kids + "]}}";
    Json pj;
    REQUIRE(ParseJson(text, pj));
    Panel panel;
    Error err;
    REQUIRE_MESSAGE(PanelFromJson(pj, panel, &err), err.message);
    Fixture fx;
    clock_t t0 = clock();
    Result r = GeneratePanel(fx.project, panel, fx.opts);
    double secs = double(clock() - t0) / CLOCKS_PER_SEC;
    CHECK_MESSAGE(secs < 10.0, "generation took " << secs << "s");
    const FileOut* cpp = r.Find("Big.cpp");
    REQUIRE(cpp);
    CHECK(CheckHashes(cpp->content) >= 2001);
    CHECK(r.literals.size() >= 2000);
}
#endif

#ifndef _WIN32
TEST_CASE("renamed flags and 1.92 colors are version-guarded and compile everywhere") {
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping");
        return;
    }
    Fixture fx;
    const char* panel_json = R"({ "schemaVersion": 1, "kind": "panel", "name": "Ren", "params": [], "state": [],
      "root": { "id": "window", "type": "Window", "props": { "title": "R" }, "children": [
        { "id": "tn", "type": "TreeNode", "props": { "label": "T", "flags": ["NavLeftJumpsBackHere"] }, "children": [] },
        { "id": "tn2", "type": "TreeNode", "props": { "label": "T2", "flags": ["NavLeftJumpsToParent"] }, "children": [] },
        { "id": "tb", "type": "TabBar", "props": { "flags": ["FittingPolicyResizeDown", "Reorderable"] }, "children": [
           { "id": "t1", "type": "Tab", "props": { "label": "A" } } ] },
        { "id": "b", "type": "Button", "props": { "label": "B" }, "style": { "colors": { "InputTextCursor": "#ff0000ff", "TabSelected": "#00ff00ff" } } } ] } })";
    Json pj;
    REQUIRE(ParseJson(panel_json, pj));
    Panel panel;
    Error err;
    REQUIRE_MESSAGE(PanelFromJson(pj, panel, &err), err.message);
    Theme theme;
    Json tj;
    REQUIRE(ParseJson(R"({ "schemaVersion": 1, "kind": "theme", "name": "New", "colors": { "InputTextCursor": "#ffffffff", "TabSelected": "#336699ff", "TabActive": "#112233ff" } })", tj));
    REQUIRE_MESSAGE(ThemeFromJson(tj, theme, &err), err.message);
    for (int target : {18500, 19190, 19300}) {
        Options o = fx.opts;
        o.imgui_version = target;
        Result r = GeneratePanel(fx.project, panel, o);
        Result t = GenerateTheme(fx.project, theme, o);
        const FileOut* cpp = r.Find("Ren.cpp");
        REQUIRE(cpp);
        const std::string& s = cpp->content;
        CHECK(Contains(s, "ImGuiTreeNodeFlags_NavLeftJumpsToParent"));
        CHECK(Contains(s, "ImGuiTreeNodeFlags_NavLeftJumpsBackHere"));
        CHECK(Contains(s, "#if IMGUI_VERSION_NUM >= 19200"));
        CHECK(Contains(s, "ImGuiTabBarFlags_FittingPolicyShrink"));
        CHECK(Contains(s, "ImGuiTabBarFlags_FittingPolicyResizeDown"));
        CHECK(Contains(s, "#if IMGUI_VERSION_NUM >= 19220"));
        CHECK(Contains(t.files[0].content, "#if IMGUI_VERSION_NUM >= 19200"));
        Result all;
        all.files = r.files;
        all.files.push_back(t.files[0]);
        for (const std::string& dir : ImGuiCheckouts()) {
            std::string ver = dir.substr(dir.rfind('/') + 1);
            if (ver != "imgui-v1.85" && ver != "imgui-v1.91.9b" && ver != "imgui-v1.93.0") continue;
            CompileResult(all, {}, "rename_" + std::to_string(target) + "_" + ver, dir, "ui");
        }
    }
}
#endif

#ifndef _WIN32
namespace {

const char* kAlignPanel = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Parity", "params": [],
  "state": [ { "name": "show", "type": "bool", "default": false } ],
  "root": { "id": "window", "type": "Window",
    "props": { "title": "Parity", "initialSize": { "x": 600, "y": 400 }, "initialPos": { "x": 10, "y": 10 } }, "children": [
    { "id": "r_center", "type": "HStack", "props": { "align": "Center" }, "style": { "colors": { "Button": "#ff0000ff" } }, "children": [
        { "id": "c1", "type": "Button", "props": { "label": "Alpha" } },
        { "id": "c2", "type": "Button", "props": { "label": "B", "size": { "w": 80, "h": "auto" } } } ] },
    { "id": "r_end", "type": "HStack", "props": { "align": "End", "spacing": 12 }, "style": { "colors": { "Button": "#00ff00ff" } }, "children": [
        { "id": "e0", "type": "Text", "props": { "text": "label" } },
        { "id": "e1", "type": "Button", "props": { "label": "End" } } ] },
    { "id": "r_fill", "type": "HStack", "props": { "align": "Center" }, "style": { "colors": { "Button": "#0000ffff" } }, "children": [
        { "id": "f0", "type": "Text", "props": { "text": "Smooth" } },
        { "id": "f_hidden", "type": "Text", "props": { "text": "never" }, "visibleIf": "state.show" },
        { "id": "f1", "type": "Button", "props": { "label": "Fill", "size": { "w": "fill", "h": "auto" } } },
        { "id": "f2", "type": "Button", "props": { "label": "x" } } ] },
    { "id": "v", "type": "VStack", "props": { "indent": 20 }, "children": [
        { "id": "r_nested", "type": "HStack", "props": { "align": "End" }, "style": { "colors": { "Button": "#ffff00ff" } }, "children": [
            { "id": "n1", "type": "Button", "props": { "label": "Nested" } } ] } ] },
    { "id": "inst", "type": "Component", "component": "Row", "props": { "label": "Comp" } },
    { "id": "slotted", "type": "Component", "component": "SlotRow",
      "slots": { "content": [ { "id": "inner", "type": "Button", "props": { "label": "Inner" } } ] } },
    { "id": "r_child", "type": "HStack", "style": { "colors": { "Button": "#ff8000ff" } }, "children": [
        { "id": "ch", "type": "Child", "props": { "border": true }, "children": [
            { "id": "chb", "type": "Button", "props": { "label": "In" } } ] },
        { "id": "chx", "type": "Button", "props": { "label": "Side", "size": { "w": 60, "h": "auto" } } } ] }
  ] } })imbjson";

const char* kAlignRow = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "Row",
  "params": [ { "name": "label", "type": "string", "default": "X" } ],
  "root": { "id": "row", "type": "HStack", "props": { "align": "Center" }, "style": { "colors": { "Button": "#ff00ffff" } }, "children": [
      { "id": "rb", "type": "Button", "props": { "label": { "param": "label" } } } ] } })imbjson";

const char* kAlignSlotRow = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "SlotRow", "params": [], "slots": [ "content" ],
  "root": { "id": "srow", "type": "HStack", "props": { "align": "End" }, "style": { "colors": { "Button": "#00ffffff" } }, "children": [
      { "id": "s1", "type": "Button", "props": { "label": "S1" } },
      { "id": "slot_content", "type": "Slot", "props": { "name": "content" } } ] } })imbjson";

const char* kAlignPad = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "Pad",
  "params": [ { "name": "ind", "type": "float", "default": 4 }, { "name": "gap", "type": "float", "default": 3 },
              { "name": "al", "type": "string", "default": "End" } ],
  "root": { "id": "pad", "type": "VStack", "props": { "indent": { "param": "ind" } }, "children": [
      { "id": "prow", "type": "HStack", "props": { "spacing": { "param": "gap" }, "align": { "param": "al" } }, "children": [
          { "id": "pa", "type": "Button", "props": { "label": "A", "size": { "w": "fill", "h": "auto" } } },
          { "id": "pb", "type": "Button", "props": { "label": "B" } } ] } ] } })imbjson";

const char* kSplitPanel = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Split", "params": [],
  "state": [ { "name": "al", "type": "string", "default": "End" }, { "name": "ai", "type": "int", "default": 1 },
             { "name": "mshow", "type": "bool", "default": true } ],
  "root": { "id": "window", "type": "Window",
    "props": { "title": "Split", "initialSize": { "x": 640, "y": 700 }, "initialPos": { "x": 10, "y": 10 } }, "children": [
    { "id": "lead", "type": "Text", "props": { "text": "lead" } },
    { "id": "sf1", "type": "Component", "component": "SplitFill", "style": { "colors": { "Button": "#ff0000ff" } },
      "slots": { "content": [ { "id": "sf1_in", "type": "Button", "props": { "label": "In" }, "style": { "colors": { "Button": "#00ff00ff" } } } ] } },
    { "id": "sf2", "type": "Component", "component": "SplitFill", "style": { "colors": { "Button": "#0000ffff" } } },
    { "id": "sh1", "type": "Component", "component": "SplitHide", "style": { "colors": { "Button": "#ffff00ff" } } },
    { "id": "sb1", "type": "Component", "component": "SplitBox", "style": { "colors": { "Button": "#ff00ffff" } },
      "slots": { "content": [ { "id": "sb1_in", "type": "Button", "props": { "label": "Boxed" }, "style": { "colors": { "Button": "#00ffffff" } } } ] } },
    { "id": "sc1", "type": "Component", "component": "SplitCanvas", "style": { "colors": { "Button": "#ff8000ff" } },
      "slots": { "content": [ { "id": "sc1_in", "type": "Button", "props": { "label": "Mid" }, "style": { "colors": { "Button": "#8000ffff" } } } ] } },
    { "id": "ap1", "type": "Component", "component": "AlignP", "props": { "al": "Center", "ae": "End", "ai": 1 } },
    { "id": "pe", "type": "HStack", "props": { "align": { "expr": "state.al" } }, "style": { "colors": { "Button": "#00ff80ff" } }, "children": [
        { "id": "pe1", "type": "Button", "props": { "label": "Expr" } } ] },
    { "id": "pei", "type": "HStack", "props": { "align": { "expr": "state.ai" } }, "style": { "colors": { "Button": "#400080ff" } }, "children": [
        { "id": "pei1", "type": "Button", "props": { "label": "IntExpr" } } ] },
    { "id": "gp0", "type": "Component", "component": "GridP", "style": { "colors": { "Button": "#804000ff" } } },
    { "id": "gp3", "type": "Component", "component": "GridP", "props": { "cols": 3 }, "style": { "colors": { "Button": "#408000ff" } } },
    { "id": "pcv", "type": "Canvas", "props": { "size": { "w": 200, "h": 80 }, "border": true }, "style": { "colors": { "Button": "#c0c0c0ff" } }, "children": [
        { "id": "pcr", "type": "Button", "props": { "label": "TR", "size": { "w": 40, "h": 20 } }, "layout": { "anchor": "TopRight" } },
        { "id": "pcb", "type": "Button", "props": { "label": "BR" }, "layout": { "anchor": "BottomRight" } } ] },
    { "id": "dlg", "type": "Modal", "props": { "title": "Dlg" }, "bind": { "open": "state.mshow" },
      "events": { "onClose": { "actions": [ { "type": "set", "target": "state.ai", "value": 1 } ] } }, "children": [
        { "id": "dlg_b", "type": "Button", "props": { "label": "InModal" }, "style": { "colors": { "Button": "#ff4040ff" } } } ] }
  ] } })imbjson";

const char* kSplitFill = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "SplitFill", "params": [], "slots": [ "content" ],
  "root": { "id": "frow", "type": "HStack", "children": [
      { "id": "fa", "type": "Button", "props": { "label": "Auto" } },
      { "id": "slot_content", "type": "Slot", "props": { "name": "content" } },
      { "id": "ff", "type": "Button", "props": { "label": "Fill", "size": { "w": "fill", "h": "auto" } } },
      { "id": "fz", "type": "Button", "props": { "label": "Z" } } ] } })imbjson";

const char* kSplitHide = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "SplitHide",
  "params": [ { "name": "show", "type": "bool", "default": false } ], "slots": [ "content" ],
  "root": { "id": "hrow", "type": "HStack", "children": [
      { "id": "hh", "type": "Button", "props": { "label": "Hidden" }, "visibleIf": "show" },
      { "id": "slot_content", "type": "Slot", "props": { "name": "content" } },
      { "id": "hb", "type": "Button", "props": { "label": "After" } } ] } })imbjson";

const char* kSplitBox = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "SplitBox", "params": [], "slots": [ "content" ],
  "root": { "id": "brow", "type": "HStack", "children": [
      { "id": "bl", "type": "Button", "props": { "label": "L" } },
      { "id": "bbox", "type": "Child", "props": { "size": { "w": "fill", "h": 40 } }, "children": [
          { "id": "slot_content", "type": "Slot", "props": { "name": "content" } } ] },
      { "id": "bt", "type": "Button", "props": { "label": "T", "size": { "w": 50, "h": "auto" } } } ] } })imbjson";

const char* kSplitCanvas = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "SplitCanvas", "params": [], "slots": [ "content" ],
  "root": { "id": "cv", "type": "Canvas", "props": { "size": { "w": 220, "h": 60 } }, "children": [
      { "id": "c0", "type": "Button", "props": { "label": "TL" } },
      { "id": "slot_content", "type": "Slot", "props": { "name": "content" }, "layout": { "anchor": "Center" } },
      { "id": "c2", "type": "Button", "props": { "label": "R", "size": { "w": 30, "h": 20 } }, "layout": { "anchor": "Right" } } ] } })imbjson";

const char* kAlignP = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "AlignP",
  "params": [ { "name": "al", "type": "string", "default": "End" }, { "name": "ae", "type": "enum:Start,Center,End", "default": "Center" },
              { "name": "ai", "type": "int", "default": 2 } ],
  "root": { "id": "acol", "type": "VStack", "children": [
      { "id": "as_row", "type": "HStack", "props": { "align": { "param": "al" } }, "style": { "colors": { "Button": "#0080ffff" } }, "children": [
          { "id": "as1", "type": "Button", "props": { "label": "S1" } }, { "id": "as2", "type": "Button", "props": { "label": "S2" } } ] },
      { "id": "ae_row", "type": "HStack", "props": { "align": { "param": "ae" } }, "style": { "colors": { "Button": "#ff0080ff" } }, "children": [
          { "id": "ae1", "type": "Button", "props": { "label": "E1" } } ] },
      { "id": "ai_row", "type": "HStack", "props": { "align": { "param": "ai" } }, "style": { "colors": { "Button": "#80ff00ff" } }, "children": [
          { "id": "ai1", "type": "Button", "props": { "label": "I1" } } ] } ] } })imbjson";

const char* kGridP = R"imbjson({ "schemaVersion": 1, "kind": "component", "name": "GridP", "params": [ { "name": "cols", "type": "int", "default": 0 } ],
  "root": { "id": "gg", "type": "Grid", "props": { "columns": { "param": "cols" }, "headers": [ "a", "b" ] }, "children": [
      { "id": "gg1", "type": "Button", "props": { "label": "G1" } }, { "id": "gg2", "type": "Button", "props": { "label": "G2" } } ] } })imbjson";

const std::vector<const char*> kAlignComps = {kAlignRow, kAlignSlotRow, kAlignPad};
const std::vector<const char*> kSplitComps = {kSplitFill, kSplitHide, kSplitBox, kSplitCanvas, kAlignP, kGridP};
const char* kAlignColors = "IM_COL32(255, 0, 0, 255), IM_COL32(0, 255, 0, 255), IM_COL32(0, 0, 255, 255), IM_COL32(255, 255, 0, 255), "
                           "IM_COL32(255, 0, 255, 255), IM_COL32(0, 255, 255, 255), IM_COL32(255, 128, 0, 255)";
const char* kSplitColors = "IM_COL32(255, 0, 0, 255), IM_COL32(0, 255, 0, 255), IM_COL32(0, 0, 255, 255), IM_COL32(255, 255, 0, 255), "
                           "IM_COL32(255, 0, 255, 255), IM_COL32(0, 255, 255, 255), IM_COL32(255, 128, 0, 255), IM_COL32(128, 0, 255, 255), "
                           "IM_COL32(0, 128, 255, 255), IM_COL32(255, 0, 128, 255), IM_COL32(128, 255, 0, 255), IM_COL32(0, 255, 128, 255), "
                           "IM_COL32(64, 0, 128, 255), IM_COL32(128, 64, 0, 255), IM_COL32(64, 128, 0, 255), IM_COL32(192, 192, 192, 255)";

struct ParityFixture {
    Fixture fx;
    Panel panel;
    Result all;
    ParityFixture(const char* panel_json, const std::vector<const char*>& comps) {
        Json j;
        Error err;
        REQUIRE(ParseJson(panel_json, j));
        REQUIRE_MESSAGE(PanelFromJson(j, panel, &err), err.message);
        fx.project.panels.clear();
        fx.project.panels.push_back(panel);
        fx.project.components.clear();
        for (const char* text : comps) {
            Component c;
            REQUIRE(ParseJson(text, j));
            REQUIRE_MESSAGE(ComponentFromJson(j, c, &err), err.message);
            fx.project.components.push_back(c);
        }
        fx.project.live.enabled = false;
        Result r = GeneratePanel(fx.project, panel, fx.opts);
        Result cr = GenerateComponents(fx.project, fx.opts);
        for (const Diagnostic& d : r.diagnostics) CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.node_id << ": " << d.message);
        for (const Diagnostic& d : cr.diagnostics) CHECK_MESSAGE(d.level != Diagnostic::Level::Error, d.node_id << ": " << d.message);
        all.files = r.files;
        for (const FileOut& f : cr.files) all.files.push_back(f);
        all.diagnostics = r.diagnostics;
        for (const Diagnostic& d : cr.diagnostics) all.diagnostics.push_back(d);
    }
};

std::string ParityDriver(const std::string& name, const char* panel_json, const std::vector<const char*>& comps, const char* colors) {
    std::string s = "#include \"" + name + ".h\"\n#include \"imforge_components.h\"\n#include \"imforge/imforge_interp.h\"\n"
                    "#include \"imgui_headless.h\"\n#include <algorithm>\n#include <cstdio>\n#include <vector>\n";
    s += std::string("static const char* kPanel = R\"imbjson(") + panel_json + ")imbjson\";\n";
    s += "static const char* kComps[] = {";
    for (size_t i = 0; i < comps.size(); ++i) s += std::string(i ? ", " : "") + "R\"imbjson(" + comps[i] + ")imbjson\"";
    s += "};\nstatic const ImU32 kColors[] = {" + std::string(colors) + "};\n";
    s += "using GenState = ui::" + name + "State;\nstatic void DrawGenerated(GenState& st) { ui::Draw" + name + "(st); }\n";
    s += R"cpp(
static void Dump(const char* side, int frame) {
    ImDrawData* dd = ImGui::GetDrawData();
    for (ImU32 c : kColors) {
        std::vector<float> xs;
        for (int l = 0; l < dd->CmdListsCount; ++l) {
            const ImDrawList* dl = dd->CmdLists[l];
            for (int i = 0; i < dl->VtxBuffer.Size; ++i)
                if (dl->VtxBuffer[i].col == c) xs.push_back(dl->VtxBuffer[i].pos.x);
        }
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        std::printf("%s %d %08x", side, frame, (unsigned)c);
        for (float x : xs) std::printf(" %.3f", x);
        std::printf("\n");
    }
}
int main() {
    {
        imforge::test::HeadlessImGui g;
        GenState st;
        for (int f = 0; f < 3; ++f) {
            g.BeginFrame();
            DrawGenerated(st);
            g.EndFrame();
            Dump("gen", f);
        }
    }
    {
        imforge::test::HeadlessImGui g;
        imforge::Project project;
        imforge::Json j;
        imforge::Error err;
        for (const char* text : kComps) {
            imforge::Component c;
            if (!imforge::ParseJson(text, j) || !imforge::ComponentFromJson(j, c, &err)) return 2;
            project.components.push_back(c);
        }
        imforge::Panel panel;
        if (!imforge::ParseJson(kPanel, j) || !imforge::PanelFromJson(j, panel, &err)) return 3;
        imforge::Interpreter interp;
        interp.project = &project;
        imforge::PanelInstance inst;
        std::vector<imforge::BindingRoot> roots;
        imforge::InterpOptions opts;
        for (int f = 0; f < 3; ++f) {
            g.BeginFrame();
            interp.DrawPanel(panel, inst, roots, opts);
            g.EndFrame();
            Dump("interp", f);
        }
    }
    return 0;
}
)cpp";
    return s;
}

std::vector<float> XsOf(const std::string& line) {
    std::vector<float> xs;
    size_t pos = line.find(' ');
    if (pos != std::string::npos) pos = line.find(' ', pos + 1);
    while (pos != std::string::npos && pos + 1 < line.size()) {
        xs.push_back(static_cast<float>(std::strtod(line.c_str() + pos + 1, nullptr)));
        pos = line.find(' ', pos + 1);
    }
    return xs;
}

bool RunParity(const std::string& tag, const std::string& name, const Result& all, const std::string& driver,
               std::vector<std::string>& gen, std::vector<std::string>& interp) {
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping");
        return false;
    }
    std::string imgui = DepsDir() + "/imgui-v1.91.9b";
    if (!DirExists(imgui)) {
        MESSAGE("imgui-v1.91.9b not found; skipping");
        return false;
    }
    std::string dir = TempRoot() + "/imforge_codegen_" + tag;
    REQUIRE(std::system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "'").c_str()) == 0);
    for (const FileOut& f : all.files) REQUIRE(WriteFile(dir + "/" + f.path, f.content));
    REQUIRE(WriteFile(dir + "/driver.cpp", driver));
    std::string root = RepoRoot();
    std::string gxx = "g++ -std=c++17 -O0 -w -I'" + dir + "' -I'" + root + "/include' -I'" + root + "/include/imforge' -I'" + root +
                      "/tests' -I'" + imgui + "' ";
    std::vector<std::string> units = {imgui + "/imgui.cpp", imgui + "/imgui_draw.cpp", imgui + "/imgui_tables.cpp",
                                      imgui + "/imgui_widgets.cpp", dir + "/" + name + ".cpp", dir + "/driver.cpp"};
    if (all.Find("imforge_components.cpp")) units.push_back(dir + "/imforge_components.cpp");
    std::string script = "cd '" + dir + "' || exit 1; ";
    std::string objs;
    for (size_t i = 0; i < units.size(); ++i) {
        std::string o = "u" + std::to_string(i) + ".o";
        script += "(" + gxx + "-c '" + units[i] + "' -o " + o + " 2>" + o + ".err) & ";
        objs += " " + o;
    }
    script += "wait; g++" + objs + " -o parity 2>link.err && ./parity >out.txt 2>run.err";
    int rc = std::system(script.c_str());
    std::string errs;
    for (size_t i = 0; i < units.size(); ++i) errs += ReadFile(dir + "/u" + std::to_string(i) + ".o.err");
    errs += ReadFile(dir + "/link.err") + ReadFile(dir + "/run.err");
    REQUIRE_MESSAGE(rc == 0, "build/run failed:\n" << errs);
    for (const std::string& l : Lines(ReadFile(dir + "/out.txt"))) {
        if (l.compare(0, 4, "gen ") == 0) gen.push_back(l.substr(4));
        else if (l.compare(0, 7, "interp ") == 0) interp.push_back(l.substr(7));
    }
    return true;
}

void CheckSameXs(const std::vector<std::string>& gen, const std::vector<std::string>& interp, size_t lines) {
    REQUIRE(gen.size() == lines);
    REQUIRE(interp.size() == gen.size());
    const size_t colors = lines / 3;
    std::vector<bool> seen(colors, false);
    for (size_t i = 0; i < gen.size(); ++i) {
        std::vector<float> a = XsOf(gen[i]), b = XsOf(interp[i]);
        bool same = a.size() == b.size();
        for (size_t k = 0; same && k < a.size(); ++k) same = std::fabs(a[k] - b[k]) < 0.01f;
        if (!b.empty()) seen[i % colors] = true;
        CHECK_MESSAGE(same, "gen    " << gen[i] << "\ninterp " << interp[i]);
    }
    for (size_t c = 0; c < colors; ++c) CHECK_MESSAGE(seen[c], "color never drawn: " << gen[c]);
}

}  // namespace

TEST_CASE("HStack align Center/End: measured alignment in the generated code compiles on 1.85, 1.91.9b, 1.93.0") {
    ParityFixture af(kAlignPanel, kAlignComps);
    const FileOut* cpp = af.all.Find("Parity.cpp");
    const FileOut* comps = af.all.Find("imforge_components.cpp");
    REQUIRE(cpp);
    REQUIRE(comps);
    const std::string& s = cpp->content;
    CHECK(Contains(s, "const float r_center_align_w = ImGui::GetStateStorage()->GetFloat(ImGui::GetID(\"r_center##imb_w\"), 0.0f);"));
    CHECK(Contains(s, "if (r_center_align_w > 0.0f && r_center_align_w < r_center_avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + "
                      "(r_center_avail - r_center_align_w) * 0.5f);"));
    CHECK(Contains(s, "ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (r_end_avail - r_end_align_w));"));
    CHECK(Contains(s, "ImGui::GetStateStorage()->SetFloat(ImGui::GetID(\"r_center##imb_w\"), ImGui::GetItemRectSize().x);"));
    CHECK(Contains(s, "- ImGui::GetStateStorage()->GetFloat(ImGui::GetID(\"f0##imb_w\"), 0.0f)"));
    CHECK(Contains(s, "ImGui::GetStateStorage()->SetFloat(ImGui::GetID(\"f2##imb_w\"), r_fill_drawn3 ? std::max(ImGui::GetItemRectMax().x - r_fill_x3, 0.0f) : 0.0f);"));
    CHECK(Contains(s, "if (r_fill_drawn1) r_fill_any = true;"));
    CHECK(Contains(s, "ImVec2(r_child_fill_w, -FLT_MIN)"));
    const std::string& c = comps->content;
    CHECK(Contains(c, "ImGui::GetStateStorage()->SetFloat(ImGui::GetID(\"srow##imb_w\"), ImGui::GetItemRectSize().x);"));
    CHECK(Contains(c, "ImGui::GetStateStorage()->SetFloat(ImGui::GetID(\"row##imb_w\"), ImGui::GetItemRectSize().x);"));
    CHECK(Contains(c, "if ((ind) > 0) ImGui::Indent((ind) * imb_s);"));
    CHECK(Contains(c, "if ((ind) > 0) ImGui::Unindent((ind) * imb_s);"));
    CHECK(Contains(c, "ImGui::SameLine(0.0f, (gap) * imb_s);"));
    CHECK(Contains(c, "const int prow_align = (!al ? 0 : (std::strcmp(al, \"Center\") == 0 ? 1 : (std::strcmp(al, \"End\") == 0 ? 2 : 0)));"));
    CHECK(Contains(c, "if (prow_align > 0 && prow_align_w > 0.0f && prow_align_w < prow_avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + "
                      "(prow_avail - prow_align_w) * (prow_align == 1 ? 0.5f : 1.0f));"));
    bool param_align_diag = false;
    for (const Diagnostic& d : af.all.diagnostics) param_align_diag = param_align_diag || (d.node_id == "prow" && d.prop == "align");
    CHECK(!param_align_diag);
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping compile");
        return;
    }
    for (const std::string& dir : ImGuiCheckouts()) {
        std::string ver = dir.substr(dir.rfind('/') + 1);
        if (ver != "imgui-v1.85" && ver != "imgui-v1.91.9b" && ver != "imgui-v1.93.0") continue;
        CompileResult(af.all, {}, "align_" + ver, dir, "ui");
    }
}

TEST_CASE("HStack align/fill parity: generated code and interpreter place items at the same X for 3 frames") {
    ParityFixture af(kAlignPanel, kAlignComps);
    std::vector<std::string> gen, interp;
    if (!RunParity("align_parity", "Parity", af.all, ParityDriver("Parity", kAlignPanel, kAlignComps, kAlignColors), gen, interp)) return;
    CheckSameXs(gen, interp, 21);
    for (size_t row : {0u, 1u, 3u, 4u, 5u}) {
        std::vector<float> f0 = XsOf(gen[row]), f2 = XsOf(gen[14 + row]);
        REQUIRE(!f0.empty());
        REQUIRE(!f2.empty());
        CHECK_MESSAGE(f2[0] > f0[0] + 10.0f, "row " << row << " not aligned: " << gen[14 + row]);
    }
}

TEST_CASE("split components, param/expr align, Grid columns, Canvas: generated code compiles on 1.85, 1.91.9b, 1.93.0") {
    ParityFixture sf(kSplitPanel, kSplitComps);
    const FileOut* cpp = sf.all.Find("Split.cpp");
    const FileOut* comps = sf.all.Find("imforge_components.cpp");
    REQUIRE(cpp);
    REQUIRE(comps);
    const std::string& c = comps->content;
    CHECK(Contains(c, "ImGui::GetStateStorage()->SetFloat(ImGui::GetID(\"frow##imb_fill_w\"), frow_fill_w);"));
    CHECK(Contains(c, "const float frow_fill_w = ImGui::GetStateStorage()->GetFloat(ImGui::GetID(\"frow##imb_fill_w\"), 0.0f);"));
    CHECK(Contains(c, "bool hrow_any = ImGui::GetStateStorage()->GetBool(ImGui::GetID(\"hrow##imb_any\"), false);"));
    CHECK(Contains(c, "ImGui::GetStateStorage()->SetFloat(ImGui::GetID(\"slot_content##imb_w\"), frow_drawn1 ? std::max(ImGui::GetItemRectMax().x - frow_x1, 0.0f) : 0.0f);"));
    CHECK(Contains(c, "ImVec2(brow_fill_w, 40 * imb_s)"));
    CHECK(Contains(c, "const ImVec2 cv_origin(ImGui::GetStateStorage()->GetFloat(ImGui::GetID(\"cv##imb_origin_x\"), 0.0f), "));
    CHECK(Contains(c, "const int as_row_align = (!al ? 0 : (std::strcmp(al, \"Center\") == 0 ? 1 : (std::strcmp(al, \"End\") == 0 ? 2 : 0)));"));
    CHECK(Contains(c, "const int ae_row_align = (ae == 1 ? 1 : (ae == 2 ? 2 : 0));"));
    CHECK(Contains(c, "const int ai_row_align = ai;"));
    CHECK(Contains(c, "const int gg_cols = std::max(1, std::min(static_cast<int>(cols), (IMGUI_VERSION_NUM >= 18990 ? 511 : 63)));"));
    CHECK(Contains(c, "if (gg_cols > 1) ImGui::TableSetupColumn(\"b\");"));
    CHECK(Contains(c, "for (int gg_c = 2; gg_c < gg_cols; ++gg_c) ImGui::TableSetupColumn(\"\");"));
    const std::string& s = cpp->content;
    CHECK(Contains(s, "const int pe_align = (std::strcmp(state.al.c_str(), \"Center\") == 0 ? 1 : (std::strcmp(state.al.c_str(), \"End\") == 0 ? 2 : 0));"));
    CHECK(Contains(s, "const int pei_align = static_cast<int>(state.ai);"));
    CHECK(!Contains(s, "NoScrollbar"));
    CHECK(Contains(s, "if (ImGui::GetCursorPos().x == pcv_last.x && ImGui::GetCursorPos().y == pcv_last.y) ImGui::Dummy(ImVec2(0, 0));"));
    CHECK(Contains(s, "const int dlg_prev = ImGui::GetStateStorage()->GetInt(ImGui::GetID(\"dlg##imb_open\"), -1);"));
    CHECK(Contains(s, "if (dlg_popup && dlg_prev != 1) state.mshow = true;"));
    CHECK(Contains(s, "else if (!dlg_popup && state.mshow) ImGui::OpenPopup(\"###dlg\");"));
    CHECK(Contains(s, "const bool dlg_shown = ImGui::BeginPopupModal(\"Dlg\" \"###dlg\", nullptr, 0);"));
    CHECK(Contains(s, "if (dlg_shown && !state.mshow) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }"));
    CHECK(Contains(s, "if (dlg_shown && !dlg_now) state.mshow = false;"));
    CHECK(Contains(s, "if (!dlg_now && dlg_prev == 1) {"));
    for (const Diagnostic& d : sf.all.diagnostics) CHECK_MESSAGE(d.prop != "align", d.node_id << ": " << d.message);
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping compile");
        return;
    }
    for (const std::string& dir : ImGuiCheckouts()) {
        std::string ver = dir.substr(dir.rfind('/') + 1);
        if (ver != "imgui-v1.85" && ver != "imgui-v1.91.9b" && ver != "imgui-v1.93.0") continue;
        CompileResult(sf.all, {}, "split_" + ver, dir, "ui");
    }
}

TEST_CASE("split/param-align/Grid/Canvas parity: generated code and interpreter place items at the same X for 3 frames") {
    ParityFixture sf(kSplitPanel, kSplitComps);
    std::vector<std::string> gen, interp;
    if (!RunParity("split_parity", "Split", sf.all, ParityDriver("Split", kSplitPanel, kSplitComps, kSplitColors), gen, interp)) return;
    CheckSameXs(gen, interp, 48);
}

namespace {

#define IMB_SEQ(code) R"({ "actions": [ { "type": "set", "target": "state.seq", "value": { "expr": "state.seq * 100 + )" #code R"(" } } ] })"

const char* kEventsPanel = R"imbjson({ "schemaVersion": 1, "kind": "panel", "name": "Events", "params": [],
  "state": [ { "name": "show", "type": "bool", "default": false }, { "name": "win", "type": "bool", "default": true },
             { "name": "t1", "type": "bool", "default": true }, { "name": "seq", "type": "int", "default": 0 } ],
  "root": { "id": "window", "type": "Window", "bind": { "open": "state.win" },
    "props": { "title": "Events", "initialSize": { "x": 500, "y": 400 }, "initialPos": { "x": 10, "y": 10 } },
    "events": { "onOpen": )imbjson" IMB_SEQ(61) R"imbjson(, "onClose": )imbjson" IMB_SEQ(62) R"imbjson( }, "children": [
    { "id": "hdr", "type": "CollapsingHeader", "props": { "label": "Header" },
      "events": { "onOpen": )imbjson" IMB_SEQ(41) R"imbjson(, "onClose": )imbjson" IMB_SEQ(42) R"imbjson( }, "children": [
        { "id": "hdr_t", "type": "Text", "props": { "text": "inside" } } ] },
    { "id": "tree", "type": "TreeNode", "props": { "label": "Tree", "defaultOpen": true },
      "events": { "onOpen": )imbjson" IMB_SEQ(51) R"imbjson(, "onClose": )imbjson" IMB_SEQ(52) R"imbjson( }, "children": [
        { "id": "leaf", "type": "Text", "props": { "text": "leaf" } } ] },
    { "id": "tabs", "type": "TabBar", "children": [
        { "id": "tab1", "type": "Tab", "props": { "label": "One", "closable": true }, "bind": { "open": "state.t1" },
          "events": { "onOpen": )imbjson" IMB_SEQ(21) R"imbjson(, "onClose": )imbjson" IMB_SEQ(22) R"imbjson( }, "children": [
            { "id": "x1", "type": "Text", "props": { "text": "one" } } ] },
        { "id": "tab2", "type": "Tab", "props": { "label": "Two" },
          "events": { "onOpen": )imbjson" IMB_SEQ(31) R"imbjson(, "onClose": )imbjson" IMB_SEQ(32) R"imbjson( }, "children": [
            { "id": "x2", "type": "Text", "props": { "text": "two" } } ] } ] },
    { "id": "dlg", "type": "Modal", "props": { "title": "Dialog" }, "bind": { "open": "state.show" },
      "events": { "onOpen": )imbjson" IMB_SEQ(11) R"imbjson(, "onClose": )imbjson" IMB_SEQ(12) R"imbjson( }, "children": [
        { "id": "ok", "type": "Button", "props": { "label": "OK", "size": { "w": 160, "h": 40 } },
          "events": { "onClick": { "actions": [ { "type": "closePopup" } ] } } } ] }
  ] } })imbjson";

#undef IMB_SEQ

std::string EventsDriver() {
    std::string s = "#include \"Events.h\"\n#include \"imforge/imforge_interp.h\"\n#include \"imgui_headless.h\"\n#include <cfloat>\n#include <cstdio>\n";
    s += std::string("static const char* kPanel = R\"imbjson(") + kEventsPanel + ")imbjson\";\n";
    s += R"cpp(
static const int kFrames = 27;
static ImVec2 g_pos[kFrames][3];
struct ScriptClick { int start, target; };
static const ScriptClick kClicks[] = {{4, 0}, {14, 1}, {17, 1}, {20, 2}};
static void Mouse(int f) {
    ImVec2 p(-FLT_MAX, -FLT_MAX);
    bool down = false;
    for (const ScriptClick& c : kClicks)
        if (f >= c.start && f < c.start + 3) {
            p = g_pos[c.start - 1][c.target];
            down = f == c.start + 1;
        }
    ImGui::GetIO().AddMousePosEvent(p.x, p.y);
    ImGui::GetIO().AddMouseButtonEvent(0, down);
}
template <class Set> static void Script(int f, Set set) {
    if (f == 2 || f == 8) set('s', true);
    if (f == 9) set('s', false);
    if (f == 10) set('t', false);
    if (f == 13) set('t', true);
    if (f == 23) set('w', false);
    if (f == 24) set('w', true);
}
int main() {
    {
        imforge::test::HeadlessImGui g;
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        imforge::Json j;
        imforge::Error err;
        imforge::Panel panel;
        if (!imforge::ParseJson(kPanel, j) || !imforge::PanelFromJson(j, panel, &err)) return 3;
        imforge::Project project;
        imforge::Interpreter interp;
        interp.project = &project;
        imforge::PanelInstance inst;
        std::vector<imforge::BindingRoot> roots;
        imforge::InterpOptions opts;
        int frame = 0;
        interp.hooks.on_item = [&](const imforge::Node& n, const ImVec2& mn, const ImVec2& mx, bool) {
            int t = n.id == "ok" ? 0 : n.id == "hdr" ? 1 : n.id == "tree" ? 2 : -1;
            if (t >= 0) g_pos[frame][t] = ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
        };
        interp.EnsureState(panel, inst);
        for (frame = 0; frame < kFrames; ++frame) {
            if (frame > 0)
                for (int t = 0; t < 3; ++t) g_pos[frame][t] = g_pos[frame - 1][t];
            Script(frame, [&](char k, bool v) { *inst.State(k == 's' ? "show" : k == 't' ? "t1" : "win") = imforge::Value(v); });
            *inst.State("seq") = imforge::Value(0);
            Mouse(frame);
            g.BeginFrame();
            interp.DrawPanel(panel, inst, roots, opts);
            g.EndFrame();
            std::printf("interp %d %lld %d\n", frame, inst.State("seq")->ToInt(), inst.State("show")->ToBool() ? 1 : 0);
        }
    }
    {
        imforge::test::HeadlessImGui g;
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        ui::EventsState st;
        for (int f = 0; f < kFrames; ++f) {
            Script(f, [&](char k, bool v) { (k == 's' ? st.show : k == 't' ? st.t1 : st.win) = v; });
            st.seq = 0;
            Mouse(f);
            g.BeginFrame();
            ui::DrawEvents(st);
            g.EndFrame();
            std::printf("gen %d %d %d\n", f, static_cast<int>(st.seq), st.show ? 1 : 0);
        }
    }
    return 0;
}
)cpp";
    return s;
}

}  // namespace

TEST_CASE("open/close events: Modal/Tab/CollapsingHeader/TreeNode/Window track transitions and compile on 1.85, 1.91.9b, 1.93.0") {
    ParityFixture ef(kEventsPanel, {});
    const FileOut* cpp = ef.all.Find("Events.cpp");
    REQUIRE(cpp);
    const std::string& s = cpp->content;
    CHECK(Contains(s, "const int window_prev = ImGui::GetStateStorage()->GetInt(ImGui::GetID(\"window##imb_open\"), -1);"));
    CHECK(Contains(s, "const bool window_now = state.win;"));
    CHECK(Contains(s, "if (!window_now && window_prev == 1) {"));
    CHECK(Contains(s, "bool tab1_now = false;"));
    CHECK(Contains(s, "if (state.t1) {"));
    CHECK(Contains(s, "&state.t1, "));
    CHECK(Contains(s, "tab1_now = true;"));
    CHECK(Contains(s, "if (hdr_now && hdr_prev == 0) {"));
    CHECK(Contains(s, "ImGui::GetStateStorage()->SetInt(ImGui::GetID(\"tree##imb_open\"), tree_now ? 1 : 0);"));
    CHECK(Contains(s, "else if (!dlg_popup && state.show) ImGui::OpenPopup(\"###dlg\");"));
    CHECK(Contains(s, "const bool dlg_now = ImGui::IsPopupOpen(\"###dlg\");"));
    CHECK(!Contains(s, "IsWindowAppearing"));
    CHECK(!Contains(s, "IsItemToggledOpen"));
    if (!HaveGxx()) {
        MESSAGE("g++ not available; skipping compile");
        return;
    }
    for (const std::string& dir : ImGuiCheckouts()) {
        std::string ver = dir.substr(dir.rfind('/') + 1);
        if (ver != "imgui-v1.85" && ver != "imgui-v1.91.9b" && ver != "imgui-v1.93.0") continue;
        CompileResult(ef.all, {}, "events_" + ver, dir, "ui");
    }
}

TEST_CASE("open/close event parity: generated code and interpreter fire the same events on the same frames") {
    ParityFixture ef(kEventsPanel, {});
    std::vector<std::string> gen, interp;
    if (!RunParity("events_parity", "Events", ef.all, EventsDriver(), gen, interp)) return;
    REQUIRE(gen.size() == 27);
    REQUIRE(interp.size() == gen.size());
    for (size_t i = 0; i < gen.size(); ++i) CHECK_MESSAGE(gen[i] == interp[i], "gen    " << gen[i] << "\ninterp " << interp[i]);
    std::vector<int> codes;
    int close_frame_show = -1;
    for (const std::string& l : gen) {
        int f = 0, show = 0;
        long long seq = 0;
        REQUIRE(std::sscanf(l.c_str(), "%d %lld %d", &f, &seq, &show) == 3);
        std::vector<int> frame_codes;
        for (; seq > 0; seq /= 100) frame_codes.insert(frame_codes.begin(), static_cast<int>(seq % 100));
        for (int c : frame_codes) {
            codes.push_back(c);
            if (c == 12 && close_frame_show < 0) close_frame_show = show;
        }
    }
    const std::vector<int> expected = {11, 12, 11, 12, 22, 31, 41, 42, 52, 62, 61};
    std::string got;
    for (int c : codes) got += std::to_string(c) + " ";
    CHECK_MESSAGE(codes == expected, "event codes: " << got);
    CHECK(close_frame_show == 0);
}
#endif
