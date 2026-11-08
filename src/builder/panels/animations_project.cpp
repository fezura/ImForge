#include "animations_project.h"

#include <algorithm>
#include <map>
#include <memory>

#include "imforge/imb_motion.h"
#include "imforge/imb_motion_presets.h"
#include "imforge/imforge_motion_json.h"

#include "../editor_context.h"
#include "../project_io.h"
#include "anim_keyframe_edit.h"

namespace imforge::builder::anims {

namespace {

struct EditState {
    std::string target;
    bool tab_request = false;
};

EditState& State() {
    static EditState s;
    return s;
}

struct CachedDef {
    Json source;
    std::unique_ptr<AnimStorage> storage;
    std::string error;
};

const size_t kRetiredDefs = 16;

// keyed by animation name. a rebuilt def keeps the old storage around for a bit, previews may still
// hold the pointer this frame
struct DefCacheState {
    std::map<std::string, CachedDef> by_name;
    std::vector<std::unique_ptr<AnimStorage>> retired;
};

DefCacheState& DefCache() {
    static DefCacheState cache;
    return cache;
}

void SyncFiles(Project& p) {
    p.animation_files.clear();
    for (const Json& a : p.animations) p.animation_files.push_back(DefaultAnimationFile(a["name"].AsString("")));
}

template <class F>
void ForEachMotion(Project& p, F&& f) {
    for (Panel& pn : p.panels)
        Walk(pn.root, [&](Node& n) { f(n); });
    for (Component& c : p.components)
        Walk(c.root, [&](Node& n) { f(n); });
}

bool ComponentsUse(const Project& p, const std::string& name) {
    bool used = false;
    for (const Component& c : p.components)
        Walk(c.root, [&](const Node& n) {
            for (const MotionSpec& m : n.anim.motions) used = used || m.custom == name;
        });
    return used;
}

// panel edits ride along in the project snapshot, components need their own undo entry
template <class F>
void EditWithRefs(EditorContext& ctx, const char* label, const std::string& name, F&& f) {
    bool comps = ComponentsUse(ctx.project, name);
    if (comps) ctx.BeginComponentsEdit(label);
    ctx.EditProject(label, f);
    if (comps) ctx.EndEdit();
}

std::string AddAnim(EditorContext& ctx, Json anim, const char* label) {
    std::string name = anim["name"].AsString("");
    ctx.EditProject(label, [&] {
        ctx.project.animations.push_back(std::move(anim));
        SyncFiles(ctx.project);
    });
    SetEditTarget(name);
    ctx.SetStatus(std::string(label) + ": " + name);
    return name;
}

}  // namespace

std::vector<std::string> CustomNames(const Project& p) {
    std::vector<std::string> out;
    for (const Json& a : p.animations) out.push_back(a["name"].AsString(""));
    return out;
}

int IndexOf(const Project& p, const std::string& name) {
    for (size_t i = 0; i < p.animations.size(); ++i)
        if (p.animations[i]["name"].AsString("") == name) return (int)i;
    return -1;
}

std::vector<std::string> PresetNames() {
    static std::vector<std::string> names;
    if (names.empty()) {
        int n = 0;
        const char* const* list = imb::presets::Names(n);
        for (int i = 0; i < n; ++i) names.push_back(list[i]);
    }
    return names;
}

bool IsPresetName(const std::string& name) {
    const std::vector<std::string>& names = PresetNames();
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::vector<std::string> TakenNames(const Project& p) {
    std::vector<std::string> out = CustomNames(p);
    std::vector<std::string> pre = PresetNames();
    out.insert(out.end(), pre.begin(), pre.end());
    return out;
}

int CountUses(const Project& p, const std::string& name) {
    int uses = 0;
    auto count = [&](const Node& n) {
        for (const MotionSpec& m : n.anim.motions)
            if (m.custom == name) {
                ++uses;
                break;
            }
    };
    for (const Panel& pn : p.panels) Walk(pn.root, count);
    for (const Component& c : p.components) Walk(c.root, count);
    return uses;
}

std::string Create(EditorContext& ctx, const std::string& base) {
    std::string name = kf::UniqueAnimName(base, TakenNames(ctx.project));
    return AddAnim(ctx, kf::AnimEditToJson(kf::MakeDefaultAnim(name)), "New animation");
}

std::string Duplicate(EditorContext& ctx, const std::string& name) {
    int idx = IndexOf(ctx.project, name);
    if (idx < 0) return std::string();
    Json copy = ctx.project.animations[(size_t)idx];
    std::string fresh = kf::UniqueAnimName(name + "Copy", TakenNames(ctx.project));
    copy.Set("name", Json(fresh));
    return AddAnim(ctx, std::move(copy), "Duplicate animation");
}

std::string DuplicatePreset(EditorContext& ctx, const std::string& preset) {
    const imb::AnimDef* def = imb::FindAnim(preset.c_str());
    if (!def || !IsPresetName(preset)) return std::string();
    Json j = AnimToJson(*def);
    std::string fresh = kf::UniqueAnimName(preset + "Custom", TakenNames(ctx.project));
    j.Set("name", Json(fresh));
    return AddAnim(ctx, std::move(j), "Duplicate preset");
}

bool Rename(EditorContext& ctx, const std::string& from, const std::string& to, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    int idx = IndexOf(ctx.project, from);
    if (idx < 0) return fail("no animation named " + from);
    if (from == to) return true;
    if (!kf::IsValidAnimName(to)) return fail("use letters, digits and _ (starting with a letter)");
    std::vector<std::string> taken = TakenNames(ctx.project);
    if (std::find(taken.begin(), taken.end(), to) != taken.end())
        return fail(IsPresetName(to) ? to + " is a built-in preset" : to + " already exists");
    EditWithRefs(ctx, "Rename animation", from, [&] {
        ctx.project.animations[(size_t)idx].Set("name", Json(to));
        SyncFiles(ctx.project);
        ForEachMotion(ctx.project, [&](Node& n) {
            for (MotionSpec& m : n.anim.motions)
                if (m.custom == from) m.custom = to;
        });
    });
    if (State().target == from) State().target = to;
    ctx.SetStatus("Renamed " + from + " to " + to);
    return true;
}

bool Remove(EditorContext& ctx, const std::string& name) {
    int idx = IndexOf(ctx.project, name);
    if (idx < 0) return false;
    EditWithRefs(ctx, "Delete animation", name, [&] {
        ctx.project.animations.erase(ctx.project.animations.begin() + idx);
        SyncFiles(ctx.project);
        ForEachMotion(ctx.project, [&](Node& n) {
            auto& ms = n.anim.motions;
            ms.erase(std::remove_if(ms.begin(), ms.end(), [&](const MotionSpec& m) { return m.custom == name; }), ms.end());
        });
    });
    if (State().target == name) {
        std::vector<std::string> names = CustomNames(ctx.project);
        State().target = names.empty() ? std::string() : names[std::min((size_t)idx, names.size() - 1)];
    }
    ctx.SetStatus("Deleted animation " + name);
    return true;
}

bool Replace(EditorContext& ctx, const std::string& name, const Json& anim, const char* undo_label) {
    int idx = IndexOf(ctx.project, name);
    if (idx < 0) return false;
    if (ctx.project.animations[(size_t)idx] == anim) return true;
    ctx.EditProject(undo_label, [&] {
        ctx.project.animations[(size_t)idx] = anim;
        SyncFiles(ctx.project);
    });
    return true;
}

void RequestEdit(EditorContext& ctx, const std::string& name) {
    SetEditTarget(name);
    State().tab_request = true;
    ctx.OpenPanel("Anim Timeline", true);
}

const std::string& EditTarget() { return State().target; }
void SetEditTarget(const std::string& name) { State().target = name; }

bool TakeTabRequest() {
    bool r = State().tab_request;
    State().tab_request = false;
    return r;
}

const imb::AnimDef* DefFromJson(const Json& anim, std::string* err) {
    DefCacheState& cache = DefCache();
    CachedDef& c = cache.by_name[anim["name"].AsString("")];
    if (c.source.IsNull() || !(c.source == anim)) {
        if (c.storage) {
            cache.retired.push_back(std::move(c.storage));
            if (cache.retired.size() > kRetiredDefs) cache.retired.erase(cache.retired.begin());
        }
        c.source = anim;
        c.error.clear();
        auto storage = std::make_unique<AnimStorage>();
        Error e;
        if (AnimFromJson(anim, *storage, &e)) c.storage = std::move(storage);
        else c.error = e.message.empty() ? "animation does not load" : e.message;
    }
    if (err) *err = c.error;
    return c.storage ? c.storage->Def() : nullptr;
}

const imb::AnimDef* CustomDef(const Project& p, const std::string& name, std::string* err) {
    const Json* j = p.FindAnimation(name);
    if (!j) {
        if (err) *err = "no animation named " + name;
        return nullptr;
    }
    return DefFromJson(*j, err);
}

const imb::AnimDef* MotionDef(const Project& p, const MotionSpec& m) {
    if (!m.custom.empty()) return CustomDef(p, m.custom);
    return m.preset.empty() ? nullptr : imb::FindAnim(m.preset.c_str());
}

}  // namespace imforge::builder::anims
