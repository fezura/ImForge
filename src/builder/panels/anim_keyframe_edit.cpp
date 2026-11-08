#include "anim_keyframe_edit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imforge/imforge_anim_names.h"
#include "imforge/imforge_core.h"

namespace imforge::builder::kf {

namespace {

const PropInfo kProps[] = {
    {"opacity", "Opacity", 1.0f, 0.0f, 1.0f, 0.01f, "%.2f", false},
    {"translateX", "Translate X", 0.0f, -4096.0f, 4096.0f, 0.5f, "%.1f px", false},
    {"translateY", "Translate Y", 0.0f, -4096.0f, 4096.0f, 0.5f, "%.1f px", false},
    {"scale", "Scale", 1.0f, 0.0f, 3.0f, 0.01f, "%.2f", false},
    {"scaleX", "Scale X", 1.0f, 0.0f, 3.0f, 0.01f, "%.2f", false},
    {"scaleY", "Scale Y", 1.0f, 0.0f, 3.0f, 0.01f, "%.2f", false},
    {"rotate", "Rotate", 0.0f, -720.0f, 720.0f, 0.5f, "%.1f deg", false},
    {"skewX", "Skew X", 0.0f, -85.0f, 85.0f, 0.25f, "%.1f deg", false},
    {"skewY", "Skew Y", 0.0f, -85.0f, 85.0f, 0.25f, "%.1f deg", false},
    {"tint", "Tint", 1.0f, 0.0f, 1.0f, 0.01f, "%.2f", true},
    {"reveal", "Reveal", 1.0f, 0.0f, 1.0f, 0.01f, "%.2f", false},
    {"textReveal", "Text reveal", 1.0f, 0.0f, 1.0f, 0.01f, "%.2f", false},
    {"glow", "Glow", 0.0f, 0.0f, 1.0f, 0.01f, "%.2f", false},
    {"shadow", "Shadow", 0.0f, 0.0f, 1.0f, 0.01f, "%.2f", false},
};
const int kPropCount = (int)(sizeof(kProps) / sizeof(kProps[0]));

// category, trigger it gets when the json has none (same table as the loader)
const char* const kCategoryTriggers[] = {"entrance", "enter", "exit", "exit", "attention", "attention",
                                         "hover", "hover", "press", "press", "loop", "loop",
                                         "text", "enter", "change", "change", "stagger", "enter"};
const char* const kReservedNames[] = {"RegisterAll", "Names", "None", "True", "False", "Bool", "Status", "Success", "Always"};
const size_t kMaxNameLength = 48;

float Clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

void IdentityValue(const std::string& prop, float out[4]) {
    const PropInfo* p = FindProp(prop);
    float id = p ? p->identity : 0.0f;
    bool color = p && p->color;
    out[0] = id;
    out[1] = color ? id : 0.0f;
    out[2] = color ? id : 0.0f;
    out[3] = color ? id : 0.0f;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

// "name(a, b, c)" -> args; false if the shape is off
bool SplitCall(const std::string& s, const char* fn, std::vector<float>& args) {
    size_t n = std::strlen(fn);
    if (s.size() < n + 2 || s.compare(0, n, fn) != 0 || s[n] != '(' || s.back() != ')') return false;
    std::string inner = s.substr(n + 1, s.size() - n - 2);
    args.clear();
    size_t start = 0;
    while (start <= inner.size()) {
        size_t comma = inner.find(',', start);
        if (comma == std::string::npos) comma = inner.size();
        double v = 0;
        if (!core_detail::ParseNumber(Trim(inner.substr(start, comma - start)), v) || !std::isfinite(v)) return false;
        args.push_back((float)v);
        start = comma + 1;
    }
    return true;
}

std::string Num(float f) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.3f", (double)f);
    for (char* p = buf; *p; ++p)
        if (*p == ',') *p = '.';
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

Json NumJson(float f) { return Json(core_detail::FloatToNum(f)); }

bool ParseKeyValue(const Json& j, const std::string& prop, float out[4]) {
    IdentityValue(prop, out);
    if (j.IsNumber()) {
        out[0] = (float)j.AsDouble();
        return true;
    }
    if (j.IsString()) {
        bool ok = false;
        Vec4 c = ParseColor(j.AsString(), &ok);
        if (!ok) return false;
        out[0] = c.x;
        out[1] = c.y;
        out[2] = c.z;
        out[3] = c.w;
        return true;
    }
    if (j.IsArray()) {
        for (size_t i = 0; i < j.Size() && i < 4; ++i) out[i] = (float)j[i].AsDouble(out[i]);
        return true;
    }
    return false;
}

Json KeyValueJson(const std::string& prop, const float v[4]) {
    const PropInfo* p = FindProp(prop);
    if (p && p->color) {
        Vec4 c;
        c.x = v[0];
        c.y = v[1];
        c.z = v[2];
        c.w = v[3];
        return Json(ColorToHex(c));
    }
    return NumJson(v[0]);
}

}  // namespace

const char* CategoryTrigger(const std::string& category) {
    for (size_t i = 0; i + 1 < sizeof(kCategoryTriggers) / sizeof(kCategoryTriggers[0]); i += 2)
        if (category == kCategoryTriggers[i]) return kCategoryTriggers[i + 1];
    return "enter";
}

int CategoryCount() { return (int)(sizeof(kCategoryTriggers) / sizeof(kCategoryTriggers[0]) / 2); }
const char* CategoryAt(int i) { return kCategoryTriggers[(i < 0 ? 0 : (i >= CategoryCount() ? CategoryCount() - 1 : i)) * 2]; }

int PropCount() { return kPropCount; }
const PropInfo& PropAt(int i) { return kProps[i < 0 ? 0 : (i >= kPropCount ? kPropCount - 1 : i)]; }
const PropInfo* FindProp(const std::string& key) {
    for (const PropInfo& p : kProps)
        if (key == p.key) return &p;
    return nullptr;
}

bool AnimEditFromJson(const Json& j, AnimEdit& out, std::string* err) {
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (!j.IsObject()) return fail("animation is not a JSON object");
    AnimEdit a;
    a.name = j["name"].AsString("");
    a.category = j["category"].AsString(a.category);
    a.description = j["description"].AsString("");
    a.credit = j["credit"].AsString("");
    if (j["tags"].IsArray())
        for (size_t i = 0; i < j["tags"].Size(); ++i) a.tags.push_back(j["tags"][i].AsString(""));
    a.duration = (float)j["duration"].AsDouble(a.duration);
    a.delay = (float)j["delay"].AsDouble(a.delay);
    a.iterations = j["iterations"].AsInt(a.category == "loop" ? 0 : 1);
    a.direction = j["direction"].AsString(a.direction);
    a.fill = j["fill"].AsString(a.fill);
    a.reveal_dir = j["revealDir"].AsString(a.reveal_dir);
    a.trigger = j["trigger"].AsString(CategoryTrigger(a.category));
    a.ease = j["ease"].AsString("Linear");
    a.origin_x = (float)j["origin"]["x"].AsDouble(a.origin_x);
    a.origin_y = (float)j["origin"]["y"].AsDouble(a.origin_y);
    const Json& tracks = j["tracks"];
    if (!tracks.IsNull() && !tracks.IsObject()) return fail("\"tracks\" must be an object");
    for (const auto& kv : tracks.obj) {
        if (!kv.second.IsArray()) return fail("track \"" + kv.first + "\" is not an array");
        TrackEdit tr;
        tr.prop = kv.first;
        for (const Json& kj : kv.second.arr) {
            Key k;
            k.t = (float)kj["t"].AsDouble(0);
            if (!ParseKeyValue(kj["v"], tr.prop, k.v)) return fail("track \"" + kv.first + "\": bad keyframe value");
            k.ease = kj["ease"].AsString("");
            tr.keys.push_back(k);
        }
        NormalizeTrack(tr);
        a.tracks.push_back(std::move(tr));
    }
    out = std::move(a);
    return true;
}

Json AnimEditToJson(const AnimEdit& a) {
    Json o = Json::Object();
    o.Set("name", Json(a.name));
    o.Set("category", Json(a.category));
    if (!a.description.empty()) o.Set("description", Json(a.description));
    if (!a.tags.empty()) {
        Json t = Json::Array();
        for (const std::string& s : a.tags) t.Push(Json(s));
        o.Set("tags", std::move(t));
    }
    if (!a.credit.empty()) o.Set("credit", Json(a.credit));
    o.Set("duration", NumJson(a.duration));
    o.Set("delay", NumJson(a.delay));
    o.Set("iterations", Json(a.iterations));
    o.Set("direction", Json(a.direction));
    o.Set("fill", Json(a.fill));
    Json origin = Json::Object();
    origin.Set("x", NumJson(a.origin_x));
    origin.Set("y", NumJson(a.origin_y));
    o.Set("origin", std::move(origin));
    o.Set("revealDir", Json(a.reveal_dir));
    o.Set("trigger", Json(a.trigger));
    o.Set("ease", Json(a.ease));
    Json tracks = Json::Object();
    for (const TrackEdit& tr : a.tracks) {
        Json arr = Json::Array();
        for (const Key& k : tr.keys) {
            Json kj = Json::Object();
            kj.Set("t", NumJson(k.t));
            kj.Set("v", KeyValueJson(tr.prop, k.v));
            if (!k.ease.empty()) kj.Set("ease", Json(k.ease));
            arr.Push(std::move(kj));
        }
        tracks.Set(tr.prop, std::move(arr));
    }
    o.Set("tracks", std::move(tracks));
    return o;
}

AnimEdit MakeDefaultAnim(const std::string& name) {
    AnimEdit a;
    a.name = name;
    a.category = "entrance";
    a.description = "Custom animation.";
    a.duration = 0.5f;
    a.ease = "OutCubic";
    a.trigger = "enter";
    TrackEdit op = MakeTrack("opacity");
    op.keys.front().v[0] = 0.0f;
    TrackEdit ty = MakeTrack("translateY");
    ty.keys.front().v[0] = 12.0f;
    a.tracks.push_back(op);
    a.tracks.push_back(ty);
    return a;
}

bool ParseEase(const std::string& in, EaseParsed& out) {
    std::string s = Trim(in);
    std::vector<float> args;
    EaseParsed e;
    if (SplitCall(s, "cubic-bezier", args)) {
        if (args.size() != 4 || args[0] < 0.0f || args[0] > 1.0f || args[2] < 0.0f || args[2] > 1.0f) return false;
        if (args[1] < -5.0f || args[1] > 5.0f || args[3] < -5.0f || args[3] > 5.0f) return false;
        e.type = EaseType::CubicBezier;
        e.name.clear();
        for (int i = 0; i < 4; ++i) e.p[i] = args[(size_t)i];
    } else if (SplitCall(s, "steps", args)) {
        if (args.size() != 1 || args[0] < 1.0f || args[0] > 100.0f || args[0] != std::floor(args[0])) return false;
        e.type = EaseType::Steps;
        e.name.clear();
        e.p[0] = args[0];
    } else if (SplitCall(s, "spring", args)) {
        if (args.size() != 2 || args[0] <= 0.0f || args[0] > 10000.0f || args[1] <= 0.0f || args[1] > 1000.0f) return false;
        e.type = EaseType::Spring;
        e.name.clear();
        e.p[0] = args[0];
        e.p[1] = args[1];
    } else {
        imb::Ease named;
        if (!TryEaseFromName(s.c_str(), &named)) return false;
        e.type = EaseType::Named;
        e.name = s;
    }
    out = e;
    return true;
}

std::string FormatEase(const EaseParsed& e) {
    switch (e.type) {
    case EaseType::CubicBezier:
        return "cubic-bezier(" + Num(e.p[0]) + "," + Num(e.p[1]) + "," + Num(e.p[2]) + "," + Num(e.p[3]) + ")";
    case EaseType::Steps: return "steps(" + std::to_string((int)std::lround(e.p[0])) + ")";
    case EaseType::Spring: return "spring(" + Num(e.p[0]) + "," + Num(e.p[1]) + ")";
    case EaseType::Named: break;
    }
    return e.name;
}

bool IsValidEase(const std::string& s) {
    EaseParsed e;
    return ParseEase(s, e);
}

float SnapTime(float t, bool free) {
    t = Clamp01(t);
    if (free) return t;
    float per_unit = std::round(1.0f / kSnapStep);
    return Clamp01(std::round(t * per_unit) / per_unit);
}

float RetimeKey(TrackEdit& tr, int index, float new_t, bool free) {
    int n = (int)tr.keys.size();
    if (index < 0 || index >= n) return 0.0f;
    if (index == 0 || index == n - 1) return tr.keys[(size_t)index].t;
    float t = SnapTime(new_t, free);
    float lo = tr.keys[(size_t)index - 1].t + kMinKeyGap;
    float hi = tr.keys[(size_t)index + 1].t - kMinKeyGap;
    if (lo > hi) return tr.keys[(size_t)index].t;
    t = t < lo ? lo : (t > hi ? hi : t);
    tr.keys[(size_t)index].t = t;
    return t;
}

void SampleTrack(const TrackEdit& tr, float t, const std::string& default_ease, const EaseFn& sample, float out[4]) {
    if (tr.keys.empty()) {
        IdentityValue(tr.prop, out);
        return;
    }
    const Key& first = tr.keys.front();
    const Key& last = tr.keys.back();
    const Key* a = &first;
    const Key* b = nullptr;
    if (t > first.t && t < last.t) {
        for (size_t i = 0; i + 1 < tr.keys.size(); ++i) {
            if (t >= tr.keys[i].t && t < tr.keys[i + 1].t) {
                a = &tr.keys[i];
                b = &tr.keys[i + 1];
                break;
            }
        }
    } else if (t >= last.t) {
        a = &last;
    }
    if (!b || b->t <= a->t) {
        for (int i = 0; i < 4; ++i) out[i] = a->v[i];
        return;
    }
    float u = (t - a->t) / (b->t - a->t);
    const std::string& ease = a->ease.empty() ? default_ease : a->ease;
    float k = sample ? sample(ease, u) : u;
    for (int i = 0; i < 4; ++i) out[i] = a->v[i] + (b->v[i] - a->v[i]) * k;
}

int InsertKey(TrackEdit& tr, float t, const float* value, const std::string& default_ease, const EaseFn& sample) {
    t = Clamp01(t);
    if (tr.keys.empty()) tr = MakeTrack(tr.prop);
    for (size_t i = 0; i < tr.keys.size(); ++i)
        if (std::fabs(tr.keys[i].t - t) < kMinKeyGap) return (int)i;
    if ((int)tr.keys.size() >= kMaxKeysPerTrack) return -1;
    Key k;
    k.t = t;
    if (value) {
        for (int i = 0; i < 4; ++i) k.v[i] = value[i];
    } else {
        SampleTrack(tr, t, default_ease, sample, k.v);
    }
    size_t pos = 0;
    while (pos < tr.keys.size() && tr.keys[pos].t < t) ++pos;
    if (pos > 0) k.ease = tr.keys[pos - 1].ease;
    tr.keys.insert(tr.keys.begin() + (long)pos, k);
    return (int)pos;
}

bool CanDeleteKey(const TrackEdit& tr, int index) {
    int n = (int)tr.keys.size();
    return n > 2 && index > 0 && index < n - 1;
}

bool DeleteKey(TrackEdit& tr, int index) {
    if (!CanDeleteKey(tr, index)) return false;
    tr.keys.erase(tr.keys.begin() + index);
    return true;
}

void NormalizeTrack(TrackEdit& tr) {
    for (Key& k : tr.keys) k.t = Clamp01(k.t);
    std::stable_sort(tr.keys.begin(), tr.keys.end(), [](const Key& a, const Key& b) { return a.t < b.t; });
    std::vector<Key> kept;
    for (const Key& k : tr.keys) {
        if (!kept.empty() && k.t - kept.back().t < 1e-6f) continue;
        kept.push_back(k);
    }
    if ((int)kept.size() > kMaxKeysPerTrack) {
        Key last = kept.back();
        kept.resize((size_t)kMaxKeysPerTrack - 1);
        kept.push_back(last);
    }
    if (kept.empty()) {
        tr = MakeTrack(tr.prop);
        return;
    }
    if (kept.size() == 1) kept.push_back(kept.front());
    kept.front().t = 0.0f;
    kept.back().t = 1.0f;
    tr.keys = std::move(kept);
}

TrackEdit MakeTrack(const std::string& prop) {
    TrackEdit tr;
    tr.prop = prop;
    Key a, b;
    IdentityValue(prop, a.v);
    IdentityValue(prop, b.v);
    a.t = 0.0f;
    b.t = 1.0f;
    tr.keys.push_back(a);
    tr.keys.push_back(b);
    return tr;
}

bool IsSortedWithEndpoints(const TrackEdit& tr) {
    if (tr.keys.size() < 2 || tr.keys.front().t != 0.0f || tr.keys.back().t != 1.0f) return false;
    for (size_t i = 1; i < tr.keys.size(); ++i)
        if (!(tr.keys[i].t > tr.keys[i - 1].t)) return false;
    return true;
}

bool IsValidAnimName(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameLength || !(name[0] >= 'A' && name[0] <= 'Z')) return false;
    for (char c : name)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    for (const char* r : kReservedNames)
        if (name == r) return false;
    return true;
}

std::string UniqueAnimName(const std::string& base_in, const std::vector<std::string>& taken) {
    std::string base;
    bool upper_next = true;
    for (char c : base_in) {
        bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum) {
            upper_next = true;
            continue;
        }
        if (upper_next && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        upper_next = false;
        base.push_back(c);
    }
    if (base.empty() || !(base[0] >= 'A' && base[0] <= 'Z')) base = "Anim" + base;
    if (base.size() > kMaxNameLength - 4) base.resize(kMaxNameLength - 4);
    auto used = [&](const std::string& s) {
        for (const char* r : kReservedNames)
            if (s == r) return true;
        return std::find(taken.begin(), taken.end(), s) != taken.end();
    };
    if (!used(base)) return base;
    std::string stem = base;
    while (stem.size() > 1 && stem.back() >= '0' && stem.back() <= '9') stem.pop_back();
    for (int i = 2;; ++i) {
        std::string cand = stem + std::to_string(i);
        if (!used(cand)) return cand;
    }
}

}  // namespace imforge::builder::kf
