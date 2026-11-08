#include <algorithm>
#include <cmath>
#include <cstring>

#include "emit_internal.h"
#include "imforge/imb_motion.h"
#include "imforge/imb_motion_presets.h"
#include "imforge/imforge_motion_json.h"

namespace imforge {
namespace codegen {

namespace {

const char* const kTriggerJson[] = {"enter", "exit", "hover", "press", "focus", "loop", "change", "attention"};
const char* const kTriggerCpp[] = {"Enter", "Exit", "Hover", "Press", "Focus", "Loop", "Change", "Attention"};

const char* TriggerCpp(const std::string& t) {
    for (size_t i = 0; i < sizeof(kTriggerJson) / sizeof(kTriggerJson[0]); ++i)
        if (t == kTriggerJson[i]) return kTriggerCpp[i];
    return nullptr;
}

// the shipped presets only, a project animation registered under a preset name doesn't count
const imb::AnimDef* PresetDef(const std::string& name) {
    int count = 0;
    const char* const* names = imb::presets::Names(count);
    for (int i = 0; i < count; ++i)
        if (name == names[i]) return imb::FindAnim(names[i]);
    return nullptr;
}

bool AnimatesTextReveal(const imb::AnimDef* def) {
    for (int i = 0; def && i < def->track_count; ++i)
        if (def->tracks[i].prop == imb::Prop::TextReveal) return true;
    return false;
}

struct Resolved {
    const MotionSpec* spec = nullptr;
    std::string def_expr;
    const char* trigger = nullptr;
    bool custom = false;
    bool text_reveal = false;
};

struct Plan {
    MotionMode mode = MotionMode::None;
    std::vector<Resolved> list;
    int stagger_index = 0;
};

// the interpreter registers project animations in order, so the last one with a name wins and it
// also shadows a preset of the same name
const Json* ProjectAnimation(const Project& project, const std::string& name) {
    const Json* found = nullptr;
    for (const Json& a : project.animations)
        if (a.IsObject() && a["name"].IsString() && a["name"].AsString() == name) found = &a;
    return found;
}

bool ResolveOne(const EmitCtx& ctx, const MotionSpec& m, Resolved& r, std::vector<std::string>* problems) {
    r.spec = &m;
    r.trigger = TriggerCpp(m.trigger);
    if (!r.trigger) {
        if (problems) problems->push_back("unknown motion trigger '" + m.trigger + "', motion skipped");
        return false;
    }
    const std::string& name = m.Name();
    if (name.empty()) {
        if (problems) problems->push_back("motion without preset or custom name skipped");
        return false;
    }
    AnimStorage st;
    const Json* a = ProjectAnimation(ctx.project, name);
    if (a && AnimFromJson(*a, st, nullptr)) {
        r.def_expr = "imforge_anim::" + SanitizeIdentifier(name) + "()";
        r.custom = true;
        r.text_reveal = AnimatesTextReveal(st.Def());
        return true;
    }
    if (!m.preset.empty()) {
        if (const imb::AnimDef* def = PresetDef(m.preset)) {
            r.def_expr = "imb::presets::" + m.preset + "()";
            r.text_reveal = AnimatesTextReveal(def);
            return true;
        }
        if (problems) problems->push_back("unknown motion preset '" + m.preset + "', motion skipped");
        return false;
    }
    if (problems) problems->push_back("unknown custom animation '" + m.custom + "', motion skipped");
    return false;
}

bool IsDirectChild(const Node& parent, const Node& n) {
    for (const Node& c : parent.children)
        if (&c == &n) return true;
    return false;
}

bool ParentStaggers(const EmitCtx& ctx, const Node& n) {
    if (!ctx.parent || !IsDirectChild(*ctx.parent, n)) return false;
    for (const MotionSpec& m : ctx.parent->anim.motions)
        if (m.stagger > 0) return true;
    return false;
}

// a container's motion with a stagger plays on each direct child (delay + stagger * index)
// instead of the container, own motions first, the inherited ones after
Plan MakePlan(const EmitCtx& ctx, const Node& n, std::vector<std::string>* problems, std::vector<std::string>* infos) {
    Plan p;
    if (!ctx.opts.features_animations) return p;
    bool inherits = ParentStaggers(ctx, n);
    if (n.anim.motions.empty() && !inherits) return p;
    MotionMode mode = ModeOf(n);
    if (mode == MotionMode::None) {
        if (infos && !n.anim.motions.empty()) infos->push_back("motions on " + n.type + " are not played");
        return p;
    }
    const bool delegates = !n.children.empty();
    auto add = [&](const MotionSpec& m, bool quiet) {
        Resolved r;
        if (!ResolveOne(ctx, m, r, quiet ? nullptr : problems)) return;
        if (p.list.size() >= static_cast<size_t>(imb::detail::kMaxMotionBindings)) {
            if (problems && !quiet)
                problems->push_back("more than " + std::to_string(imb::detail::kMaxMotionBindings) + " motions on one item, the rest are ignored");
            return;
        }
        p.list.push_back(r);
    };
    for (const MotionSpec& m : n.anim.motions) {
        if (delegates && m.stagger > 0) {
            Resolved r;
            ResolveOne(ctx, m, r, problems);
            continue;
        }
        add(m, false);
    }
    if (inherits && mode == MotionMode::Outer) {
        for (const MotionSpec& m : ctx.parent->anim.motions)
            if (m.stagger > 0) add(m, true);
        p.stagger_index = static_cast<int>(IndexIn(*ctx.parent, n));
    }
    if (!p.list.empty()) p.mode = mode;
    return p;
}

std::string MotionId(const Node& n) { return "imb_mid_" + SanitizeIdentifier(n.id); }
std::string TableName(const Node& n) { return "imb_m_" + SanitizeIdentifier(n.id); }

// a Window root takes its id before ImGui::Begin, where every panel's "window" would hash the same
std::string MotionKey(const EmitCtx& ctx, const Node& n) {
    bool root = ctx.panel && &n == &ctx.panel->root;
    return ctx.StrLit((root ? ctx.panel->name + "/" : std::string()) + n.id + "##m");
}

bool InScope(const EmitCtx& ctx, const Node& n) {
    return std::find(ctx.motion_ids.begin(), ctx.motion_ids.end(), n.id) != ctx.motion_ids.end();
}

const MotionPending* FindPending(const EmitCtx& ctx, const Node& n) {
    for (const MotionPending& m : ctx.motion_pending)
        if (m.node == &n) return &m;
    return nullptr;
}

std::string BeginCall(const Node& n, const MotionPending& m) {
    return "imb::BeginMotion(" + MotionId(n) + ", " + TableName(n) + ", " + std::to_string(m.count) + ", " + m.opts + ")";
}

std::string Positive(float v, float fallback) { return FloatLitOf(v > 0 ? v : fallback); }

}  // namespace

MotionMode ModeOf(const Node& n) {
    const std::string& t = n.type;
    if (t == "Window" || t == "Popup" || t == "Modal" || t == "Tab" || t == "Menu") return MotionMode::Content;
    if (t == "MenuBar") return MotionMode::None;
    return MotionMode::Outer;
}

std::string MotionHeader(const Options& opts, const char* file) {
    const std::string& rh = opts.settings.runtime_header;
    size_t p = rh.find_last_of("/\\");
    return "\"" + (p == std::string::npos ? std::string() : rh.substr(0, p + 1)) + file + "\"";
}

MotionMode DeclareMotion(EmitCtx& ctx, const Node& n, const std::string& visible) {
    std::vector<std::string> problems, infos;
    Plan p = MakePlan(ctx, n, &problems, &infos);
    for (const std::string& s : problems) ctx.Diag(Diagnostic::Level::Warning, &n, "anim", s);
    for (const std::string& s : infos) ctx.Diag(Diagnostic::Level::Info, &n, "anim", s);
    if (p.mode == MotionMode::None) return p.mode;
    Writer& w = ctx.w;
    ctx.Include(MotionHeader(ctx.opts, "imb_motion.h"));
    for (const Resolved& r : p.list) {
        if (r.custom) ctx.uses_custom_anims = true;
        else ctx.Include(MotionHeader(ctx.opts, "imb_motion_presets.h"));
    }
    if (ctx.uses_custom_anims) ctx.Include("\"imforge_animations.h\"");
    w.Line("static const imb::MotionBinding " + TableName(n) + "[] = {");
    {
        Writer::Scope s = w.Scoped();
        for (const Resolved& r : p.list) {
            const MotionSpec& m = *r.spec;
            w.Line("{" + r.def_expr + ", imb::Trigger::" + r.trigger + ", " + Positive(m.duration_scale, 1.0f) + ", " +
                   Positive(m.delay, 0.0f) + ", " + std::to_string(m.iterations < -1 ? -1 : m.iterations) + ", " + Positive(m.stagger, 0.0f) + "},");
        }
    }
    w.Line("};");
    w.Line("const ImGuiID " + MotionId(n) + " = ImGui::GetID(" + MotionKey(ctx, n) + ");");
    ctx.motion_ids.push_back(n.id);
    MotionPending pending;
    pending.node = &n;
    pending.count = static_cast<int>(p.list.size());
    pending.opts = "imb::MotionOptions()";
    for (const Resolved& r : p.list) pending.text_reveal = pending.text_reveal || r.text_reveal;
    // content scopes ignore visibleIf, the container itself isn't drawn while hidden
    if (p.mode == MotionMode::Outer && (!visible.empty() || p.stagger_index > 0)) {
        pending.opts = "imb_mo_" + SanitizeIdentifier(n.id);
        w.Line("imb::MotionOptions " + pending.opts + ";");
        if (!visible.empty()) w.Line(pending.opts + ".visible = " + visible + ";");
        if (p.stagger_index > 0) w.Line(pending.opts + ".stagger_index = " + std::to_string(p.stagger_index) + ";");
    }
    ctx.motion_pending.push_back(pending);
    return p.mode;
}

std::string MotionBeginCall(const EmitCtx& ctx, const Node& n) {
    const MotionPending* m = FindPending(ctx, n);
    return m ? BeginCall(n, *m) : std::string("false");
}

void ForgetMotion(EmitCtx& ctx, const Node& n) {
    auto it = std::find(ctx.motion_ids.begin(), ctx.motion_ids.end(), n.id);
    if (it != ctx.motion_ids.end()) ctx.motion_ids.erase(it);
    for (size_t i = ctx.motion_pending.size(); i > 0; --i) {
        if (ctx.motion_pending[i - 1].node != &n) continue;
        ctx.motion_pending.erase(ctx.motion_pending.begin() + static_cast<long>(i - 1));
        break;
    }
}

bool OpenContentMotion(EmitCtx& ctx, const Node& n) {
    const MotionPending* m = FindPending(ctx, n);
    if (!m || ModeOf(n) != MotionMode::Content) return false;
    ctx.w.Line("if (" + BeginCall(n, *m) + ") {");
    ctx.w.Indent();
    return true;
}

void CloseContentMotion(EmitCtx& ctx) {
    ctx.w.Line("imb::EndMotion();");
    ctx.w.Unindent();
    ctx.w.Line("}");
}

bool MotionListens(const EmitCtx& ctx, const Node& n, const char* event) {
    if (!ctx.opts.features_animations) return false;
    for (const MotionSpec& m : n.anim.motions)
        if (m.on == event && TriggerCpp(m.trigger)) return true;
    return false;
}

void EmitMotionTriggers(EmitCtx& ctx, const Node& n, const char* event) {
    if (!MotionListens(ctx, n, event)) return;
    std::string id = InScope(ctx, n) ? MotionId(n) : "ImGui::GetID(" + MotionKey(ctx, n) + ")";
    std::vector<const char*> done;
    for (const MotionSpec& m : n.anim.motions) {
        const char* t = TriggerCpp(m.trigger);
        if (m.on != event || !t || std::find(done.begin(), done.end(), t) != done.end()) continue;
        done.push_back(t);
        ctx.w.Line("imb::TriggerMotion(" + id + ", imb::Trigger::" + t + ");");
    }
}

std::string MotionTextReveal(EmitCtx& ctx, const Node& n) {
    const MotionPending* m = FindPending(ctx, n);
    if (!m || !m->text_reveal) return std::string();
    ctx.uses_text_reveal = true;
    return "imb::MotionTextReveal(" + MotionId(n) + ")";
}

std::string TextRevealHelper(const Writer& like) {
    Writer w;
    w.opts = like.opts;
    w.Line("#ifndef IMFORGE_TEXT_REVEAL");
    w.Line("#define IMFORGE_TEXT_REVEAL");
    w.Line("// cuts `text` to its first round(fraction * n) utf-8 characters, same rule as the interpreter");
    w.Line("inline void ImbTextReveal(char* text, float fraction) {");
    {
        Writer::Scope s = w.Scoped();
        w.Line("if (!(fraction < 1.0f)) return;");
        w.Line("if (!(fraction > 0.0f)) {");
        w.Indent();
        w.Line("text[0] = 0;");
        w.Line("return;");
        w.Unindent();
        w.Line("}");
        w.Line("int chars = 0;");
        w.Line("for (const char* p = text; *p; ++p) chars += ((unsigned char)*p & 0xC0) != 0x80;");
        w.Line("const int keep = (int)(fraction * (float)chars + 0.5f);");
        w.Line("int seen = 0;");
        w.Line("for (char* p = text; *p; ++p) {");
        w.Indent();
        w.Line("if (((unsigned char)*p & 0xC0) != 0x80 && seen++ == keep) {");
        w.Indent();
        w.Line("*p = 0;");
        w.Line("return;");
        w.Unindent();
        w.Line("}");
        w.Unindent();
        w.Line("}");
    }
    w.Line("}");
    w.Line("#endif");
    w.Blank();
    return w.out;
}

bool UsesCustomAnimations(const Project& project, const Node& root, int depth) {
    if (depth > 32) return false;
    bool found = false;
    Walk(root, [&](const Node& n) {
        if (found) return;
        for (const MotionSpec& m : n.anim.motions) found = found || (!m.Name().empty() && project.FindAnimation(m.Name()));
        if (found || n.type != "Component") return;
        std::string cname = n.component;
        if (cname.empty()) {
            const PropValue* p = n.Prop("component");
            if (p && p->raw.IsString()) cname = p->raw.AsString();
        }
        if (const Component* c = project.FindComponent(cname)) found = UsesCustomAnimations(project, c->root, depth + 1);
    });
    return found;
}

}  // namespace codegen
}  // namespace imforge
