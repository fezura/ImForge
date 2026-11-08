#pragma once

#include <algorithm>

#include "imforge_core.h"

namespace imforge {

namespace core_detail {

inline bool InPipeList(const char* list, const std::string& s) {
    if (!list) return false;
    for (const char* p = list; *p;) {
        const char* e = p;
        while (*e && *e != '|') ++e;
        if ((size_t)(e - p) == s.size() && std::memcmp(p, s.data(), s.size()) == 0) return true;
        p = *e ? e + 1 : e;
    }
    return false;
}

inline bool IsStateType(const std::string& t) {
    return InPipeList("bool|int|float|double|string|vec2|vec4|color", t);
}

inline bool IsComponentParamType(const std::string& t) {
    if (InPipeList("string|int|float|bool|color|vec2|vec4", t)) return true;
    if (t.compare(0, 5, "enum:") == 0) return t.size() > 5;
    if (t.compare(0, 8, "binding<") == 0 && t.size() > 9 && t.back() == '>')
        return InPipeList("bool|int|float|string|vec2|vec4|color|any", t.substr(8, t.size() - 9));
    return false;
}

inline bool IsDockingOnlyFlag(const char* enum_name, const std::string& flag) {
    enum_name = detail::StripImGuiPrefix(enum_name);
    return enum_name && std::strcmp(enum_name, "WindowFlags") == 0 && flag == "NoDocking";
}

inline bool IsValidColorJson(const Json& v) {
    bool ok = false;
    if (v.IsString()) ParseColor(v.AsString(), &ok);
    return ok;
}

struct Validator {
    const Project& project;
    const Component* def;
    const std::vector<Param>& params;
    const std::vector<StateVar>* state;
    std::string panel_name;
    std::vector<Diagnostic>& out;
    std::vector<std::pair<std::string, std::string>> id_types;
    int version = 0;
    bool ok = true;

    Validator(const Project& p, const Component* d, const std::vector<Param>& prm, const std::vector<StateVar>* st,
              const std::string& name, std::vector<Diagnostic>& o)
        : project(p), def(d), params(prm), state(st), panel_name(name), out(o), version(p.ImGuiVersionNum()) {}

    void Report(Diagnostic::Level lvl, const std::string& node_id, const std::string& prop, const std::string& msg) {
        Diagnostic d;
        d.level = lvl;
        d.message = msg;
        d.panel = panel_name;
        d.node_id = node_id;
        d.prop = prop;
        if (lvl == Diagnostic::Level::Error) ok = false;
        out.push_back(std::move(d));
    }
    void Err(const Node& n, const std::string& prop, const std::string& msg) {
        Report(Diagnostic::Level::Error, n.id, prop, msg);
    }
    void Warn(const Node& n, const std::string& prop, const std::string& msg) {
        Report(Diagnostic::Level::Warning, n.id, prop, msg);
    }

    const std::string* TypeOfId(const std::string& id) const {
        for (const auto& it : id_types)
            if (it.first == id) return &it.second;
        return nullptr;
    }
    bool HasParam(const std::string& name) const {
        for (const Param& p : params)
            if (p.name == name) return true;
        return false;
    }

    void CheckDeclarations() {
        std::vector<std::string> seen;
        for (const Param& p : params) {
            if (!IsIdentifier(p.name)) Report(Diagnostic::Level::Error, "", p.name, "invalid param name \"" + p.name + "\"");
            if (std::find(seen.begin(), seen.end(), p.name) != seen.end())
                Report(Diagnostic::Level::Error, "", p.name, "duplicate param name \"" + p.name + "\"");
            seen.push_back(p.name);
            if (def && !IsComponentParamType(p.type))
                Report(Diagnostic::Level::Error, "", p.name, "invalid component param type \"" + p.type + "\"");
        }
        if (!state) return;
        seen.clear();
        for (const StateVar& s : *state) {
            if (!IsIdentifier(s.name)) Report(Diagnostic::Level::Error, "", s.name, "invalid state name \"" + s.name + "\"");
            if (std::find(seen.begin(), seen.end(), s.name) != seen.end())
                Report(Diagnostic::Level::Error, "", s.name, "duplicate state name \"" + s.name + "\"");
            seen.push_back(s.name);
            if (!IsStateType(s.type))
                Report(Diagnostic::Level::Error, "", s.name, "invalid state type \"" + s.type + "\"");
        }
    }

    void CheckIds(const Node& root) {
        Walk(root, [&](const Node& n) {
            if (!IsIdentifier(n.id)) Err(n, "id", "invalid node id \"" + n.id + "\"");
            else if (TypeOfId(n.id)) Err(n, "id", "duplicate node id \"" + n.id + "\"");
            id_types.emplace_back(n.id, n.type);
        });
    }

    void CheckPath(const Node& n, const std::string& prop, const std::string& path) {
        size_t e = 0;
        while (e < path.size() && path[e] != '.' && path[e] != '[') ++e;
        std::string root = path.substr(0, e);
        if (!IsIdentifier(root)) {
            Err(n, prop, "invalid binding path \"" + path + "\"");
            return;
        }
        if (def) {
            Err(n, prop, "component trees must reference bindings through {\"param\": ...}, got \"" + path + "\"");
            return;
        }
        if (root == "state") {
            size_t b = e + 1, f = b;
            while (f < path.size() && path[f] != '.' && path[f] != '[') ++f;
            std::string var = e < path.size() && path[e] == '.' ? path.substr(b, f - b) : std::string();
            bool found = false;
            if (state)
                for (const StateVar& s : *state) found = found || s.name == var;
            if (!found) Err(n, prop, "unknown state variable in path \"" + path + "\"");
            return;
        }
        if (!HasParam(root)) Err(n, prop, "binding path root \"" + root + "\" is not a panel param or \"state\"");
    }

    void CheckParamRef(const Node& n, const std::string& prop, const PropValue& pv) {
        if (!def) Err(n, prop, "{\"param\": ...} is only allowed inside a component definition");
        else if (!HasParam(pv.Param())) Err(n, prop, "unknown component param \"" + pv.Param() + "\"");
    }

    void CheckFlags(const Node& n, const std::string& prop, const char* enum_name, const Json& v) {
        if (!v.IsArray()) {
            Err(n, prop, "expected an array of flag names");
            return;
        }
        size_t fc = 0;
        const char* en = enum_name ? enum_name : "";
        const FlagDef* fd = FlagsFor(en, fc);
        for (const Json& f : v.arr) {
            if (!f.IsString()) {
                Err(n, prop, "flag names must be strings");
                continue;
            }
            if (!fd) continue;
            const FlagDef* found = nullptr;
            for (size_t i = 0; i < fc; ++i)
                if (f.AsString() == fd[i].name) found = &fd[i];
            if (!found) {
                if (!IsDockingOnlyFlag(en, f.AsString()))
                    Warn(n, prop, "unknown flag \"" + f.AsString() + "\" for " + en);
                else if (project.imgui_branch != "docking")
                    Warn(n, prop, "flag \"" + f.AsString() + "\" requires the docking branch");
            } else if (found->min_version > version)
                Warn(n, prop, "flag \"" + f.AsString() + "\" requires ImGui >= " + Project::VersionString(found->min_version));
        }
    }

    void CheckSizeObject(const Node& n, const std::string& prop, const Json& v) {
        if (!v.IsObject()) {
            Err(n, prop, "expected a size object {\"w\", \"h\"}");
            return;
        }
        SizeSpec s;
        if (v.Has("w") && !SizeSpec::Parse(v["w"], s)) Err(n, prop, "invalid SizeSpec for \"w\"");
        if (v.Has("h") && !SizeSpec::Parse(v["h"], s)) Err(n, prop, "invalid SizeSpec for \"h\"");
    }

    void CheckLiteral(const Node& n, const std::string& prop, const PropDef& pd, const Json& v) {
        if (v.IsNull()) return;
        SizeSpec s;
        switch (pd.type) {
        case PropType::Bool:
            if (!v.IsBool()) Err(n, prop, "expected a boolean");
            break;
        case PropType::Int:
        case PropType::Float:
            if (!v.IsNumber()) Err(n, prop, "expected a number");
            else if ((pd.min != 0 || pd.max != 0) && (v.AsDouble() < pd.min || v.AsDouble() > pd.max))
                Err(n, prop, "value out of range [" + FormatFloat(pd.min) + ", " + FormatFloat(pd.max) + "]");
            break;
        case PropType::String:
        case PropType::Code:
            if (!v.IsString()) Err(n, prop, "expected a string");
            break;
        case PropType::Expr:
            if (!v.IsString() && !v.IsNumber() && !v.IsBool()) Err(n, prop, "expected an expression or literal");
            break;
        case PropType::Color:
            if (!IsValidColorJson(v)) Err(n, prop, "invalid color (expected \"#rrggbb\" or \"#rrggbbaa\")");
            break;
        case PropType::Vec2:
            if (!(v.IsObject() && v["x"].IsNumber() && v["y"].IsNumber())) Err(n, prop, "expected {\"x\", \"y\"}");
            break;
        case PropType::Vec4:
            if (!(v.IsObject() && v["x"].IsNumber() && v["y"].IsNumber() && v["z"].IsNumber() && v["w"].IsNumber()))
                Err(n, prop, "expected {\"x\", \"y\", \"z\", \"w\"}");
            break;
        case PropType::Size: CheckSizeObject(n, prop, v); break;
        case PropType::Enum:
            if (!v.IsString() || !InPipeList(pd.enum_values, v.AsString()))
                Err(n, prop, "expected one of " + std::string(pd.enum_values ? pd.enum_values : ""));
            break;
        case PropType::Flags: CheckFlags(n, prop, pd.enum_values, v); break;
        case PropType::StringList:
            if (!v.IsArray()) Err(n, prop, "expected an array of strings");
            else
                for (const Json& e : v.arr)
                    if (!e.IsString()) Err(n, prop, "expected an array of strings");
            break;
        case PropType::SizeList:
            if (!v.IsArray()) Err(n, prop, "expected an array of SizeSpecs");
            else
                for (const Json& e : v.arr)
                    if (!SizeSpec::Parse(e, s)) Err(n, prop, "invalid SizeSpec in list");
            break;
        }
    }

    void CheckProps(const Node& n, const PropDef* defs, int count) {
        for (const auto& p : n.props) {
            const PropDef* pd = nullptr;
            for (int i = 0; i < count; ++i)
                if (p.first == defs[i].name) pd = &defs[i];
            if (!pd) {
                Err(n, p.first, "unknown prop \"" + p.first + "\" for " + n.type);
                continue;
            }
            if (pd->min_version > version)
                Warn(n, p.first, "prop \"" + p.first + "\" requires ImGui >= " + Project::VersionString(pd->min_version));
            if (p.second.IsParam()) CheckParamRef(n, p.first, p.second);
            else if (!p.second.IsExpr()) CheckLiteral(n, p.first, *pd, p.second.raw);
        }
    }

    void CheckBindValue(const Node& n, const std::string& slot, const PropValue& pv) {
        if (pv.IsParam()) CheckParamRef(n, slot, pv);
        else if (pv.raw.IsString()) CheckPath(n, slot, pv.raw.AsString());
        else Err(n, slot, "binding must be a path string or {\"param\": ...}");
    }

    void CheckBinds(const Node& n, const BindDef* defs, int count) {
        for (const auto& b : n.bind) {
            bool known = false;
            for (int i = 0; i < count; ++i) known = known || b.first == defs[i].name;
            if (!known) Err(n, b.first, "unknown binding slot \"" + b.first + "\" for " + n.type);
            CheckBindValue(n, b.first, b.second);
        }
        for (int i = 0; i < count; ++i)
            if (defs[i].required && !n.Bind(defs[i].name))
                Err(n, defs[i].name, "missing required binding \"" + std::string(defs[i].name) + "\"");
    }

    void CheckAction(const Node& n, const std::string& ev, const Action& a) {
        const std::string ctx = "event \"" + ev + "\": ";
        if (!InPipeList("set|toggle|openPopup|closePopup|setTab|copyClipboard|handler", a.type)) {
            Err(n, ev, ctx + "unknown action type \"" + a.type + "\"");
            return;
        }
        if (a.type == "set" || a.type == "toggle") {
            if (a.target.empty()) Err(n, ev, ctx + a.type + " action requires \"target\"");
            else CheckPath(n, ev, a.target);
            if (a.type == "set" && a.value.IsNull()) Err(n, ev, ctx + "set action requires \"value\"");
        } else if (a.type == "openPopup") {
            const std::string* t = TypeOfId(a.target);
            if (!t || (*t != "Popup" && *t != "Modal"))
                Err(n, ev, ctx + "openPopup target \"" + a.target + "\" is not a Popup or Modal node");
        } else if (a.type == "setTab") {
            const std::string* t = TypeOfId(a.target);
            const std::string* tab = a.value.IsString() ? TypeOfId(a.value.AsString()) : nullptr;
            if (!t || *t != "TabBar") Err(n, ev, ctx + "setTab target \"" + a.target + "\" is not a TabBar node");
            if (!tab || *tab != "Tab") Err(n, ev, ctx + "setTab value must be the id of a Tab node");
        } else if (a.type == "copyClipboard" && a.value.IsNull()) {
            Err(n, ev, ctx + "copyClipboard action requires \"value\"");
        }
    }

    void CheckEvents(const Node& n, const char* const* evs, int evn) {
        for (const auto& e : n.events) {
            bool known = false;
            for (int i = 0; i < evn; ++i) known = known || e.first == evs[i];
            if (!known) Warn(n, e.first, "unknown event \"" + e.first + "\" for " + n.type);
            if (e.second.actions.empty() && !e.second.handler)
                Report(Diagnostic::Level::Info, n.id, e.first, "event \"" + e.first + "\" has no actions or handler");
            for (const Action& a : e.second.actions) CheckAction(n, e.first, a);
        }
    }

    void CheckColName(const Node& n, const std::string& prop, const std::string& name, const char* what) {
        int mv = ImGuiColMinVersion(name.c_str());
        if (mv == 0) Warn(n, prop, std::string("unknown ImGuiCol ") + what + " \"" + name + "\"");
        else if (mv > version) Warn(n, prop, "ImGuiCol \"" + name + "\" requires ImGui >= " + Project::VersionString(mv));
    }
    bool CheckVarName(const Node& n, const std::string& prop, const std::string& name, const char* what) {
        int mv = ImGuiStyleVarMinVersion(name.c_str());
        if (mv == 0) Warn(n, prop, std::string("unknown ImGuiStyleVar ") + what + " \"" + name + "\"");
        else if (mv > version) Warn(n, prop, "ImGuiStyleVar \"" + name + "\" requires ImGui >= " + Project::VersionString(mv));
        return mv != 0;
    }

    void CheckStyle(const Node& n, const StyleOverride& s) {
        for (const auto& c : s.colors) CheckColName(n, "style.colors", c.first, "name");
        for (const auto& v : s.vars) {
            if (!CheckVarName(n, "style.vars", v.first, "name")) continue;
            bool vec2 = StyleVarIsVec2(v.first.c_str());
            if (vec2 != v.second.IsObject())
                Err(n, "style.vars", std::string("style var \"") + v.first + "\" expects " + (vec2 ? "{\"x\", \"y\"}" : "a number"));
        }
        for (const std::string& cls : s.classes) {
            bool found = false;
            for (const auto& sc : project.style_classes) found = found || sc.first == cls;
            if (!found) Err(n, "style.classes", "unknown style class \"" + cls + "\"");
            else if (std::string other = ClassIdentifierClash(cls); !other.empty())
                Err(n, "style.classes", "style classes \"" + cls + "\" and \"" + other + "\" give the same C++ name");
        }
    }

    // exported code names each class Style_<name>_Push, so names that differ only in punctuation collide
    static std::string ClassIdentifier(const std::string& name) {
        std::string id;
        for (char c : name) id.push_back(((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') ? c : '_');
        return id;
    }
    std::string ClassIdentifierClash(const std::string& cls) const {
        std::string id = ClassIdentifier(cls);
        for (const auto& sc : project.style_classes)
            if (sc.first != cls && ClassIdentifier(sc.first) == id) return sc.first;
        return std::string();
    }

    void CheckAnim(const Node& n, const AnimSpec& a) {
        static const char* const kEases = "Linear|InQuad|OutQuad|InOutQuad|InCubic|OutCubic|InOutCubic|OutBack|InBack|"
                                          "OutElastic|OutBounce|OutExpo|Spring";
        static const char* const kPresets =
            "None|Fade|SlideUp|SlideDown|SlideLeft|SlideRight|FadeSlideUp|FadeSlideDown|ScalePop|Stagger";
        for (const Transition& t : a.transitions) {
            if (t.prop.compare(0, 6, "color.") == 0) {
                CheckColName(n, "anim", t.prop.substr(6), "in transition");
                if (!IsValidColorJson(t.to)) Err(n, "anim", "transition \"" + t.prop + "\" needs a color string in \"to\"");
            } else if (t.prop.compare(0, 4, "var.") == 0) {
                std::string var = t.prop.substr(4);
                if (CheckVarName(n, "anim", var, "in transition") && StyleVarIsVec2(var.c_str()) != t.to.IsObject())
                    Err(n, "anim", "transition \"" + t.prop + "\" has a \"to\" value of the wrong kind");
            }
            if (t.duration < 0) Err(n, "anim", "transition \"" + t.prop + "\" has a negative duration");
            if (!InPipeList(kEases, t.ease)) Warn(n, "anim", "unknown ease \"" + t.ease + "\"");
        }
        if (!a.enter_preset.empty() && !InPipeList(kPresets, a.enter_preset))
            Warn(n, "anim", "unknown enter preset \"" + a.enter_preset + "\"");
        if (!a.exit_preset.empty() && !InPipeList(kPresets, a.exit_preset))
            Warn(n, "anim", "unknown exit preset \"" + a.exit_preset + "\"");
    }

    void CheckMotions(const Node& n, const WidgetDef* wd) {
        MotionPresetFn known = MotionPresetResolver();
        for (const MotionSpec& m : n.anim.motions) {
            if (m.preset.empty() == m.custom.empty()) {
                Err(n, "anim", m.preset.empty() ? "motion needs a \"preset\" or \"custom\" animation"
                                                : "motion \"" + m.preset + "\" sets both \"preset\" and \"custom\"");
                continue;
            }
            const std::string what = "motion \"" + m.Name() + "\"";
            if (!m.preset.empty() && known && !known(m.preset.c_str()))
                Warn(n, "anim", "unknown motion preset \"" + m.preset + "\"");
            if (!m.custom.empty() && !project.FindAnimation(m.custom))
                Err(n, "anim", "unknown custom animation \"" + m.custom + "\"");
            if (!IsMotionTriggerName(m.trigger)) Err(n, "anim", what + ": unknown trigger \"" + m.trigger + "\"");
            if (!(m.duration_scale > 0)) Err(n, "anim", what + ": durationScale must be > 0");
            if (m.duration_scale > 1000) Err(n, "anim", what + ": durationScale is over 1000");
            if (m.delay < 0) Err(n, "anim", what + ": negative delay");
            if (m.delay > 3600) Err(n, "anim", what + ": delay is over 3600 seconds");
            if (m.stagger < 0) Err(n, "anim", what + ": negative stagger");
            if (m.stagger > 60) Err(n, "anim", what + ": stagger is over 60 seconds");
            if (m.iterations < -1) Err(n, "anim", what + ": iterations must be -1 (default), 0 (infinite) or more");
            if (m.iterations > 1000) Err(n, "anim", what + ": iterations is over 1000");
            if (!m.on.empty() && wd) {
                bool found = false;
                for (int i = 0; i < wd->event_count; ++i) found = found || m.on == wd->events[i];
                if (!found) Warn(n, "anim", what + ": unknown event \"" + m.on + "\" for " + n.type);
            }
        }
    }

    void CheckFx(const Node& n, const std::vector<FxSpec>& fx) {
        for (const FxSpec& f : fx) {
            if (!f.params.IsObject()) continue;
            for (const auto& kv : f.params.obj)
                if (kv.second.IsString() && !kv.second.AsString().empty() && kv.second.AsString()[0] == '#' &&
                    !IsValidColorJson(kv.second))
                    Err(n, "fx", "fx \"" + f.type + "\": invalid color in \"" + kv.first + "\"");
        }
    }

    void CheckComponentInstance(const Node& n) {
        if (n.component.empty()) {
            Err(n, "component", "Component node is missing its \"component\" name");
            return;
        }
        const Component* c = project.FindComponent(n.component);
        if (def && n.component == def->name) {
            Err(n, "component", "component \"" + n.component + "\" cannot instantiate itself");
            return;
        }
        if (!c) {
            Err(n, "component", "unknown component \"" + n.component + "\"");
            return;
        }
        for (const auto& p : n.props) {
            const Param* pd = nullptr;
            for (const Param& cp : c->params)
                if (cp.name == p.first) pd = &cp;
            if (!pd) {
                Warn(n, p.first, "unknown param \"" + p.first + "\" for component " + c->name);
                continue;
            }
            if (p.second.IsParam()) CheckParamRef(n, p.first, p.second);
            else if (pd->type.compare(0, 8, "binding<") == 0) CheckBindValue(n, p.first, p.second);
            else if (pd->type == "color" && !p.second.IsExpr() && !IsValidColorJson(p.second.raw))
                Err(n, p.first, "invalid color for component param \"" + p.first + "\"");
        }
        for (const Param& cp : c->params)
            if (cp.def.IsNull() && !n.Prop(cp.name.c_str()) && !n.Bind(cp.name.c_str()))
                Err(n, cp.name, "missing value for component param \"" + cp.name + "\"");
        for (const auto& s : n.slots)
            if (std::find(c->slots.begin(), c->slots.end(), s.first) == c->slots.end())
                Err(n, "slots", "component " + c->name + " has no slot \"" + s.first + "\"");
        if (!n.children.empty()) Err(n, "children", "Component instances take content through \"slots\", not \"children\"");
    }

    void CheckSlot(const Node& n) {
        if (!def) {
            Err(n, "", "Slot is only allowed inside a component definition");
            return;
        }
        const PropValue* name = n.Prop("name");
        if (!name || !name->raw.IsString()) Err(n, "name", "Slot requires a string \"name\" prop");
        else if (std::find(def->slots.begin(), def->slots.end(), name->raw.AsString()) == def->slots.end())
            Err(n, "name", "slot \"" + name->raw.AsString() + "\" is not declared by component " + def->name);
    }

    void CheckStructure(const Node& n, const Node* parent, bool container, const char* child_types) {
        if (n.type == "Window" && parent) Err(n, "", "Window is only allowed as the panel root");
        if (n.type == "Tab" && (!parent || parent->type != "TabBar")) Err(n, "", "Tab must be a direct child of a TabBar");
        if (n.type == "MenuBar") {
            const PropValue* mb = parent ? parent->Prop("menuBar") : nullptr;
            if (!parent || parent->type != "Window" || !mb || !mb->raw.AsBool())
                Err(n, "", "MenuBar must be a direct child of a Window with menuBar = true");
        }
        if (!container && !n.children.empty()) Err(n, "children", n.type + " cannot have children");
        if (!n.slots.empty() && n.type != "Component") Err(n, "slots", "only Component instances have slots");
        if (child_types)
            for (const Node& c : n.children)
                if (!InPipeList(child_types, c.type))
                    Err(c, "", n.type + " accepts only " + child_types + " children, got " + c.type);
    }

    void Visit(const Node& n, const Node* parent) {
        const WidgetDef* wd = project.widget_catalog ? project.widget_catalog->Find(n.type.c_str()) : FindWidget(n.type.c_str());
        if (!wd) {
            Err(n, "type", "unknown widget type \"" + n.type + "\"");
        } else {
            if (wd->min_version > version)
                Warn(n, "type", n.type + " requires ImGui >= " + Project::VersionString(wd->min_version));
            CheckStructure(n, parent, wd->container, wd->child_types);
            if (n.type == "Component") CheckComponentInstance(n);
            else CheckProps(n, wd->props, wd->prop_count);
            if (n.type == "Slot") CheckSlot(n);
            CheckBinds(n, wd->binds, wd->bind_count);
            CheckEvents(n, wd->events, wd->event_count);
        }
        CheckStyle(n, n.style);
        CheckAnim(n, n.anim);
        CheckMotions(n, wd);
        CheckFx(n, n.fx);
        if (!n.layout.IsDefault()) {
            if (!InPipeList("TopLeft|Top|TopRight|Left|Center|Right|BottomLeft|Bottom|BottomRight", n.layout.anchor))
                Err(n, "layout", "unknown layout anchor \"" + n.layout.anchor + "\"");
            if (!parent || parent->type != "Canvas") Warn(n, "layout", "layout is only used for direct children of a Canvas");
        }
        if (!n.font.empty()) {
            bool found = false;
            for (const FontSpec& f : project.fonts) found = found || f.name == n.font;
            if (!found) Warn(n, "font", "unknown font \"" + n.font + "\"");
        }
        for (const Node& c : n.children) Visit(c, &n);
        for (const auto& s : n.slots)
            for (const Node& c : s.second) Visit(c, &n);
    }

    bool Run(const Node& root) {
        CheckDeclarations();
        CheckIds(root);
        Visit(root, nullptr);
        return ok;
    }
};

}  // namespace core_detail

inline bool ValidatePanel(const Project& project, const Panel& panel, std::vector<Diagnostic>& out) {
    core_detail::Validator v(project, nullptr, panel.params, &panel.state, panel.name, out);
    return v.Run(panel.root);
}

inline bool ValidateComponent(const Project& project, const Component& component, std::vector<Diagnostic>& out) {
    core_detail::Validator v(project, &component, component.params, nullptr, component.name, out);
    return v.Run(component.root);
}

}  // namespace imforge
