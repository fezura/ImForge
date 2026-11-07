#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

#include "../builder_fonts.h"
#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/icon_table.h"
#include "../ui/panel_refactor.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

const char* const kStateTypes[] = {"bool", "int", "float", "double", "string", "vec2", "vec4", "color"};
constexpr const char* kUnknownTypeMsg = "type not in types.json \xE2\x80\x94 bindings can't be type-checked";

bool IsNumericType(const std::string& t) { return t == "int" || t == "float" || t == "double"; }

Json Vec(std::initializer_list<const char*> keys, float v) {
    Json o = Json::Object();
    for (const char* k : keys) o.Set(k, Json(0.0 + v));
    return o;
}

Json DefaultFor(const std::string& t) {
    if (t == "bool") return Json(false);
    if (IsNumericType(t)) return Json(0);
    if (t == "string") return Json("");
    if (t == "vec2") return Vec({"x", "y"}, 0);
    if (t == "vec4") return Vec({"x", "y", "z", "w"}, 0);
    if (t == "color") return Json("#ffffffff");
    return Json();
}

Json ConvertDefault(const StateVar& v, const std::string& to) {
    if (IsNumericType(v.type) && IsNumericType(to) && v.def.IsNumber())
        return to == "int" ? Json(v.def.AsInt(0)) : Json(v.def.AsDouble(0));
    if ((v.type == "vec4" || v.type == "color") && (to == "vec4" || to == "color")) {
        Vec4 c = v.def.IsString() ? ParseColor(v.def.AsString())
                                  : Vec4{(float)v.def["x"].AsDouble(0), (float)v.def["y"].AsDouble(0), (float)v.def["z"].AsDouble(0),
                                         (float)v.def["w"].AsDouble(0)};
        if (to == "color") return Json(ColorToHex(c));
        Json o = Json::Object();
        o.Set("x", Json(core_detail::FloatToNum(c.x)));
        o.Set("y", Json(core_detail::FloatToNum(c.y)));
        o.Set("z", Json(core_detail::FloatToNum(c.z)));
        o.Set("w", Json(core_detail::FloatToNum(c.w)));
        return o;
    }
    return DefaultFor(to);
}

template <class T>
std::string UniqueName(const std::vector<T>& list, const std::string& base) {
    auto taken = [&](const std::string& n) {
        for (const T& e : list)
            if (e.name == n) return true;
        return n == "state";
    };
    if (!taken(base)) return base;
    for (int n = 2;; ++n) {
        std::string cand = base + std::to_string(n);
        if (!taken(cand)) return cand;
    }
}

template <class T>
std::string NameError(const std::vector<T>& list, size_t index, const std::string& name, bool is_param) {
    if (!core_detail::IsIdentifier(name)) return "invalid identifier";
    if (is_param && name == "state") return "\"state\" is reserved for state variables";
    for (size_t i = 0; i < list.size(); ++i)
        if (i != index && list[i].name == name) return "duplicate name";
    return std::string();
}

template <class T>
size_t CountNamed(const std::vector<T>& list, const std::string& name) {
    size_t n = 0;
    for (const T& e : list) n += e.name == name ? 1 : 0;
    return n;
}

std::string LowerFirst(std::string s) {
    if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') s[0] = (char)(s[0] - 'A' + 'a');
    return s;
}

void RenameInstanceKeys(EditorContext& ctx, const std::string& panel, bool state, const std::string& from, const std::string& to) {
#if IMFORGE_HAS_INTERP
    auto it = ctx.instances.find("panel:" + panel);
    if (it == ctx.instances.end()) return;
    if (state) {
        for (auto& kv : it->second.state)
            if (kv.first == from) kv.first = to;
        return;
    }
    for (auto& kv : it->second.mock) {
        const std::string& k = kv.first;
        bool hit = k.compare(0, from.size(), from) == 0 && (k.size() == from.size() || k[from.size()] == '.' || k[from.size()] == '[');
        if (hit) kv.first = to + k.substr(from.size());
    }
#else
    (void)ctx;
    (void)panel;
    (void)state;
    (void)from;
    (void)to;
#endif
}

void IssuePill(const char* text, const ImVec4& color, const std::string& tip) {
    ui::Badge(text, color);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
}

class PanelSettingsPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Panel Settings"; }
    const char* DefaultDock() const override { return "right"; }
    const char* Icon() const override {
#ifdef ICON_FA_SLIDERS
        return ICON_FA_SLIDERS;
#else
        return nullptr;
#endif
    }
    void Draw(EditorContext& ctx) override;
    void OnProjectChanged(EditorContext&) override {
        edit_id_ = 0;
        last_panel_.clear();
    }

private:
    ImGuiID edit_id_ = 0;
    int edit_seen_frame_ = 0;
    std::string edit_buf_, scratch_, last_panel_;
    std::vector<std::function<void()>> deferred_;

    template <class F>
    void Defer(EditorContext& ctx, Panel& panel, F f) {
        EditorContext* c = &ctx;
        Panel* p = &panel;
        size_t pc = panel.params.size(), sc = panel.state.size();
        deferred_.push_back([c, p, pc, sc, f] {
            if (p->params.size() == pc && p->state.size() == sc) f(*c, *p);
        });
    }

    bool CommitField(const char* id, const std::string& current, std::string& out, const char* hint);
    void DrawHeader(EditorContext& ctx, const Panel& panel);
    void DrawParams(EditorContext& ctx, Panel& panel, const std::vector<PathRef>& refs);
    void DrawParamType(EditorContext& ctx, Panel& panel, size_t i, const std::vector<std::string>& suggestions);
    void DrawState(EditorContext& ctx, Panel& panel, const std::vector<PathRef>& refs);
    bool DrawDefaultEditor(StateVar& v, Json& out);
    void DrawSignature(EditorContext& ctx, const Panel& panel);
    void DrawUsage(EditorContext& ctx, const Panel& panel, const std::vector<PathRef>& refs);
    void DrawRowButtons(EditorContext& ctx, Panel& panel, bool params, size_t i, size_t count, int uses);

    void RenameParam(EditorContext& ctx, Panel& panel, size_t i, const std::string& name);
    void RenameStateVar(EditorContext& ctx, Panel& panel, size_t i, const std::string& name);
    void AddParam(EditorContext& ctx, Panel& panel, const std::vector<std::string>& suggestions);
};

std::vector<std::string> TypeSuggestions(const EditorContext& ctx) {
    std::vector<std::string> out = ctx.types.StructNames();
    for (const std::string& t : ctx.project.binding_types) out.push_back(t);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool KnownType(EditorContext& ctx, const std::string& type) {
    for (const std::string& s : ctx.types.StructNames())
        if (s == type) return true;
#if IMFORGE_HAS_TYPEDB
    CType ct;
    if (ctx.types.ResolveTypeName(type, ct)) return true;
#endif
    return false;
}

bool PanelSettingsPanel::CommitField(const char* id, const std::string& current, std::string& out, const char* hint) {
    ImGuiID wid = ImGui::GetID(id);
    bool editing = edit_id_ == wid;
    if (!editing) scratch_ = current;
    std::string& buf = editing ? edit_buf_ : scratch_;
    ui::InputTextWithHintStd(id, hint ? hint : "", buf, ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemActivated()) {
        edit_id_ = wid;
        edit_buf_ = buf;
    }
    if (edit_id_ == wid) edit_seen_frame_ = ImGui::GetFrameCount();
    if (ImGui::IsItemDeactivated() && edit_id_ == wid) {
        edit_id_ = 0;
        out = edit_buf_;
        return out != current;
    }
    return false;
}

void PanelSettingsPanel::Draw(EditorContext& ctx) {
    if (!ctx.has_project) {
        ui::EmptyState(IconUtf8("fa-sliders"), "No project open", "Parameters, state and window options of the active panel appear here.");
        return;
    }
    Panel* panel = ctx.ActivePanel();
    if (!panel) {
        ImGui::Spacing();
        ui::TextDim(ctx.active_kind == DocKind::Component ? "Components are configured in the Components panel."
                                                          : "Select a panel (Panels tab) to edit its parameters and state.");
        return;
    }
    if (panel->name != last_panel_ || (edit_id_ && ImGui::GetFrameCount() - edit_seen_frame_ > 1)) {
        edit_id_ = 0;
        last_panel_ = panel->name;
    }
    std::vector<PathRef> refs = CollectPathRefs(*panel, &ctx.project);
    DrawHeader(ctx, *panel);
    if (ui::Section("Parameters")) DrawParams(ctx, *panel, refs);
    if (ui::Section("State")) DrawState(ctx, *panel, refs);
    if (ui::Section("Generated signature")) DrawSignature(ctx, *panel);
    if (ui::Section("Usage", false)) DrawUsage(ctx, *panel, refs);
    std::vector<std::function<void()>> ops = std::move(deferred_);
    deferred_.clear();
    for (std::function<void()>& op : ops) op();
}

void PanelSettingsPanel::DrawHeader(EditorContext& ctx, const Panel& panel) {
    ui::HeadingText(ctx.Font(FontSlot::Heading), panel.name.c_str());
    ui::TextDim("%s  \xC2\xB7  %d params  \xC2\xB7  %d state vars", panel.file.empty() ? "(unsaved)" : panel.file.c_str(),
                (int)panel.params.size(), (int)panel.state.size());
    if (ctx.types.Empty())
        ui::TextDim("No types.json loaded: parameter types and bindings can't be type-checked.");
    ImGui::Spacing();
}

void PanelSettingsPanel::DrawRowButtons(EditorContext& ctx, Panel& panel, bool params, size_t i, size_t count, int uses) {
    ImGui::BeginDisabled(i == 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        Defer(ctx, panel, [params, i](EditorContext& c, Panel& p) {
            c.Edit(params ? "Reorder parameters" : "Reorder state", [&] {
                if (params) std::swap(p.params[i], p.params[i - 1]);
                else std::swap(p.state[i], p.state[i - 1]);
            });
        });
    ImGui::EndDisabled();
    ImGui::SameLine(0, 2);
    ImGui::BeginDisabled(i + 1 >= count);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        Defer(ctx, panel, [params, i](EditorContext& c, Panel& p) {
            c.Edit(params ? "Reorder parameters" : "Reorder state", [&] {
                if (params) std::swap(p.params[i], p.params[i + 1]);
                else std::swap(p.state[i], p.state[i + 1]);
            });
        });
    ImGui::EndDisabled();
    ImGui::SameLine(0, 2);
    if (ImGui::Button("x##remove", ImVec2(ImGui::GetFrameHeight(), 0)))
        Defer(ctx, panel, [params, i](EditorContext& c, Panel& p) {
            c.Edit(params ? "Remove parameter" : "Remove state variable", [&] {
                if (params) p.params.erase(p.params.begin() + (long)i);
                else p.state.erase(p.state.begin() + (long)i);
            });
        });
    if (uses > 0) ui::Tooltip("Remove (referenced in the panel; those bindings become unresolved)");
    else ui::Tooltip("Remove");
}

void PanelSettingsPanel::DrawParams(EditorContext& ctx, Panel& panel, const std::vector<PathRef>& refs) {
    std::vector<std::string> suggestions = TypeSuggestions(ctx);
    const BuilderColors& col = Colors();
    ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
    if (!panel.params.empty() && ImGui::BeginTable("##params", 4, tf)) {
        float fh = ImGui::GetFrameHeight();
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("C++ type", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("##issues", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.2f);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, fh * 3 + 6);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < panel.params.size(); ++i) {
            Param& prm = panel.params[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string nn;
            if (CommitField("##name", prm.name, nn, "name")) RenameParam(ctx, panel, i, nn);
            ImGui::TableSetColumnIndex(1);
            DrawParamType(ctx, panel, i, suggestions);
            ImGui::TableSetColumnIndex(2);
            int uses = 0;
            for (const PathRef& r : refs) uses += r.root == prm.name ? 1 : 0;
            std::string err = NameError(panel.params, i, prm.name, true);
            if (!err.empty()) IssuePill("!", col.error, "Name: " + err);
            else if (prm.type.empty()) IssuePill("!", col.error, "Missing C++ type");
            else if (!KnownType(ctx, prm.type)) IssuePill("?", col.warning, kUnknownTypeMsg);
            else IssuePill(std::to_string(uses).c_str(), col.text_dim, std::to_string(uses) + " references in this panel");
            ImGui::TableSetColumnIndex(3);
            DrawRowButtons(ctx, panel, true, i, panel.params.size(), uses);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (panel.params.empty()) ui::TextDim("No parameters. Parameters are the C++ objects the panel binds to.");
    if (ImGui::Button("+ Add parameter")) AddParam(ctx, panel, suggestions);
    ImGui::Spacing();
}

void PanelSettingsPanel::DrawParamType(EditorContext& ctx, Panel& panel, size_t i, const std::vector<std::string>& suggestions) {
    float arrow = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - arrow, arrow));
    std::string nt;
    if (CommitField("##type", panel.params[i].type, nt, "C++ type"))
        Defer(ctx, panel, [i, nt](EditorContext& c, Panel& p) { c.Edit("Parameter type", [&] { p.params[i].type = nt; }); });
    ImGui::SameLine(0, 0);
    if (ImGui::BeginCombo("##type_pick", nullptr, ImGuiComboFlags_NoPreview | ImGuiComboFlags_PopupAlignLeft | ImGuiComboFlags_HeightLarge)) {
        if (suggestions.empty()) ui::TextDim("No struct types known (load types.json)");
        for (const std::string& s : suggestions) {
            bool sel = s == panel.params[i].type;
            if (ImGui::Selectable(s.c_str(), sel))
                Defer(ctx, panel, [i, s](EditorContext& c, Panel& p) { c.Edit("Parameter type", [&] { p.params[i].type = s; }); });
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ui::Tooltip("Pick a struct from types.json or type any C++ type");
}

void PanelSettingsPanel::AddParam(EditorContext& ctx, Panel& panel, const std::vector<std::string>& suggestions) {
    std::string type;
    for (const std::string& s : suggestions) {
        bool used = false;
        for (const Param& p : panel.params) used = used || p.type == s;
        if (!used) {
            type = s;
            break;
        }
    }
    Param p;
    p.type = type.empty() ? std::string("Data") : type;
    std::string base = LowerFirst(p.type);
    p.name = UniqueName(panel.params, core_detail::IsIdentifier(base) ? base : std::string("param"));
    ctx.Edit("Add parameter", [&] { panel.params.push_back(p); });
}

void PanelSettingsPanel::RenameParam(EditorContext& ctx, Panel& panel, size_t i, const std::string& name) {
    std::string err = NameError(panel.params, i, name, true);
    if (!err.empty()) {
        ctx.SetStatus("Parameter \"" + name + "\": " + err);
        return;
    }
    std::string old = panel.params[i].name;
    std::string panel_name = panel.name;
    bool simple = old.empty() || CountNamed(panel.params, old) > 1;
    Defer(ctx, panel, [i, old, name, simple, panel_name](EditorContext& c, Panel& p) {
        int n = 0;
        c.Edit("Rename parameter", [&] {
            if (simple) p.params[i].name = name;
            else n = RenameRootInPanel(p, old, name, &c.project);
        });
        RenameInstanceKeys(c, panel_name, false, old, name);
        c.SetStatus("Renamed parameter " + old + " to " + name + " (" + std::to_string(n) + " references updated)");
    });
}

void PanelSettingsPanel::RenameStateVar(EditorContext& ctx, Panel& panel, size_t i, const std::string& name) {
    std::string err = NameError(panel.state, i, name, false);
    if (!err.empty()) {
        ctx.SetStatus("State variable \"" + name + "\": " + err);
        return;
    }
    std::string old = panel.state[i].name;
    std::string panel_name = panel.name;
    bool simple = old.empty() || CountNamed(panel.state, old) > 1;
    Defer(ctx, panel, [i, old, name, simple, panel_name](EditorContext& c, Panel& p) {
        int n = 0;
        c.Edit("Rename state variable", [&] {
            if (simple) p.state[i].name = name;
            else n = RenameStateVarInPanel(p, old, name, &c.project);
        });
        RenameInstanceKeys(c, panel_name, true, old, name);
        c.SetStatus("Renamed state." + old + " to state." + name + " (" + std::to_string(n) + " references updated)");
    });
}

bool PanelSettingsPanel::DrawDefaultEditor(StateVar& v, Json& out) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string& t = v.type;
    if (t == "bool") {
        bool b = v.def.IsBool() ? v.def.b : v.def.AsDouble(0) != 0;
        if (!ImGui::Checkbox("##def", &b)) return false;
        out = Json(b);
        return true;
    }
    if (t == "int") {
        int x = v.def.AsInt(0);
        if (!ImGui::InputInt("##def", &x)) return false;
        out = Json(x);
        return true;
    }
    if (t == "float" || t == "double") {
        double x = v.def.AsDouble(0);
        if (!ImGui::InputDouble("##def", &x, 0.0, 0.0, "%g")) return false;
        out = t == "float" ? Json(core_detail::FloatToNum((float)x)) : Json(x);
        return true;
    }
    if (t == "string") {
        std::string s = v.def.AsString("");
        if (!ui::InputTextStd("##def", s)) return false;
        out = Json(s);
        return true;
    }
    if (t == "vec2") {
        float xy[2] = {(float)v.def["x"].AsDouble(0), (float)v.def["y"].AsDouble(0)};
        if (!ImGui::DragFloat2("##def", xy, 0.1f)) return false;
        out = Json::Object();
        out.Set("x", Json(core_detail::FloatToNum(xy[0])));
        out.Set("y", Json(core_detail::FloatToNum(xy[1])));
        return true;
    }
    if (t == "vec4") {
        float q[4] = {(float)v.def["x"].AsDouble(0), (float)v.def["y"].AsDouble(0), (float)v.def["z"].AsDouble(0),
                      (float)v.def["w"].AsDouble(0)};
        if (!ImGui::DragFloat4("##def", q, 0.01f)) return false;
        out = Json::Object();
        const char* keys[4] = {"x", "y", "z", "w"};
        for (int k = 0; k < 4; ++k) out.Set(keys[k], Json(core_detail::FloatToNum(q[k])));
        return true;
    }
    if (t == "color") {
        Vec4 c = v.def.IsString() ? ParseColor(v.def.AsString()) : Vec4{1, 1, 1, 1};
        float rgba[4] = {c.x, c.y, c.z, c.w};
        if (!ImGui::ColorEdit4("##def", rgba, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar)) return false;
        out = Json(ColorToHex(Vec4{rgba[0], rgba[1], rgba[2], rgba[3]}));
        return true;
    }
    ui::TextDim("unknown type");
    return false;
}

void PanelSettingsPanel::DrawState(EditorContext& ctx, Panel& panel, const std::vector<PathRef>& refs) {
    const BuilderColors& col = Colors();
    ImGuiTableFlags tf = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
    if (!panel.state.empty() && ImGui::BeginTable("##state", 5, tf)) {
        float fh = ImGui::GetFrameHeight();
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.0f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("##issues", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.2f);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, fh * 3 + 6);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < panel.state.size(); ++i) {
            StateVar& v = panel.state[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string nn;
            if (CommitField("##name", v.name, nn, "name")) RenameStateVar(ctx, panel, i, nn);
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##type", v.type.empty() ? "?" : v.type.c_str())) {
                for (const char* t : kStateTypes) {
                    if (ImGui::Selectable(t, v.type == t) && v.type != t) {
                        std::string to = t;
                        Defer(ctx, panel, [i, to](EditorContext& c, Panel& p) {
                            c.Edit("State type", [&] {
                                StateVar& sv = p.state[i];
                                sv.def = ConvertDefault(sv, to);
                                sv.type = to;
                            });
                        });
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TableSetColumnIndex(2);
            Json nd;
            if (DrawDefaultEditor(v, nd)) ctx.Edit("State default", [&] { v.def = nd; });
            ImGui::TableSetColumnIndex(3);
            int uses = 0;
            for (const PathRef& r : refs) uses += r.root == "state" && r.member == v.name ? 1 : 0;
            std::string err = NameError(panel.state, i, v.name, false);
            bool type_ok = std::find_if(std::begin(kStateTypes), std::end(kStateTypes), [&](const char* t) { return v.type == t; }) !=
                           std::end(kStateTypes);
            if (!err.empty()) IssuePill("!", col.error, "Name: " + err);
            else if (!type_ok) IssuePill("!", col.error, "Unknown state type \"" + v.type + "\"");
            else IssuePill(std::to_string(uses).c_str(), col.text_dim, std::to_string(uses) + " references to state." + v.name);
            ImGui::TableSetColumnIndex(4);
            DrawRowButtons(ctx, panel, false, i, panel.state.size(), uses);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (panel.state.empty()) ui::TextDim("No state variables. State lives in the generated <Name>State struct.");
    if (ImGui::Button("+ Add state variable")) {
        StateVar v;
        v.name = UniqueName(panel.state, "flag");
        v.type = "bool";
        v.def = Json(false);
        ctx.Edit("Add state variable", [&] { panel.state.push_back(v); });
    }
    ImGui::Spacing();
}

void PanelSettingsPanel::DrawSignature(EditorContext& ctx, const Panel& panel) {
    std::string sig = PanelSignaturePreview(panel, ctx.project.codegen);
    int lines = (int)std::count(sig.begin(), sig.end(), '\n') + 1;
    ImFont* mono = ctx.Font(FontSlot::Mono);
    if (!mono) mono = GetFonts().mono;
    if (mono) ImGui::PushFont(mono);
    float h = ImGui::GetTextLineHeight() * (float)lines + ImGui::GetStyle().FramePadding.y * 2;
    ui::InputTextMultilineStd("##signature", sig, ImVec2(-FLT_MIN, h), ImGuiInputTextFlags_ReadOnly);
    if (mono) ImGui::PopFont();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(sig.c_str());
    ImGui::SameLine();
    ui::TextDim("naming: functions %s, variables %s", ctx.project.codegen.function_case.c_str(),
                ctx.project.codegen.variable_case.c_str());
    ImGui::Spacing();
}

void PanelSettingsPanel::DrawUsage(EditorContext& ctx, const Panel& panel, const std::vector<PathRef>& refs) {
    struct Group {
        std::string label;
        std::vector<const PathRef*> items;
        bool unresolved = false;
    };
    std::vector<Group> groups;
    for (const Param& p : panel.params) {
        Group g;
        g.label = p.name;
        for (const PathRef& r : refs)
            if (r.root == p.name) g.items.push_back(&r);
        groups.push_back(std::move(g));
    }
    for (const StateVar& v : panel.state) {
        Group g;
        g.label = "state." + v.name;
        for (const PathRef& r : refs)
            if (r.root == "state" && r.member == v.name) g.items.push_back(&r);
        groups.push_back(std::move(g));
    }
    Group unknown;
    unknown.label = "unresolved";
    unknown.unresolved = true;
    for (const PathRef& r : refs) {
        bool known = false;
        if (r.root == "state") {
            for (const StateVar& v : panel.state) known = known || v.name == r.member;
        } else {
            for (const Param& p : panel.params) known = known || p.name == r.root;
        }
        if (!known) unknown.items.push_back(&r);
    }
    if (!unknown.items.empty()) groups.push_back(std::move(unknown));
    if (groups.empty()) {
        ui::TextDim("Declare parameters or state variables to see where they are used.");
        return;
    }
    const BuilderColors& col = Colors();
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        const Group& g = groups[gi];
        ImGui::PushID((int)gi);
        if (g.unresolved) ImGui::PushStyleColor(ImGuiCol_Text, col.warning);
        bool expanded = ImGui::TreeNodeEx("##group", ImGuiTreeNodeFlags_SpanAvailWidth | (g.items.empty() ? ImGuiTreeNodeFlags_Leaf : 0),
                                      "%s  (%d)", g.label.c_str(), (int)g.items.size());
        if (g.unresolved) {
            ImGui::PopStyleColor();
            ui::Tooltip("References to roots or state variables this panel does not declare");
        }
        if (expanded) {
            for (size_t k = 0; k < g.items.size(); ++k) {
                const PathRef& r = *g.items[k];
                if (k > 0 && g.items[k - 1]->node_id == r.node_id && g.items[k - 1]->where == r.where &&
                    (!g.unresolved || g.items[k - 1]->root == r.root))
                    continue;
                std::string text = r.node_id + "  \xC2\xB7  " + r.where;
                if (g.unresolved) text += "  (" + (r.root == "state" ? "state." + r.member : r.root) + ")";
                ImGui::PushID((int)k);
                if (ImGui::Selectable(text.c_str(), ctx.IsSelected(r.node_id))) ctx.Select(r.node_id);
                if (ImGui::IsItemHovered()) ctx.hovered_id = r.node_id;
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

}  // namespace

}  // namespace imforge::builder

IMFORGE_REGISTER_PANEL(imforge::builder::PanelSettingsPanel, 5);
