#pragma once

#include <algorithm>

#include "imforge_core.h"

namespace imforge {

namespace core_detail {

inline bool Fail(Error* err, const std::string& msg) {
    if (err) {
        err->message = msg;
        err->line = 0;
        err->col = 0;
    }
    return false;
}

inline bool ReadString(const Json& j, const char* key, std::string& out, Error* err, const std::string& ctx) {
    const Json* v = j.Find(key);
    if (!v || v->IsNull()) return true;
    if (!v->IsString()) return Fail(err, ctx + ": \"" + key + "\" must be a string");
    out = v->AsString();
    return true;
}
inline bool ReadBool(const Json& j, const char* key, bool& out, Error* err, const std::string& ctx) {
    const Json* v = j.Find(key);
    if (!v || v->IsNull()) return true;
    if (!v->IsBool()) return Fail(err, ctx + ": \"" + key + "\" must be a boolean");
    out = v->AsBool();
    return true;
}
inline bool ReadFloat(const Json& j, const char* key, float& out, Error* err, const std::string& ctx) {
    const Json* v = j.Find(key);
    if (!v || v->IsNull()) return true;
    if (!v->IsNumber()) return Fail(err, ctx + ": \"" + key + "\" must be a number");
    out = (float)v->AsDouble();
    return true;
}
inline bool ReadInt(const Json& j, const char* key, int& out, Error* err, const std::string& ctx) {
    const Json* v = j.Find(key);
    if (!v || v->IsNull()) return true;
    if (!v->IsNumber()) return Fail(err, ctx + ": \"" + key + "\" must be a number");
    out = v->AsInt();
    return true;
}
inline bool ReadStringList(const Json& j, const char* key, std::vector<std::string>& out, Error* err,
                           const std::string& ctx) {
    const Json* v = j.Find(key);
    if (!v || v->IsNull()) return true;
    if (!v->IsArray()) return Fail(err, ctx + ": \"" + key + "\" must be an array of strings");
    out.clear();
    for (const Json& e : v->arr) {
        if (!e.IsString()) return Fail(err, ctx + ": \"" + key + "\" must be an array of strings");
        out.push_back(e.AsString());
    }
    return true;
}
inline Json StringListToJson(const std::vector<std::string>& v) {
    Json a = Json::Array();
    for (const std::string& s : v) a.Push(Json(s));
    return a;
}

inline Json Vec2ToJson(const Vec2& v) {
    Json o = Json::Object();
    o.Set("x", Json(FloatToNum(v.x)));
    o.Set("y", Json(FloatToNum(v.y)));
    return o;
}
inline bool Vec2FromJson(const Json& j, Vec2& out) {
    if (!j.IsObject() || !j["x"].IsNumber() || !j["y"].IsNumber()) return false;
    out.x = (float)j["x"].AsDouble();
    out.y = (float)j["y"].AsDouble();
    return true;
}
inline bool IsStyleVarValue(const Json& j) {
    return j.IsNumber() || (j.IsObject() && j["x"].IsNumber() && j["y"].IsNumber());
}

inline bool ColorMapFromJson(const Json& j, std::vector<std::pair<std::string, Vec4>>& out, Error* err,
                             const std::string& ctx) {
    if (j.IsNull()) return true;
    if (!j.IsObject()) return Fail(err, ctx + ": \"colors\" must be an object");
    out.clear();
    for (const auto& kv : j.obj) {
        bool ok = false;
        Vec4 c = kv.second.IsString() ? ParseColor(kv.second.AsString(), &ok) : Vec4();
        if (!ok) return Fail(err, ctx + ": color \"" + kv.first + "\" must be \"#rrggbb\" or \"#rrggbbaa\"");
        out.emplace_back(kv.first, c);
    }
    return true;
}
inline Json ColorMapToJson(const std::vector<std::pair<std::string, Vec4>>& colors) {
    Json o = Json::Object();
    for (const auto& kv : colors) o.Set(kv.first, Json(ColorToHex(kv.second)));
    return o;
}
inline bool VarMapFromJson(const Json& j, std::vector<std::pair<std::string, Json>>& out, Error* err,
                           const std::string& ctx) {
    if (j.IsNull()) return true;
    if (!j.IsObject()) return Fail(err, ctx + ": \"vars\" must be an object");
    out.clear();
    for (const auto& kv : j.obj) {
        if (!IsStyleVarValue(kv.second))
            return Fail(err, ctx + ": var \"" + kv.first + "\" must be a number or {\"x\",\"y\"}");
        out.emplace_back(kv.first, kv.second);
    }
    return true;
}
inline Json VarMapToJson(const std::vector<std::pair<std::string, Json>>& vars) {
    Json o = Json::Object();
    for (const auto& kv : vars) o.Set(kv.first, kv.second);
    return o;
}

inline bool StyleFromJson(const Json& j, StyleOverride& out, Error* err, const std::string& ctx) {
    out = StyleOverride();
    if (j.IsNull()) return true;
    if (!j.IsObject()) return Fail(err, ctx + ": \"style\" must be an object");
    return ColorMapFromJson(j["colors"], out.colors, err, ctx) && VarMapFromJson(j["vars"], out.vars, err, ctx) &&
           ReadStringList(j, "classes", out.classes, err, ctx);
}
inline Json StyleToJson(const StyleOverride& s) {
    Json o = Json::Object();
    if (!s.colors.empty()) o.Set("colors", ColorMapToJson(s.colors));
    if (!s.vars.empty()) o.Set("vars", VarMapToJson(s.vars));
    if (!s.classes.empty()) o.Set("classes", StringListToJson(s.classes));
    return o;
}

inline bool PropMapFromJson(const Json& j, const char* key, std::vector<std::pair<std::string, PropValue>>& out,
                            Error* err, const std::string& ctx) {
    const Json* v = j.Find(key);
    if (!v || v->IsNull()) return true;
    if (!v->IsObject()) return Fail(err, ctx + ": \"" + key + "\" must be an object");
    out.clear();
    for (const auto& kv : v->obj) out.emplace_back(kv.first, PropValue(kv.second));
    return true;
}
inline Json PropMapToJson(const std::vector<std::pair<std::string, PropValue>>& props) {
    std::vector<std::pair<std::string, const PropValue*>> sorted;
    for (const auto& p : props) sorted.emplace_back(p.first, &p.second);
    std::sort(sorted.begin(), sorted.end(),
              [](const std::pair<std::string, const PropValue*>& a, const std::pair<std::string, const PropValue*>& b) {
                  return a.first < b.first;
              });
    Json o = Json::Object();
    for (const auto& p : sorted) o.Set(p.first, p.second->raw);
    return o;
}

inline bool EventsFromJson(const Json& j, std::vector<std::pair<std::string, EventSpec>>& out, Error* err,
                           const std::string& ctx) {
    out.clear();
    if (j.IsNull()) return true;
    if (!j.IsObject()) return Fail(err, ctx + ": \"events\" must be an object");
    for (const auto& kv : j.obj) {
        const std::string ectx = ctx + ": event \"" + kv.first + "\"";
        if (!kv.second.IsObject()) return Fail(err, ectx + " must be an object");
        EventSpec ev;
        if (!ReadBool(kv.second, "handler", ev.handler, err, ectx)) return false;
        const Json& acts = kv.second["actions"];
        if (!acts.IsNull() && !acts.IsArray()) return Fail(err, ectx + ": \"actions\" must be an array");
        for (const Json& a : acts.arr) {
            if (!a.IsObject()) return Fail(err, ectx + ": action must be an object");
            Action act;
            if (!ReadString(a, "type", act.type, err, ectx) || !ReadString(a, "target", act.target, err, ectx))
                return false;
            if (act.type.empty()) return Fail(err, ectx + ": action missing \"type\"");
            act.value = a["value"];
            ev.actions.push_back(std::move(act));
        }
        out.emplace_back(kv.first, std::move(ev));
    }
    return true;
}
inline Json EventsToJson(const std::vector<std::pair<std::string, EventSpec>>& events) {
    Json o = Json::Object();
    for (const auto& kv : events) {
        Json ev = Json::Object();
        if (!kv.second.actions.empty()) {
            Json acts = Json::Array();
            for (const Action& a : kv.second.actions) {
                Json act = Json::Object();
                act.Set("type", Json(a.type));
                if (!a.target.empty()) act.Set("target", Json(a.target));
                if (!a.value.IsNull()) act.Set("value", a.value);
                acts.Push(std::move(act));
            }
            ev.Set("actions", std::move(acts));
        }
        if (kv.second.handler) ev.Set("handler", Json(true));
        o.Set(kv.first, std::move(ev));
    }
    return o;
}

inline bool AnimFromJson(const Json& j, AnimSpec& out, Error* err, const std::string& ctx) {
    out = AnimSpec();
    if (j.IsNull()) return true;
    if (!j.IsObject()) return Fail(err, ctx + ": \"anim\" must be an object");
    const Json& trs = j["transitions"];
    if (!trs.IsNull() && !trs.IsArray()) return Fail(err, ctx + ": \"transitions\" must be an array");
    for (const Json& t : trs.arr) {
        if (!t.IsObject()) return Fail(err, ctx + ": transition must be an object");
        Transition tr;
        if (!ReadString(t, "prop", tr.prop, err, ctx) || !ReadString(t, "on", tr.on, err, ctx) ||
            !ReadFloat(t, "duration", tr.duration, err, ctx) || !ReadString(t, "ease", tr.ease, err, ctx) ||
            !ReadFloat(t, "stiffness", tr.stiffness, err, ctx) || !ReadFloat(t, "damping", tr.damping, err, ctx))
            return false;
        if (tr.prop.empty()) return Fail(err, ctx + ": transition missing \"prop\"");
        tr.to = t["to"];
        out.transitions.push_back(std::move(tr));
    }
    const Json& enter = j["enter"];
    if (!enter.IsNull()) {
        if (!enter.IsObject()) return Fail(err, ctx + ": \"enter\" must be an object");
        if (!ReadString(enter, "preset", out.enter_preset, err, ctx) ||
            !ReadFloat(enter, "duration", out.enter_duration, err, ctx) ||
            !ReadFloat(enter, "stagger", out.stagger, err, ctx))
            return false;
    }
    const Json& exit = j["exit"];
    if (!exit.IsNull()) {
        if (!exit.IsObject()) return Fail(err, ctx + ": \"exit\" must be an object");
        if (!ReadString(exit, "preset", out.exit_preset, err, ctx) ||
            !ReadFloat(exit, "duration", out.exit_duration, err, ctx))
            return false;
    }
    return true;
}
inline Json AnimToJson(const AnimSpec& a) {
    Json o = Json::Object();
    if (!a.transitions.empty()) {
        Json trs = Json::Array();
        for (const Transition& t : a.transitions) {
            Json tr = Json::Object();
            tr.Set("prop", Json(t.prop));
            if (!t.on.empty()) tr.Set("on", Json(t.on));
            if (!t.to.IsNull()) tr.Set("to", t.to);
            tr.Set("duration", Json(FloatToNum(t.duration)));
            tr.Set("ease", Json(t.ease));
            if (t.stiffness != 0) tr.Set("stiffness", Json(FloatToNum(t.stiffness)));
            if (t.damping != 0) tr.Set("damping", Json(FloatToNum(t.damping)));
            trs.Push(std::move(tr));
        }
        o.Set("transitions", std::move(trs));
    }
    if (!a.enter_preset.empty()) {
        Json e = Json::Object();
        e.Set("preset", Json(a.enter_preset));
        e.Set("duration", Json(FloatToNum(a.enter_duration)));
        if (a.stagger != 0) e.Set("stagger", Json(FloatToNum(a.stagger)));
        o.Set("enter", std::move(e));
    }
    if (!a.exit_preset.empty()) {
        Json e = Json::Object();
        e.Set("preset", Json(a.exit_preset));
        e.Set("duration", Json(FloatToNum(a.exit_duration)));
        o.Set("exit", std::move(e));
    }
    return o;
}

inline bool FxFromJson(const Json& j, std::vector<FxSpec>& out, Error* err, const std::string& ctx) {
    out.clear();
    if (j.IsNull()) return true;
    if (!j.IsArray()) return Fail(err, ctx + ": \"fx\" must be an array");
    for (const Json& f : j.arr) {
        if (!f.IsObject()) return Fail(err, ctx + ": fx entry must be an object");
        FxSpec fx;
        fx.params = Json::Object();
        for (const auto& kv : f.obj) {
            if (kv.first == "type") {
                if (!kv.second.IsString()) return Fail(err, ctx + ": fx \"type\" must be a string");
                fx.type = kv.second.AsString();
            } else {
                fx.params.Set(kv.first, kv.second);
            }
        }
        if (fx.type.empty()) return Fail(err, ctx + ": fx entry missing \"type\"");
        out.push_back(std::move(fx));
    }
    return true;
}
inline Json FxToJson(const std::vector<FxSpec>& fx) {
    Json a = Json::Array();
    for (const FxSpec& f : fx) {
        Json o = Json::Object();
        o.Set("type", Json(f.type));
        if (f.params.IsObject())
            for (const auto& kv : f.params.obj) o.Set(kv.first, kv.second);
        a.Push(std::move(o));
    }
    return a;
}

inline bool LayoutFromJson(const Json& j, Layout& out, Error* err, const std::string& ctx) {
    out = Layout();
    if (j.IsNull()) return true;
    if (!j.IsObject()) return Fail(err, ctx + ": \"layout\" must be an object");
    if (!ReadString(j, "anchor", out.anchor, err, ctx)) return false;
    if (out.anchor.empty()) out.anchor = "TopLeft";
    const Json& off = j["offset"];
    if (!off.IsNull() && !Vec2FromJson(off, out.offset))
        return Fail(err, ctx + ": \"layout.offset\" must be {\"x\",\"y\"}");
    return true;
}
inline Json LayoutToJson(const Layout& l) {
    Json o = Json::Object();
    o.Set("anchor", Json(l.anchor));
    if (l.offset.x != 0 || l.offset.y != 0) o.Set("offset", Vec2ToJson(l.offset));
    return o;
}

template <class T>
inline bool ParamsFromJson(const Json& j, const char* key, std::vector<T>& out, Error* err, const std::string& ctx) {
    out.clear();
    const Json* v = j.Find(key);
    if (!v || v->IsNull()) return true;
    if (!v->IsArray()) return Fail(err, ctx + ": \"" + key + "\" must be an array");
    for (const Json& p : v->arr) {
        const std::string pctx = ctx + ": " + key + " entry";
        if (!p.IsObject()) return Fail(err, pctx + " must be an object");
        T item;
        if (!ReadString(p, "name", item.name, err, pctx) || !ReadString(p, "type", item.type, err, pctx)) return false;
        if (item.name.empty()) return Fail(err, pctx + " missing \"name\"");
        if (item.type.empty()) return Fail(err, pctx + " \"" + item.name + "\" missing \"type\"");
        item.def = p["default"];
        out.push_back(std::move(item));
    }
    return true;
}
template <class T>
inline Json ParamsToJson(const std::vector<T>& params) {
    Json a = Json::Array();
    for (const T& p : params) {
        Json o = Json::Object();
        o.Set("name", Json(p.name));
        o.Set("type", Json(p.type));
        if (!p.def.IsNull()) o.Set("default", p.def);
        a.Push(std::move(o));
    }
    return a;
}

inline bool CheckHeader(const Json& j, const char* kind, Error* err) {
    if (!j.IsObject()) return Fail(err, std::string(kind) + ": expected a JSON object");
    const Json* sv = j.Find("schemaVersion");
    if (sv && !(sv->IsNumber() && sv->AsInt() == 1))
        return Fail(err, std::string(kind) + ": unsupported schemaVersion (expected 1)");
    const Json* k = j.Find("kind");
    if (k && !(k->IsString() && k->AsString() == kind))
        return Fail(err, std::string(kind) + ": \"kind\" must be \"" + kind + "\"");
    return true;
}
inline Json NewFile(const char* kind, const std::string& name) {
    Json o = Json::Object();
    o.Set("schemaVersion", Json(1));
    o.Set("kind", Json(kind));
    o.Set("name", Json(name));
    return o;
}

}  // namespace core_detail

inline bool NodeFromJson(const Json& j, Node& out, Error* err) {
    using namespace core_detail;
    out = Node();
    if (!j.IsObject()) return Fail(err, "node: expected a JSON object");
    out.id = j["id"].AsString("");
    out.type = j["type"].AsString("");
    if (out.type.empty()) return Fail(err, "node \"" + out.id + "\": missing \"type\"");
    if (out.id.empty()) return Fail(err, "node of type " + out.type + ": missing \"id\"");
    const std::string ctx = "node \"" + out.id + "\"";
    if (!ReadString(j, "name", out.name, err, ctx) || !ReadString(j, "component", out.component, err, ctx) ||
        !ReadString(j, "visibleIf", out.visible_if, err, ctx) || !ReadString(j, "enabledIf", out.enabled_if, err, ctx) ||
        !ReadString(j, "tooltip", out.tooltip, err, ctx) || !ReadString(j, "font", out.font, err, ctx) ||
        !ReadBool(j, "locked", out.locked, err, ctx) || !PropMapFromJson(j, "props", out.props, err, ctx) ||
        !PropMapFromJson(j, "bind", out.bind, err, ctx) || !StyleFromJson(j["style"], out.style, err, ctx) ||
        !EventsFromJson(j["events"], out.events, err, ctx) || !AnimFromJson(j["anim"], out.anim, err, ctx) ||
        !FxFromJson(j["fx"], out.fx, err, ctx) || !LayoutFromJson(j["layout"], out.layout, err, ctx))
        return false;
    for (const auto& b : out.bind) {
        if (!b.second.raw.IsString() && !b.second.IsParam())
            return Fail(err, ctx + ": bind \"" + b.first + "\" must be a path string or {\"param\":...}");
    }
    const Json& children = j["children"];
    if (!children.IsNull() && !children.IsArray()) return Fail(err, ctx + ": \"children\" must be an array");
    for (const Json& c : children.arr) {
        Node child;
        if (!NodeFromJson(c, child, err)) return false;
        out.children.push_back(std::move(child));
    }
    const Json& slots = j["slots"];
    if (!slots.IsNull() && !slots.IsObject()) return Fail(err, ctx + ": \"slots\" must be an object");
    for (const auto& kv : slots.obj) {
        if (!kv.second.IsArray()) return Fail(err, ctx + ": slot \"" + kv.first + "\" must be an array");
        std::vector<Node> nodes;
        for (const Json& c : kv.second.arr) {
            Node child;
            if (!NodeFromJson(c, child, err)) return false;
            nodes.push_back(std::move(child));
        }
        out.slots.emplace_back(kv.first, std::move(nodes));
    }
    return true;
}

inline Json NodeToJson(const Node& n) {
    using namespace core_detail;
    Json o = Json::Object();
    o.Set("id", Json(n.id));
    o.Set("type", Json(n.type));
    if (!n.name.empty()) o.Set("name", Json(n.name));
    if (!n.component.empty()) o.Set("component", Json(n.component));
    if (!n.props.empty()) o.Set("props", PropMapToJson(n.props));
    if (!n.bind.empty()) o.Set("bind", PropMapToJson(n.bind));
    if (!n.visible_if.empty()) o.Set("visibleIf", Json(n.visible_if));
    if (!n.enabled_if.empty()) o.Set("enabledIf", Json(n.enabled_if));
    if (!n.tooltip.empty()) o.Set("tooltip", Json(n.tooltip));
    if (!n.font.empty()) o.Set("font", Json(n.font));
    if (!n.style.Empty()) o.Set("style", StyleToJson(n.style));
    if (!n.events.empty()) o.Set("events", EventsToJson(n.events));
    if (!n.anim.Empty()) o.Set("anim", AnimToJson(n.anim));
    if (!n.fx.empty()) o.Set("fx", FxToJson(n.fx));
    if (!n.layout.IsDefault()) o.Set("layout", LayoutToJson(n.layout));
    if (n.locked) o.Set("locked", Json(true));
    if (!n.children.empty()) {
        Json a = Json::Array();
        for (const Node& c : n.children) a.Push(NodeToJson(c));
        o.Set("children", std::move(a));
    }
    if (!n.slots.empty()) {
        Json s = Json::Object();
        for (const auto& kv : n.slots) {
            Json a = Json::Array();
            for (const Node& c : kv.second) a.Push(NodeToJson(c));
            s.Set(kv.first, std::move(a));
        }
        o.Set("slots", std::move(s));
    }
    return o;
}

inline bool PanelFromJson(const Json& j, Panel& out, Error* err) {
    using namespace core_detail;
    std::string file = out.file;
    out = Panel();
    out.file = file;
    if (!CheckHeader(j, "panel", err)) return false;
    if (!ReadString(j, "name", out.name, err, "panel")) return false;
    if (out.name.empty()) return Fail(err, "panel: missing \"name\"");
    const std::string ctx = "panel \"" + out.name + "\"";
    if (!ParamsFromJson(j, "params", out.params, err, ctx) || !ParamsFromJson(j, "state", out.state, err, ctx))
        return false;
    if (!j.Has("root")) return Fail(err, ctx + ": missing \"root\"");
    return NodeFromJson(j["root"], out.root, err);
}

inline Json PanelToJson(const Panel& p) {
    using namespace core_detail;
    Json o = NewFile("panel", p.name);
    if (!p.params.empty()) o.Set("params", ParamsToJson(p.params));
    if (!p.state.empty()) o.Set("state", ParamsToJson(p.state));
    o.Set("root", NodeToJson(p.root));
    return o;
}

inline bool ComponentFromJson(const Json& j, Component& out, Error* err) {
    using namespace core_detail;
    std::string file = out.file;
    out = Component();
    out.file = file;
    if (!CheckHeader(j, "component", err)) return false;
    if (!ReadString(j, "name", out.name, err, "component")) return false;
    if (out.name.empty()) return Fail(err, "component: missing \"name\"");
    const std::string ctx = "component \"" + out.name + "\"";
    if (!ParamsFromJson(j, "params", out.params, err, ctx) || !ReadStringList(j, "slots", out.slots, err, ctx))
        return false;
    if (!j.Has("root")) return Fail(err, ctx + ": missing \"root\"");
    return NodeFromJson(j["root"], out.root, err);
}

inline Json ComponentToJson(const Component& c) {
    using namespace core_detail;
    Json o = NewFile("component", c.name);
    if (!c.params.empty()) o.Set("params", ParamsToJson(c.params));
    if (!c.slots.empty()) o.Set("slots", StringListToJson(c.slots));
    o.Set("root", NodeToJson(c.root));
    return o;
}

inline bool ThemeFromJson(const Json& j, Theme& out, Error* err) {
    using namespace core_detail;
    std::string file = out.file;
    out = Theme();
    out.file = file;
    if (!CheckHeader(j, "theme", err)) return false;
    if (!ReadString(j, "name", out.name, err, "theme")) return false;
    if (out.name.empty()) return Fail(err, "theme: missing \"name\"");
    const std::string ctx = "theme \"" + out.name + "\"";
    return ColorMapFromJson(j["colors"], out.colors, err, ctx) && VarMapFromJson(j["vars"], out.vars, err, ctx);
}

inline Json ThemeToJson(const Theme& t) {
    using namespace core_detail;
    Json o = NewFile("theme", t.name);
    o.Set("colors", ColorMapToJson(t.colors));
    o.Set("vars", VarMapToJson(t.vars));
    return o;
}

inline bool ProjectFromJson(const Json& j, Project& out, Error* err) {
    using namespace core_detail;
    std::string dir = out.dir;
    const WidgetCatalog* catalog = out.widget_catalog;
    out = Project();
    out.dir = dir;
    out.widget_catalog = catalog;
    if (!CheckHeader(j, "project", err)) return false;
    if (!ReadString(j, "name", out.name, err, "project")) return false;
    if (out.name.empty()) return Fail(err, "project: missing \"name\"");
    const std::string ctx = "project \"" + out.name + "\"";
    const Json& imgui = j["imgui"];
    const Json& platform = j["platform"];
    const Json& features = j["features"];
    const Json& cg = j["codegen"];
    const Json& bind = j["binding"];
    const Json& live = j["live"];
    const Json& fonts = j["fonts"];
    const Json& classes = j["styleClasses"];
    if (!ReadString(imgui, "version", out.imgui_version, err, ctx) ||
        !ReadString(imgui, "branch", out.imgui_branch, err, ctx) || !ReadString(platform, "window", out.window, err, ctx) ||
        !ReadString(platform, "renderer", out.renderer, err, ctx))
        return false;
    if (!features.IsNull() && !features.IsObject()) return Fail(err, ctx + ": \"features\" must be an object");
    for (const auto& kv : features.obj) {
        if (!kv.second.IsBool()) return Fail(err, ctx + ": feature \"" + kv.first + "\" must be a boolean");
        out.features.emplace_back(kv.first, kv.second.AsBool());
    }
    CodegenSettings& c = out.codegen;
    if (!ReadInt(cg, "cppStandard", c.cpp_standard, err, ctx) || !ReadString(cg, "namespace", c.nmspace, err, ctx) ||
        !ReadBool(cg, "splitHeader", c.split_header, err, ctx) ||
        !ReadString(cg, "functionCase", c.function_case, err, ctx) ||
        !ReadString(cg, "variableCase", c.variable_case, err, ctx) || !ReadInt(cg, "indent", c.indent, err, ctx) ||
        !ReadBool(cg, "useTabs", c.use_tabs, err, ctx) || !ReadString(cg, "outputDir", c.output_dir, err, ctx) ||
        !ReadBool(cg, "markers", c.markers, err, ctx) || !ReadString(cg, "dpiMode", c.dpi_mode, err, ctx) ||
        !ReadFloat(cg, "baseFontSize", c.base_font_size, err, ctx) ||
        !ReadString(cg, "runtimeHeader", c.runtime_header, err, ctx) || !ReadBool(cg, "stdString", c.std_string, err, ctx))
        return false;
    if (!ReadString(bind, "compileCommands", out.compile_commands, err, ctx) ||
        !ReadStringList(bind, "includePaths", out.include_paths, err, ctx) ||
        !ReadStringList(bind, "types", out.binding_types, err, ctx) ||
        !ReadString(bind, "typesJson", out.types_json, err, ctx))
        return false;
    LiveSettings& l = out.live;
    if (!ReadBool(live, "enabled", l.enabled, err, ctx) || !ReadString(live, "transport", l.transport, err, ctx) ||
        !ReadInt(live, "tcpPort", l.tcp_port, err, ctx) || !ReadString(live, "pipeName", l.pipe_name, err, ctx) ||
        !ReadString(live, "shmName", l.shm_name, err, ctx))
        return false;
    if (!fonts.IsNull() && !fonts.IsArray()) return Fail(err, ctx + ": \"fonts\" must be an array");
    for (const Json& f : fonts.arr) {
        if (!f.IsObject()) return Fail(err, ctx + ": font entry must be an object");
        FontSpec fs;
        if (!ReadString(f, "name", fs.name, err, ctx) || !ReadString(f, "file", fs.file, err, ctx) ||
            !ReadFloat(f, "size", fs.size, err, ctx) || !ReadString(f, "ranges", fs.ranges, err, ctx) ||
            !ReadString(f, "mergeIcons", fs.merge_icons, err, ctx))
            return false;
        out.fonts.push_back(std::move(fs));
    }
    if (!ReadStringList(j, "themes", out.theme_files, err, ctx) || !ReadString(j, "activeTheme", out.active_theme, err, ctx) ||
        !ReadStringList(j, "panels", out.panel_files, err, ctx) ||
        !ReadStringList(j, "components", out.component_files, err, ctx) ||
        !ReadStringList(j, "libraries", out.libraries, err, ctx))
        return false;
    if (!classes.IsNull() && !classes.IsObject()) return Fail(err, ctx + ": \"styleClasses\" must be an object");
    for (const auto& kv : classes.obj) {
        StyleOverride so;
        if (!StyleFromJson(kv.second, so, err, ctx + ": styleClass \"" + kv.first + "\"")) return false;
        out.style_classes.emplace_back(kv.first, std::move(so));
    }
    return true;
}

inline Json ProjectToJson(const Project& p) {
    using namespace core_detail;
    Json o = NewFile("project", p.name);
    Json imgui = Json::Object();
    imgui.Set("version", Json(p.imgui_version));
    imgui.Set("branch", Json(p.imgui_branch));
    o.Set("imgui", std::move(imgui));
    Json platform = Json::Object();
    platform.Set("window", Json(p.window));
    platform.Set("renderer", Json(p.renderer));
    o.Set("platform", std::move(platform));
    Json features = Json::Object();
    for (const auto& kv : p.features) features.Set(kv.first, Json(kv.second));
    o.Set("features", std::move(features));
    const CodegenSettings& c = p.codegen;
    Json cg = Json::Object();
    cg.Set("cppStandard", Json(c.cpp_standard));
    cg.Set("namespace", Json(c.nmspace));
    cg.Set("splitHeader", Json(c.split_header));
    cg.Set("functionCase", Json(c.function_case));
    cg.Set("variableCase", Json(c.variable_case));
    cg.Set("indent", Json(c.indent));
    cg.Set("useTabs", Json(c.use_tabs));
    cg.Set("outputDir", Json(c.output_dir));
    cg.Set("markers", Json(c.markers));
    cg.Set("dpiMode", Json(c.dpi_mode));
    cg.Set("baseFontSize", Json(FloatToNum(c.base_font_size)));
    cg.Set("runtimeHeader", Json(c.runtime_header));
    cg.Set("stdString", Json(c.std_string));
    o.Set("codegen", std::move(cg));
    Json bind = Json::Object();
    bind.Set("compileCommands", Json(p.compile_commands));
    bind.Set("includePaths", StringListToJson(p.include_paths));
    bind.Set("types", StringListToJson(p.binding_types));
    bind.Set("typesJson", Json(p.types_json));
    o.Set("binding", std::move(bind));
    Json live = Json::Object();
    live.Set("enabled", Json(p.live.enabled));
    live.Set("transport", Json(p.live.transport));
    live.Set("tcpPort", Json(p.live.tcp_port));
    live.Set("pipeName", Json(p.live.pipe_name));
    live.Set("shmName", Json(p.live.shm_name));
    o.Set("live", std::move(live));
    Json fonts = Json::Array();
    for (const FontSpec& f : p.fonts) {
        Json fs = Json::Object();
        fs.Set("name", Json(f.name));
        fs.Set("file", Json(f.file));
        fs.Set("size", Json(FloatToNum(f.size)));
        if (!f.ranges.empty()) fs.Set("ranges", Json(f.ranges));
        if (!f.merge_icons.empty()) fs.Set("mergeIcons", Json(f.merge_icons));
        fonts.Push(std::move(fs));
    }
    o.Set("fonts", std::move(fonts));
    std::vector<std::string> themes, panels, components;
    for (const Theme& t : p.themes)
        if (!t.file.empty()) themes.push_back(t.file);
    for (const Panel& pn : p.panels)
        if (!pn.file.empty()) panels.push_back(pn.file);
    for (const Component& cp : p.components)
        if (!cp.file.empty()) components.push_back(cp.file);
    o.Set("themes", StringListToJson(themes.empty() ? p.theme_files : themes));
    if (!p.active_theme.empty()) o.Set("activeTheme", Json(p.active_theme));
    Json classes = Json::Object();
    for (const auto& kv : p.style_classes) classes.Set(kv.first, StyleToJson(kv.second));
    o.Set("styleClasses", std::move(classes));
    o.Set("panels", StringListToJson(panels.empty() ? p.panel_files : panels));
    o.Set("components", StringListToJson(components.empty() ? p.component_files : components));
    o.Set("libraries", StringListToJson(p.libraries));
    return o;
}

}  // namespace imforge
