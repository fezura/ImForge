#include <algorithm>

#include "emit_internal.h"
#include "imforge/imb_motion.h"
#include "imforge/imforge_motion_json.h"

namespace imforge {
namespace codegen {

namespace {

const char* PropEnum(imb::Prop p) {
    static const char* const kNames[] = {"Opacity", "TranslateX", "TranslateY", "Scale", "ScaleX", "ScaleY", "Rotate",
                                         "SkewX", "SkewY", "Tint", "Reveal", "TextReveal", "Glow", "Shadow"};
    int i = static_cast<int>(p);
    return i >= 0 && i < static_cast<int>(sizeof(kNames) / sizeof(kNames[0])) ? kNames[i] : "Opacity";
}

const char* DirectionEnum(imb::Direction d) {
    switch (d) {
    case imb::Direction::Reverse: return "Reverse";
    case imb::Direction::Alternate: return "Alternate";
    case imb::Direction::AlternateReverse: return "AlternateReverse";
    default: return "Normal";
    }
}

const char* FillEnum(imb::Fill f) {
    switch (f) {
    case imb::Fill::None: return "None";
    case imb::Fill::Forwards: return "Forwards";
    case imb::Fill::Backwards: return "Backwards";
    default: return "Both";
    }
}

const char* RevealEnum(imb::RevealDir r) {
    switch (r) {
    case imb::RevealDir::Right: return "Right";
    case imb::RevealDir::Up: return "Up";
    case imb::RevealDir::Down: return "Down";
    case imb::RevealDir::Center: return "Center";
    default: return "Left";
    }
}

const char* TriggerEnum(imb::Trigger t) {
    static const char* const kNames[] = {"Enter", "Exit", "Hover", "Press", "Focus", "Loop", "Change", "Attention"};
    int i = static_cast<int>(t);
    return i >= 0 && i < 8 ? kNames[i] : "Enter";
}

const char* EaseKindEnum(imb::EaseKind k) {
    switch (k) {
    case imb::EaseKind::CubicBezier: return "CubicBezier";
    case imb::EaseKind::Steps: return "Steps";
    case imb::EaseKind::Spring: return "Spring";
    default: return "Named";
    }
}

std::string Floats4(const float* v) {
    return "{" + FloatLitOf(v[0]) + ", " + FloatLitOf(v[1]) + ", " + FloatLitOf(v[2]) + ", " + FloatLitOf(v[3]) + "}";
}

void EmitAccessor(Writer& w, const std::string& fn, const imb::AnimDef& def) {
    w.Line("inline const imb::AnimDef* " + fn + "() {");
    {
        Writer::Scope s = w.Scoped();
        for (int t = 0; t < def.track_count; ++t) {
            const imb::Track& tr = def.tracks[t];
            if (tr.count <= 0) continue;
            w.Line("static const imb::Keyframe k" + std::to_string(t) + "[] = {");
            {
                Writer::Scope ks = w.Scoped();
                for (int k = 0; k < tr.count; ++k) {
                    const imb::Keyframe& kf = tr.keys[k];
                    w.Line("{" + FloatLitOf(kf.t) + ", " + Floats4(kf.v) + ", {imb::EaseKind::" + EaseKindEnum(kf.ease.kind) +
                           ", imb::Ease::" + EaseName(kf.ease.named) + ", " + Floats4(kf.ease.p) + "}},");
                }
            }
            w.Line("};");
        }
        if (def.track_count > 0) {
            w.Line("static const imb::Track tracks[] = {");
            {
                Writer::Scope ts = w.Scoped();
                for (int t = 0; t < def.track_count; ++t) {
                    const imb::Track& tr = def.tracks[t];
                    std::string keys = tr.count > 0 ? "k" + std::to_string(t) : std::string("nullptr");
                    w.Line("{imb::Prop::" + std::string(PropEnum(tr.prop)) + ", " + keys + ", " + std::to_string(tr.count > 0 ? tr.count : 0) + "},");
                }
            }
            w.Line("};");
        }
        w.Line("static const imb::AnimDef def = {");
        {
            Writer::Scope ds = w.Scoped();
            w.Line(CppStringLiteral(def.name ? def.name : "") + ", " + CppStringLiteral(def.category ? def.category : "") + ", " +
                   FloatLitOf(def.duration) + ", " + FloatLitOf(def.delay) + ", " + std::to_string(def.iterations) + ",");
            w.Line("imb::Direction::" + std::string(DirectionEnum(def.direction)) + ", imb::Fill::" + FillEnum(def.fill) + ", ImVec2(" +
                   FloatLitOf(def.origin.x) + ", " + FloatLitOf(def.origin.y) + "),");
            w.Line("imb::RevealDir::" + std::string(RevealEnum(def.reveal_dir)) + ", imb::Trigger::" + TriggerEnum(def.default_trigger) + ", " +
                   (def.track_count > 0 ? "tracks" : "nullptr") + ", " + std::to_string(def.track_count) + "};");
        }
        w.Line("return &def;");
    }
    w.Line("}");
    w.Blank();
}

}  // namespace

Result GenerateAnimations(const Project& project, const Options& opts) {
    Result r;
    if (project.animations.empty()) return r;
    // the interpreter registers in project order and a later entry replaces an earlier one with the same name
    std::vector<std::pair<std::string, AnimStorage>> anims;
    for (const Json& a : project.animations) {
        AnimStorage st;
        Error err;
        if (!AnimFromJson(a, st, &err)) {
            Diagnostic d;
            d.level = Diagnostic::Level::Warning;
            d.prop = "animations";
            d.message = "custom animation skipped: " + err.message;
            r.diagnostics.push_back(d);
            continue;
        }
        std::string name = st.Name();
        auto it = std::find_if(anims.begin(), anims.end(), [&](const std::pair<std::string, AnimStorage>& e) { return e.first == name; });
        if (it != anims.end()) it->second = std::move(st);
        else anims.emplace_back(name, std::move(st));
    }
    std::sort(anims.begin(), anims.end(),
              [](const std::pair<std::string, AnimStorage>& a, const std::pair<std::string, AnimStorage>& b) { return a.first < b.first; });

    Writer w;
    w.opts = &opts;
    WriteFileHead(w, "animations schema=1");
    w.Line("#pragma once");
    w.Line("#include " + MotionHeader(opts, "imb_motion.h"));
    OpenNamespace(w, opts);
    w.Line("namespace imforge_anim {");
    w.Blank();
    std::vector<std::string> fns;
    for (const auto& a : anims) {
        std::string fn = SanitizeIdentifier(a.first);
        if (std::find(fns.begin(), fns.end(), fn) != fns.end() || fn == "RegisterAnimations") {
            Diagnostic d;
            d.level = Diagnostic::Level::Warning;
            d.prop = "animations";
            d.message = "custom animation '" + a.first + "' skipped: its C++ name " + fn + " is already taken";
            r.diagnostics.push_back(d);
            continue;
        }
        fns.push_back(fn);
        EmitAccessor(w, fn, *a.second.Def());
    }
    w.Line("// call once before looking animations up by name, the generated panels do it for you");
    w.Line("inline void RegisterAnimations() {");
    {
        Writer::Scope s = w.Scoped();
        w.Line("static bool done = false;");
        w.Line("if (done) return;");
        w.Line("done = true;");
        for (const std::string& fn : fns) w.Line("imb::RegisterAnim(" + fn + "());");
    }
    w.Line("}");
    w.Blank();
    w.Line("}  // namespace imforge_anim");
    CloseNamespace(w, opts);
    FileOut f;
    f.path = "imforge_animations.h";
    f.content = w.out;
    r.files.push_back(f);
    return r;
}

}  // namespace codegen
}  // namespace imforge
