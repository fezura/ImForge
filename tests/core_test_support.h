#pragma once

#include "doctest.h"

#include "imforge/imforge_core.h"

namespace imforge {
namespace core_test {

inline Json P(const char* text) {
    Json j;
    JsonError e;
    REQUIRE_MESSAGE(ParseJson(text, j, &e), e.message << " at " << e.line << ":" << e.col);
    return j;
}

inline Node ParseNode(const char* text) {
    Node n;
    Error e;
    REQUIRE_MESSAGE(NodeFromJson(P(text), n, &e), e.message);
    return n;
}

inline std::string NodeRoundTrip(const char* text) {
    Node n = ParseNode(text);
    std::string d1 = DumpJson(NodeToJson(n));
    Node n2;
    Error e;
    REQUIRE_MESSAGE(NodeFromJson(P(d1.c_str()), n2, &e), e.message);
    CHECK(DumpJson(NodeToJson(n2)) == d1);
    return d1;
}

inline bool HasDiag(const std::vector<Diagnostic>& diags, Diagnostic::Level lvl, const char* node_id,
                    const char* needle) {
    for (const Diagnostic& d : diags) {
        if (d.level != lvl) continue;
        if (node_id && d.node_id != node_id) continue;
        if (d.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

inline bool HasError(const std::vector<Diagnostic>& diags, const char* node_id, const char* needle) {
    return HasDiag(diags, Diagnostic::Level::Error, node_id, needle);
}

inline int CountLevel(const std::vector<Diagnostic>& diags, Diagnostic::Level lvl) {
    int n = 0;
    for (const Diagnostic& d : diags) n += d.level == lvl ? 1 : 0;
    return n;
}

inline const char* kNodeExample = R"json({
  "id": "btn_play", "type": "Button", "name": "Play button",
  "props": { "label": "{icon:fa-play} Play", "size": { "w": "fill", "h": "auto" } },
  "bind": { "value": "player.health" },
  "visibleIf": "cfg.esp.enabled", "enabledIf": "player.health > 0",
  "tooltip": "Starts the game", "font": "Bold",
  "style": { "colors": { "Button": "#2ecc71ff" }, "vars": { "FrameRounding": 8 }, "classes": ["danger"] },
  "events": { "onClick": { "actions": [ { "type": "toggle", "target": "cfg.esp.enabled" } ], "handler": true } },
  "anim": { "transitions": [ { "prop": "color.Button", "on": "hover", "to": "#27ae60ff", "duration": 0.15, "ease": "OutCubic" } ],
            "enter": { "preset": "FadeSlideUp", "duration": 0.25, "stagger": 0.03 } },
  "fx": [ { "type": "shadow", "size": 12, "color": "#00000080" }, { "type": "blur", "radius": 8 } ],
  "layout": { "anchor": "TopRight", "offset": { "x": -10, "y": 10 } },
  "locked": false,
  "children": []
})json";

inline const char* kPanelExample = R"json({
  "schemaVersion": 1, "kind": "panel", "name": "MainMenu",
  "params": [ { "name": "player", "type": "Player" }, { "name": "cfg", "type": "Settings" } ],
  "state":  [ { "name": "selectedTab", "type": "int", "default": 0 },
              { "name": "open", "type": "bool", "default": true } ],
  "root": { "id": "window", "type": "Window", "props": { "title": "Main Menu", "flags": ["NoCollapse"],
            "initialSize": {"x": 520, "y": 380}, "sizeCond": "FirstUseEver" },
            "bind": { "open": "state.open" }, "children": [
              { "id": "tabs", "type": "TabBar", "children": [
                { "id": "tab_main", "type": "Tab", "props": { "label": "Main" }, "children": [
                  { "id": "hp", "type": "Text", "props": { "text": "HP: {player.health:%.0f}" } },
                  { "id": "esp", "type": "Checkbox", "props": { "label": "ESP" }, "bind": { "value": "cfg.esp.enabled" } },
                  { "id": "sl", "type": "SliderFloat", "props": { "label": "Speed", "min": 0, "max": 10 },
                    "bind": { "value": "player.speed" } }
                ] },
                { "id": "tab_misc", "type": "Tab", "props": { "label": "Misc" }, "children": [
                  { "id": "btn", "type": "Button", "props": { "label": "Reset", "size": { "w": "fill", "h": "auto" } },
                    "events": { "onClick": { "handler": true } } }
                ] }
              ] }
            ] }
})json";

inline const char* kComponentExample = R"json({
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

inline const char* kThemeExample = R"json({ "schemaVersion": 1, "kind": "theme", "name": "Dark",
  "colors": { "WindowBg": "#1e1e1eff", "Text": "#ffffffff", "FutureCol": "#11223344" },
  "vars": { "FrameRounding": 4, "WindowPadding": { "x": 8, "y": 6 }, "Alpha": 0.95 } })json";

inline const char* kProjectExample = R"json({
  "schemaVersion": 1, "kind": "project", "name": "CheatMenu",
  "imgui": { "version": "1.91.9", "branch": "docking" },
  "platform": { "window": "win32", "renderer": "dx11" },
  "features": { "animations": true, "drawlistFx": true, "shaderFx": true, "iconFonts": true,
                "dpiScaling": true, "docking": false },
  "codegen": { "cppStandard": 17, "namespace": "ui", "splitHeader": true,
               "functionCase": "pascal", "variableCase": "snake", "indent": 4, "useTabs": false,
               "outputDir": "generated", "markers": true, "dpiMode": "fontSize", "baseFontSize": 13,
               "runtimeHeader": "imb_runtime.h", "stdString": true },
  "binding": { "compileCommands": "build/compile_commands.json", "includePaths": [], "types": ["Player","Settings"],
               "typesJson": "generated/types.json" },
  "live": { "enabled": true, "transport": "tcp", "tcpPort": 7777, "pipeName": "ImForge", "shmName": "ImForge" },
  "fonts": [ { "name": "Default", "file": "fonts/Inter-Regular.ttf", "size": 16, "ranges": "default",
               "mergeIcons": "fontawesome6" } ],
  "themes": [ "themes/Dark.theme.json" ], "activeTheme": "Dark",
  "styleClasses": { "danger": { "colors": { "Button": "#c0392bff" }, "vars": { "FrameRounding": 6 } } },
  "panels": [ "panels/MainMenu.imb.json" ], "components": [ "components/LabeledSlider.imc.json" ],
  "libraries": [ "libs/essentials.imblib" ]
})json";

inline Project MakeProject() {
    Project p;
    Error e;
    REQUIRE_MESSAGE(ProjectFromJson(P(kProjectExample), p, &e), e.message);
    Component c;
    REQUIRE_MESSAGE(ComponentFromJson(P(kComponentExample), c, &e), e.message);
    p.components.push_back(c);
    p.fonts.push_back(FontSpec{"Bold", "fonts/Inter-Bold.ttf", "default", "", 16});
    return p;
}

inline Panel MakePanel(const char* root_json) {
    Panel p;
    p.name = "P";
    p.params.push_back(Param{"player", "Player", Json()});
    p.params.push_back(Param{"cfg", "Settings", Json()});
    p.state.push_back(StateVar{"open", "bool", Json(true)});
    p.state.push_back(StateVar{"selectedTab", "int", Json(0)});
    p.root = ParseNode(root_json);
    return p;
}

inline std::vector<Diagnostic> Validate(const char* root_json, bool* ok_out = nullptr) {
    Project project = MakeProject();
    Panel panel = MakePanel(root_json);
    std::vector<Diagnostic> diags;
    bool ok = ValidatePanel(project, panel, diags);
    if (ok_out) *ok_out = ok;
    return diags;
}

}  // namespace core_test
}  // namespace imforge
