#include "components_ops.h"

#include <algorithm>
#include <map>
#include <set>

#include "../ui/tree_ops.h"

namespace imforge::builder::components {

namespace {

std::string ToIdentifier(const std::string& s, const char* fallback) {
    std::string out;
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        out.push_back(ok ? c : '_');
    }
    if (out.empty()) out = fallback;
    if (out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
    return out;
}

template <class Taken>
std::string Uniquify(const std::string& base, Taken&& taken) {
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        std::string cand = base + std::to_string(i);
        if (!taken(cand)) return cand;
    }
}

std::string NameFromPath(const std::string& path) {
    std::string seg = path;
    size_t dot = seg.find_last_of('.');
    if (dot != std::string::npos) seg = seg.substr(dot + 1);
    size_t br = seg.find('[');
    if (br != std::string::npos) seg = seg.substr(0, br);
    return ToIdentifier(seg, "value");
}

std::string BindTargetFor(const WidgetDef* def, const std::string& slot) {
    if (!def) return "any";
    const BindDef* b = FindBind(*def, slot.c_str());
    if (!b) return "any";
    std::vector<std::string> types = SplitPipe(b->types);
    if (types.empty()) return "any";
    const std::string& first = types.front();
    if (first == "enum") return "int";
    for (const std::string& t : BindingTargetTypes())
        if (t == first) return t;
    return "any";
}

bool Fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

}  // namespace

const std::vector<std::string>& ParamBaseTypes() {
    static const std::vector<std::string> kTypes = {"string", "int", "float", "bool", "color", "vec2", "vec4", "enum", "binding"};
    return kTypes;
}

const std::vector<std::string>& BindingTargetTypes() {
    static const std::vector<std::string> kTypes = {"bool", "int", "float", "string", "vec2", "vec4", "color", "any"};
    return kTypes;
}

ParamType ParseParamType(const std::string& type) {
    ParamType t;
    if (type.compare(0, 8, "binding<") == 0 && type.back() == '>') {
        t.base = "binding";
        t.arg = type.substr(8, type.size() - 9);
        if (t.arg.empty()) t.arg = "any";
    } else if (type.compare(0, 5, "enum:") == 0) {
        t.base = "enum";
        t.arg = type.substr(5);
    } else {
        t.base = type.empty() ? "string" : type;
    }
    return t;
}

std::string MakeParamType(const ParamType& t) {
    if (t.base == "binding") return "binding<" + (t.arg.empty() ? std::string("any") : t.arg) + ">";
    if (t.base == "enum") return "enum:" + t.arg;
    return t.base;
}

Json DefaultForParamType(const std::string& type) {
    ParamType t = ParseParamType(type);
    if (t.base == "string") return Json("");
    if (t.base == "int") return Json(0);
    if (t.base == "float") return Json(0.0);
    if (t.base == "bool") return Json(false);
    if (t.base == "color") return Json("#ffffffff");
    if (t.base == "vec2" || t.base == "vec4") {
        Json o = Json::Object();
        o.Set("x", Json(0));
        o.Set("y", Json(0));
        if (t.base == "vec4") {
            o.Set("z", Json(0));
            o.Set("w", Json(0));
        }
        return o;
    }
    if (t.base == "enum") {
        size_t comma = t.arg.find(',');
        std::string first = comma == std::string::npos ? t.arg : t.arg.substr(0, comma);
        return first.empty() ? Json() : Json(first);
    }
    return Json();
}

bool IsIdentifier(const std::string& s) { return core_detail::IsIdentifier(s); }

bool IsValidComponentName(const std::string& s) {
    size_t dot = s.find('.');
    if (dot == std::string::npos) return IsIdentifier(s);
    return IsIdentifier(s.substr(0, dot)) && IsIdentifier(s.substr(dot + 1));
}

std::string UniqueComponentName(const Project& project, const std::string& base) {
    std::string b = ToIdentifier(base, "Component");
    return Uniquify(b, [&](const std::string& n) { return project.FindComponent(n) != nullptr; });
}

std::string UniqueParamName(const Component& c, const std::string& base) {
    std::string b = ToIdentifier(base, "param");
    return Uniquify(b, [&](const std::string& n) {
        for (const Param& p : c.params)
            if (p.name == n) return true;
        return false;
    });
}

std::string UniqueSlotName(const Component& c, const std::string& base) {
    std::string b = ToIdentifier(base, "content");
    return Uniquify(b, [&](const std::string& n) { return std::find(c.slots.begin(), c.slots.end(), n) != c.slots.end(); });
}

bool IsLibraryComponent(const Component& c) {
    return c.file.compare(0, 5, "libs/") == 0 || c.name.find('.') != std::string::npos;
}

Component MakeEmptyComponent(const std::string& name, bool with_slot) {
    Component c;
    c.name = name;
    c.file = "components/" + name + ".imc.json";
    c.root.id = "root";
    c.root.type = "VStack";
    if (with_slot) {
        c.slots.push_back("content");
        c.root.children.push_back(MakeSlotNode(c.root, "content"));
    }
    return c;
}

std::vector<InstanceRef> FindInstances(const Project& project, const std::string& component) {
    std::vector<InstanceRef> out;
    auto scan = [&](const Node& root, const std::string& doc, bool in_component) {
        Walk(root, [&](const Node& n) {
            if (n.type == "Component" && n.component == component) out.push_back(InstanceRef{doc, in_component, n.id});
        });
    };
    for (const Panel& p : project.panels) scan(p.root, p.name, false);
    for (const Component& c : project.components)
        if (c.name != component) scan(c.root, c.name, true);
    return out;
}

size_t CountInstances(const Project& project, const std::string& component) {
    return FindInstances(project, component).size();
}

size_t RenameInstanceRefs(Node& root, const std::string& old_name, const std::string& new_name) {
    size_t n = 0;
    Walk(root, [&](Node& x) {
        if (x.type == "Component" && x.component == old_name) {
            x.component = new_name;
            ++n;
        }
    });
    return n;
}

size_t RenameParamRefs(Node& root, const std::string& old_name, const std::string& new_name) {
    size_t n = 0;
    Json pv = Json::Object();
    pv.Set("param", Json(new_name));
    auto fix = [&](std::vector<std::pair<std::string, PropValue>>& list) {
        for (auto& kv : list) {
            if (kv.second.IsParam() && kv.second.Param() == old_name) {
                kv.second.raw = pv;
                ++n;
            }
        }
    };
    Walk(root, [&](Node& x) {
        fix(x.props);
        fix(x.bind);
    });
    return n;
}

size_t RenameSlotRefs(Node& root, const std::string& old_name, const std::string& new_name) {
    size_t n = 0;
    Walk(root, [&](Node& x) {
        if (x.type != "Slot") return;
        const PropValue* p = x.Prop("name");
        std::string cur = p && p->raw.IsString() ? p->raw.AsString() : "content";
        if (cur == old_name) {
            x.SetProp("name", Json(new_name));
            ++n;
        }
    });
    return n;
}

bool HasSlotNode(const Node& root, const std::string& slot) {
    bool found = false;
    Walk(root, [&](const Node& x) {
        if (x.type != "Slot") return;
        const PropValue* p = x.Prop("name");
        std::string cur = p && p->raw.IsString() ? p->raw.AsString() : "content";
        found = found || cur == slot;
    });
    return found;
}

Node MakeSlotNode(const Node& root, const std::string& slot) {
    Node n;
    n.type = "Slot";
    std::string want = "slot_" + ToIdentifier(slot, "content");
    n.id = FindNode(root, want) ? GenerateId(root, "Slot") : want;
    n.SetProp("name", Json(slot));
    return n;
}

bool PlanExtract(const Node& root, const std::vector<std::string>& selection, const WidgetCatalog* catalog,
                 const Project* project, ExtractPlan& out, std::string* err) {
    std::vector<std::string> top = tree::TopLevelIds(root, selection);
    top.erase(std::remove(top.begin(), top.end(), root.id), top.end());
    if (top.empty()) return Fail(err, "select one or more nodes below the root");
    std::vector<std::string> ordered;
    Walk(root, [&](const Node& n) {
        if (std::find(top.begin(), top.end(), n.id) != top.end()) ordered.push_back(n.id);
    });
    tree::NodeLocation loc;
    if (!tree::LocateNode(const_cast<Node&>(root), ordered.front(), loc)) return Fail(err, "cannot locate " + ordered.front());
    if (!tree::AcceptsChild(catalog, *loc.parent, "Component"))
        return Fail(err, loc.parent->type + " cannot contain a component instance");
    out.node_ids = ordered;
    out.candidates.clear();
    std::map<std::string, std::string> path_params;
    std::set<std::string> used;
    auto unique = [&](const std::string& base) {
        std::string n = Uniquify(base, [&](const std::string& c) { return used.count(c) != 0; });
        used.insert(n);
        return n;
    };
    auto add_binding = [&](const Node& x, const std::string& key, const std::string& path, const std::string& type) {
        ExtractCandidate c;
        c.kind = ExtractCandidate::Kind::Binding;
        c.node_id = x.id;
        c.key = key;
        c.value = path;
        auto it = path_params.find(path);
        if (it != path_params.end()) {
            c.param_name = it->second;
            for (const ExtractCandidate& prev : out.candidates)
                if (prev.param_name == c.param_name) c.param_type = prev.param_type;
        } else {
            c.param_name = unique(NameFromPath(path));
            c.param_type = type;
            path_params[path] = c.param_name;
        }
        out.candidates.push_back(c);
    };
    for (const std::string& id : ordered) {
        const Node* n = FindNode(root, id);
        if (!n) continue;
        Walk(*n, [&](const Node& x) {
            if (x.type == "Component") {
                const Component* def = project ? project->FindComponent(x.component) : nullptr;
                if (!def) return;
                for (const Param& p : def->params) {
                    if (p.type.compare(0, 8, "binding<") != 0) continue;
                    const PropValue* v = x.Prop(p.name.c_str());
                    if (v && v->raw.IsString() && !v->raw.AsString().empty()) add_binding(x, p.name, v->raw.AsString(), p.type);
                }
                return;
            }
            const WidgetDef* def = catalog ? catalog->Find(x.type.c_str()) : FindWidget(x.type.c_str());
            for (const auto& b : x.bind) {
                if (!b.second.raw.IsString() || b.second.raw.AsString().empty()) continue;
                add_binding(x, b.first, b.second.raw.AsString(), "binding<" + BindTargetFor(def, b.first) + ">");
            }
            static const char* kTextProps[] = {"label", "text", "title"};
            for (const char* key : kTextProps) {
                const PropValue* p = x.Prop(key);
                if (!p || !p->raw.IsString() || p->raw.AsString().empty()) continue;
                ExtractCandidate c;
                c.kind = ExtractCandidate::Kind::Literal;
                c.node_id = x.id;
                c.key = key;
                c.value = p->raw.AsString();
                c.param_name = unique(x.id + "_" + key);
                c.param_type = "string";
                out.candidates.push_back(c);
            }
        });
    }
    return true;
}

bool ApplyExtract(Node& root, const ExtractPlan& plan, Component& out_component, std::string* instance_id,
                  std::string* err) {
    if (!IsIdentifier(plan.name)) return Fail(err, "component name must be an identifier");
    std::map<std::string, std::string> types;
    for (const ExtractCandidate& c : plan.candidates) {
        if (!c.enabled) continue;
        if (!IsIdentifier(c.param_name)) return Fail(err, "parameter name \"" + c.param_name + "\" is not an identifier");
        auto it = types.find(c.param_name);
        if (it != types.end() && it->second != c.param_type)
            return Fail(err, "parameter \"" + c.param_name + "\" is used with two different types");
        types[c.param_name] = c.param_type;
    }
    std::vector<Node> clones;
    for (const std::string& id : plan.node_ids) {
        const Node* n = FindNode(root, id);
        if (!n) return Fail(err, "node " + id + " no longer exists");
        clones.push_back(*n);
    }
    for (const ExtractCandidate& c : plan.candidates) {
        if (!c.enabled) continue;
        Json pv = Json::Object();
        pv.Set("param", Json(c.param_name));
        for (Node& clone : clones) {
            Node* x = FindNode(clone, c.node_id);
            if (!x) continue;
            if (c.kind == ExtractCandidate::Kind::Literal || x->type == "Component") {
                x->SetProp(c.key.c_str(), pv);
            } else {
                for (auto& b : x->bind)
                    if (b.first == c.key) b.second.raw = pv;
            }
        }
    }
    Component comp;
    comp.name = plan.name;
    comp.file = "components/" + plan.name + ".imc.json";
    std::set<std::string> added;
    for (const ExtractCandidate& c : plan.candidates) {
        if (!c.enabled || c.kind != ExtractCandidate::Kind::Binding || added.count(c.param_name)) continue;
        comp.params.push_back(Param{c.param_name, c.param_type, Json()});
        added.insert(c.param_name);
    }
    for (const ExtractCandidate& c : plan.candidates) {
        if (!c.enabled || c.kind != ExtractCandidate::Kind::Literal || added.count(c.param_name)) continue;
        comp.params.push_back(Param{c.param_name, "string", Json(c.value)});
        added.insert(c.param_name);
    }
    if (clones.size() == 1) {
        comp.root = std::move(clones.front());
    } else {
        comp.root.type = "VStack";
        comp.root.children = std::move(clones);
        comp.root.id = "root";
        bool clash = false;
        Walk(comp.root, [&](const Node& n) { clash = clash || (&n != &comp.root && n.id == "root"); });
        if (clash) comp.root.id = GenerateId(comp.root, "VStack");
    }
    Node inst;
    inst.type = "Component";
    inst.component = plan.name;
    inst.id = GenerateId(root, plan.name);
    for (const ExtractCandidate& c : plan.candidates)
        if (c.enabled) inst.SetProp(c.param_name.c_str(), Json(c.value));
    tree::NodeLocation loc;
    if (!tree::LocateNode(root, plan.node_ids.front(), loc)) return Fail(err, "cannot locate " + plan.node_ids.front());
    std::vector<Node>* list = loc.list;
    size_t index = loc.index;
    for (const std::string& id : plan.node_ids) tree::RemoveNode(root, id, nullptr);
    if (index > list->size()) index = list->size();
    list->insert(list->begin() + (long)index, inst);
    if (instance_id) *instance_id = inst.id;
    out_component = std::move(comp);
    return true;
}

}  // namespace imforge::builder::components
