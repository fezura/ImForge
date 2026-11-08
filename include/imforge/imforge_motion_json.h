#pragma once
// custom animations as JSON: animations/<Name>.imanim.json files and *.imanims.json sets.
// The rules match tools/gen_motion_presets.py, keep both in sync.
#include "imb_motion.h"
#include "imforge_anim_names.h"
#include "imforge_value.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace imforge {

const int kMotionMaxKeyframes = 64;
const int kMotionMaxAnimations = 2048;
const int kMotionPropCount = 14;
const int kMotionCategoryCount = 9;

static_assert(static_cast<int>(imb::Prop::Count) == kMotionPropCount, "update the motion prop table");

struct AnimData {
    std::string name, category, description, credit;
    std::vector<std::string> tags;
    std::vector<imb::Track> tracks;
    std::vector<std::vector<imb::Keyframe>> keys;  // keys[i] belongs to tracks[i]
    imb::AnimDef def{};
};

// Owns the strings and arrays an AnimDef points into. The def lives on the heap, so Def() stays
// valid when the storage is moved (vectors of AnimStorage can grow). Copies relink to their own data.
// Call Relink() after editing data by hand.
struct AnimStorage {
    std::unique_ptr<AnimData> data;

    AnimStorage() : data(new AnimData()) { Relink(); }
    AnimStorage(const AnimStorage& o) : data(new AnimData(o.data ? *o.data : AnimData())) { Relink(); }
    AnimStorage& operator=(const AnimStorage& o) {
        if (this != &o) {
            data.reset(new AnimData(o.data ? *o.data : AnimData()));
            Relink();
        }
        return *this;
    }
    AnimStorage(AnimStorage&&) noexcept = default;
    AnimStorage& operator=(AnimStorage&&) noexcept = default;

    const imb::AnimDef* Def() const { return data ? &data->def : nullptr; }
    const char* Name() const { return data ? data->name.c_str() : ""; }

    void Relink() {
        if (!data) return;
        AnimData& d = *data;
        d.keys.resize(d.tracks.size());
        for (size_t i = 0; i < d.tracks.size(); ++i) {
            d.tracks[i].keys = d.keys[i].empty() ? nullptr : d.keys[i].data();
            d.tracks[i].count = static_cast<int>(d.keys[i].size());
        }
        d.def.name = d.name.c_str();
        d.def.category = d.category.c_str();
        d.def.tracks = d.tracks.empty() ? nullptr : d.tracks.data();
        d.def.track_count = static_cast<int>(d.tracks.size());
    }
};

namespace motion_json_detail {

struct PropInfo {
    const char* name;
    imb::Prop prop;
    double lo, hi, identity;
};

inline const PropInfo* Props() {
    static const PropInfo t[kMotionPropCount] = {
        {"opacity", imb::Prop::Opacity, 0.0, 1.0, 1.0},
        {"translateX", imb::Prop::TranslateX, -4096.0, 4096.0, 0.0},
        {"translateY", imb::Prop::TranslateY, -4096.0, 4096.0, 0.0},
        {"scale", imb::Prop::Scale, 0.0, 3.0, 1.0},
        {"scaleX", imb::Prop::ScaleX, 0.0, 3.0, 1.0},
        {"scaleY", imb::Prop::ScaleY, 0.0, 3.0, 1.0},
        {"rotate", imb::Prop::Rotate, -720.0, 720.0, 0.0},
        {"skewX", imb::Prop::SkewX, -85.0, 85.0, 0.0},
        {"skewY", imb::Prop::SkewY, -85.0, 85.0, 0.0},
        {"tint", imb::Prop::Tint, 0.0, 1.0, 1.0},
        {"reveal", imb::Prop::Reveal, 0.0, 1.0, 1.0},
        {"textReveal", imb::Prop::TextReveal, 0.0, 1.0, 1.0},
        {"glow", imb::Prop::Glow, 0.0, 1.0, 0.0},
        {"shadow", imb::Prop::Shadow, 0.0, 1.0, 0.0},
    };
    return t;
}

inline int PropIndex(imb::Prop p) {
    for (int i = 0; i < kMotionPropCount; ++i)
        if (Props()[i].prop == p) return i;
    return -1;
}

inline int PropIndex(const char* name) {
    for (int i = 0; i < kMotionPropCount; ++i)
        if (name && std::strcmp(Props()[i].name, name) == 0) return i;
    return -1;
}

template <class E> struct NamedValue {
    const char* name;
    E value;
};

template <class E, int N> inline const char* ToName(const NamedValue<E> (&t)[N], E v) {
    for (int i = 0; i < N; ++i)
        if (t[i].value == v) return t[i].name;
    return t[0].name;
}

template <class E, int N> inline bool FromName(const NamedValue<E> (&t)[N], const std::string& s, E* out) {
    for (int i = 0; i < N; ++i) {
        if (s == t[i].name) {
            if (out) *out = t[i].value;
            return true;
        }
    }
    return false;
}

const NamedValue<imb::Direction> kDirections[] = {{"normal", imb::Direction::Normal},
                                                  {"reverse", imb::Direction::Reverse},
                                                  {"alternate", imb::Direction::Alternate},
                                                  {"alternate-reverse", imb::Direction::AlternateReverse}};
const NamedValue<imb::Fill> kFills[] = {{"none", imb::Fill::None},
                                        {"forwards", imb::Fill::Forwards},
                                        {"backwards", imb::Fill::Backwards},
                                        {"both", imb::Fill::Both}};
const NamedValue<imb::RevealDir> kRevealDirs[] = {{"left", imb::RevealDir::Left},   {"right", imb::RevealDir::Right},
                                                  {"up", imb::RevealDir::Up},       {"down", imb::RevealDir::Down},
                                                  {"center", imb::RevealDir::Center}};
const NamedValue<imb::Trigger> kTriggers[] = {{"enter", imb::Trigger::Enter},   {"exit", imb::Trigger::Exit},
                                              {"hover", imb::Trigger::Hover},   {"press", imb::Trigger::Press},
                                              {"focus", imb::Trigger::Focus},   {"loop", imb::Trigger::Loop},
                                              {"change", imb::Trigger::Change}, {"attention", imb::Trigger::Attention}};

// category, trigger used when the json has none
inline const char* const* Categories() {
    static const char* const t[kMotionCategoryCount * 2] = {
        "entrance", "enter", "exit", "exit", "attention", "attention", "hover", "hover", "press", "press",
        "loop", "loop", "text", "enter", "change", "change", "stagger", "enter"};
    return t;
}

inline bool IsNum(const Json& j) { return j.IsNumber() && std::isfinite(j.num); }

inline bool Fail(std::string& msg, const std::string& m) {
    msg = m;
    return false;
}

inline std::string Quote(const std::string& s) { return "\"" + s + "\""; }

inline std::string FloatStr(double v) {
    char buf[40];
    float f = static_cast<float>(v);
    for (int prec = 1; prec <= 9; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, static_cast<double>(f));
        for (char* p = buf; *p; ++p)
            if (*p == ',') *p = '.';
        if (static_cast<float>(json_detail::StrToDoubleC(buf, std::strlen(buf))) == f) break;
    }
    // %g picks 1.7e+02 for 170 at low precision
    if (std::strchr(buf, 'e') && std::fabs(f) >= 1.0f && std::fabs(f) < 1e7f)
        std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(f));
    return buf;
}

// a float as the shortest decimal that reads back to it, so dumps show 0.3 and not 0.30000001192092896
inline Json Num(float f) {
    std::string s = FloatStr(f);
    return Json(json_detail::StrToDoubleC(s.c_str(), s.size()));
}

inline bool HasDuplicateKey(const Json& j, std::string* key) {
    for (size_t i = 0; i < j.obj.size(); ++i) {
        for (size_t k = i + 1; k < j.obj.size(); ++k) {
            if (j.obj[i].first == j.obj[k].first) {
                *key = j.obj[i].first;
                return true;
            }
        }
    }
    return false;
}

inline bool CheckKeys(const Json& j, const char* const* allowed, int n, const std::string& where, std::string& msg) {
    std::string dup;
    if (HasDuplicateKey(j, &dup)) return Fail(msg, where + "duplicate key " + Quote(dup));
    for (const auto& kv : j.obj) {
        bool ok = false;
        for (int i = 0; i < n && !ok; ++i) ok = kv.first == allowed[i];
        if (!ok) return Fail(msg, where + "unknown key " + Quote(kv.first));
    }
    return true;
}

// [-+]?(digits[.digits*] | .digits)([eE][-+]?digits)?
inline bool ScanNumber(const char*& s) {
    const char* p = s;
    if (*p == '-' || *p == '+') ++p;
    bool lead = false, frac = false;
    while (*p >= '0' && *p <= '9') {
        ++p;
        lead = true;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            ++p;
            frac = true;
        }
    }
    if (!lead && !frac) return false;
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        if (*q == '+' || *q == '-') ++q;
        if (*q >= '0' && *q <= '9') {
            while (*q >= '0' && *q <= '9') ++q;
            p = q;
        }
    }
    s = p;
    return true;
}

inline bool ParseHex8(const std::string& s, float v[4]) {
    Vec4 c;
    if (s.size() != 9 || !value_detail::ParseHexColor(s, c)) return false;
    v[0] = c.x;
    v[1] = c.y;
    v[2] = c.z;
    v[3] = c.w;
    return true;
}

inline bool Near(double a, double b) { return std::fabs(a - b) <= 1e-4; }

// rotate counts in full turns: 360 looks the same as 0
inline bool SameValue(int prop, const float* a, const float* b) {
    if (Props()[prop].prop == imb::Prop::Rotate) {
        double turns = (static_cast<double>(b[0]) - a[0]) / 360.0;
        return std::fabs(turns - std::floor(turns + 0.5)) * 360.0 <= 1e-4;
    }
    for (int i = 0; i < 4; ++i)
        if (!Near(a[i], b[i])) return false;
    return true;
}

inline void Identity(int prop, float out[4]) {
    bool tint = Props()[prop].prop == imb::Prop::Tint;
    out[0] = static_cast<float>(Props()[prop].identity);
    out[1] = out[2] = out[3] = tint ? 1.0f : 0.0f;
}

inline std::string ValueStr(int prop, const float v[4]) {
    if (Props()[prop].prop == imb::Prop::Tint) return value_detail::HexColor(Vec4{v[0], v[1], v[2], v[3]});
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v[0]));
    return buf;
}

}  // namespace motion_json_detail

inline const char* MotionPropName(imb::Prop p) {
    int i = motion_json_detail::PropIndex(p);
    return i < 0 ? "opacity" : motion_json_detail::Props()[i].name;
}
inline bool TryMotionPropFromName(const char* name, imb::Prop* out) {
    int i = motion_json_detail::PropIndex(name);
    if (i < 0) return false;
    if (out) *out = motion_json_detail::Props()[i].prop;
    return true;
}
inline const char* MotionDirectionName(imb::Direction d) { return motion_json_detail::ToName(motion_json_detail::kDirections, d); }
inline const char* MotionFillName(imb::Fill f) { return motion_json_detail::ToName(motion_json_detail::kFills, f); }
inline const char* MotionRevealDirName(imb::RevealDir r) { return motion_json_detail::ToName(motion_json_detail::kRevealDirs, r); }
inline const char* MotionTriggerName(imb::Trigger t) { return motion_json_detail::ToName(motion_json_detail::kTriggers, t); }
inline bool TryMotionTriggerFromName(const char* name, imb::Trigger* out) {
    return name && motion_json_detail::FromName(motion_json_detail::kTriggers, name, out);
}
inline const char* MotionCategoryName(int i) {
    return i >= 0 && i < kMotionCategoryCount ? motion_json_detail::Categories()[i * 2] : "";
}
inline int MotionCategoryIndex(const char* name) {
    for (int i = 0; name && i < kMotionCategoryCount; ++i)
        if (std::strcmp(motion_json_detail::Categories()[i * 2], name) == 0) return i;
    return -1;
}

// any Ease name, cubic-bezier(x1,y1,x2,y2), steps(n) or spring(stiffness,damping); *why gets the reason on failure
inline bool ParseEaseSpec(const std::string& text, imb::EaseSpec& out, std::string* why = nullptr) {
    std::string unknown = "unknown ease " + motion_json_detail::Quote(text) +
                          " (use an Ease name, cubic-bezier(x1,y1,x2,y2), steps(n) or spring(stiffness,damping))";
    std::string msg;
    auto fail = [&](const std::string& m) {
        if (why) *why = m;
        return false;
    };
    if (std::strlen(text.c_str()) != text.size()) return fail(unknown);
    imb::EaseSpec e;
    if (TryEaseFromName(text.c_str(), &e.named)) {
        e.kind = imb::EaseKind::Named;
        out = e;
        return true;
    }
    struct Form {
        const char* prefix;
        int nargs;
        imb::EaseKind kind;
    };
    static const Form forms[] = {{"cubic-bezier(", 4, imb::EaseKind::CubicBezier},
                                 {"steps(", 1, imb::EaseKind::Steps},
                                 {"spring(", 2, imb::EaseKind::Spring}};
    for (const Form& f : forms) {
        size_t pl = std::strlen(f.prefix);
        if (text.compare(0, pl, f.prefix) != 0) continue;
        const char* s = text.c_str() + pl;
        double a[4] = {0, 0, 0, 0};
        bool digits_only = true;
        for (int i = 0; i < f.nargs; ++i) {
            while (*s == ' ' || *s == '\t') ++s;
            const char* start = s;
            if (!motion_json_detail::ScanNumber(s)) return fail(unknown);
            for (const char* q = start; q < s; ++q) digits_only = digits_only && *q >= '0' && *q <= '9';
            a[i] = json_detail::StrToDoubleC(start, static_cast<size_t>(s - start));
            while (*s == ' ' || *s == '\t') ++s;
            if (*s != (i + 1 < f.nargs ? ',' : ')')) return fail(unknown);
            ++s;
        }
        if (*s) return fail(unknown);
        std::string q = motion_json_detail::Quote(text);
        if (f.kind == imb::EaseKind::CubicBezier) {
            if (!(a[0] >= 0 && a[0] <= 1 && a[2] >= 0 && a[2] <= 1)) return fail(q + ": cubic-bezier x values must be within 0..1");
            if (!(a[1] >= -5 && a[1] <= 5 && a[3] >= -5 && a[3] <= 5)) return fail(q + ": cubic-bezier y values must be within -5..5");
        } else if (f.kind == imb::EaseKind::Steps) {
            if (!digits_only) return fail(unknown);
            if (!(a[0] >= 1 && a[0] <= 100)) return fail(q + ": steps needs 1..100");
        } else {
            if (!(a[0] > 0 && a[0] <= 10000)) return fail(q + ": spring stiffness must be within (0, 10000]");
            if (!(a[1] > 0 && a[1] <= 1000)) return fail(q + ": spring damping must be within (0, 1000]");
        }
        e.kind = f.kind;
        e.named = imb::Ease::Linear;
        for (int i = 0; i < 4; ++i) e.p[i] = static_cast<float>(a[i]);
        out = e;
        return true;
    }
    return fail(unknown);
}

inline std::string EaseSpecToString(const imb::EaseSpec& e) {
    using motion_json_detail::FloatStr;
    switch (e.kind) {
    case imb::EaseKind::Named: return EaseName(e.named);
    case imb::EaseKind::CubicBezier:
        return "cubic-bezier(" + FloatStr(e.p[0]) + ", " + FloatStr(e.p[1]) + ", " + FloatStr(e.p[2]) + ", " +
               FloatStr(e.p[3]) + ")";
    case imb::EaseKind::Steps: return "steps(" + std::to_string(static_cast<int>(e.p[0])) + ")";
    case imb::EaseKind::Spring: return "spring(" + FloatStr(e.p[0]) + ", " + FloatStr(e.p[1]) + ")";
    }
    return "Linear";
}

namespace motion_json_detail {

inline bool ParseTrack(int prop, const Json& arr, const imb::EaseSpec& def_ease, std::vector<imb::Keyframe>& keys,
                       std::string& msg) {
    const PropInfo& info = Props()[prop];
    std::string where = std::string("tracks.") + info.name;
    if (!arr.IsArray()) return Fail(msg, where + " must be an array of keyframes");
    if (arr.arr.size() < 2) return Fail(msg, where + " needs at least 2 keyframes (t = 0 and t = 1)");
    if (arr.arr.size() > static_cast<size_t>(kMotionMaxKeyframes))
        return Fail(msg, where + " has " + std::to_string(arr.arr.size()) + " keyframes, the limit is " +
                             std::to_string(kMotionMaxKeyframes));
    static const char* const kf_keys[] = {"t", "v", "ease"};
    double prev_t = -1;
    for (size_t i = 0; i < arr.arr.size(); ++i) {
        const Json& kf = arr.arr[i];
        std::string at = where + "[" + std::to_string(i) + "]";
        if (!kf.IsObject()) return Fail(msg, at + " must be an object {t, v, ease}");
        if (!CheckKeys(kf, kf_keys, 3, at + ": ", msg)) return false;
        const Json& t = kf["t"];
        if (!IsNum(t) || t.num < 0 || t.num > 1) return Fail(msg, at + ": t must be a number within 0..1");
        if (i > 0 && t.num <= prev_t) return Fail(msg, at + ": t must increase");
        prev_t = t.num;
        imb::Keyframe k;
        k.t = static_cast<float>(t.num);
        for (int c = 0; c < 4; ++c) k.v[c] = 0.0f;
        const Json& v = kf["v"];
        if (info.prop == imb::Prop::Tint) {
            if (!v.IsString() || !ParseHex8(v.str, k.v)) return Fail(msg, at + ": tint values must be \"#rrggbbaa\" strings");
        } else {
            if (!IsNum(v)) return Fail(msg, at + ": v must be a number");
            if (v.num < info.lo || v.num > info.hi) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), ": %s %g is outside %g..%g", info.name, v.num, info.lo, info.hi);
                return Fail(msg, at + buf);
            }
            k.v[0] = static_cast<float>(v.num);
        }
        k.ease = def_ease;
        if (const Json* e = kf.Find("ease")) {
            std::string why = "must be a string";
            if (!e->IsString() || !ParseEaseSpec(e->str, k.ease, &why)) return Fail(msg, at + ": ease " + why);
        }
        keys.push_back(k);
    }
    if (arr.arr.front()["t"].num != 0.0) return Fail(msg, where + ": the first keyframe must have t = 0");
    if (arr.arr.back()["t"].num != 1.0) return Fail(msg, where + ": the last keyframe must have t = 1");
    return true;
}

inline bool EndUsesLast(imb::Direction d, int iterations) {
    int last = iterations - 1;
    switch (d) {
    case imb::Direction::Normal: return true;
    case imb::Direction::Reverse: return false;
    case imb::Direction::Alternate: return last % 2 == 0;
    case imb::Direction::AlternateReverse: return last % 2 == 1;
    }
    return true;
}

// hover and press play forward while the state is on and back to t = 0 when it's off, so they only
// need to start at identity
inline bool CheckIdentity(const AnimData& d, const std::vector<int>& props, std::string& msg) {
    const imb::AnimDef& def = d.def;
    const std::string& cat = d.category;
    if ((cat == "entrance" || cat == "exit") && def.iterations == 0)
        return Fail(msg, cat + " animations can't repeat forever (iterations = 0)");
    bool start_rule = cat == "exit" || cat == "hover" || cat == "press" || cat == "attention" || cat == "change" || cat == "loop";
    bool end_rule = (cat == "entrance" || cat == "attention" || cat == "change") && def.iterations > 0;
    bool wraps = cat == "loop" && (def.direction == imb::Direction::Normal || def.direction == imb::Direction::Reverse);
    bool forward_start = def.direction == imb::Direction::Normal || def.direction == imb::Direction::Alternate;
    for (size_t i = 0; i < d.keys.size(); ++i) {
        const std::vector<imb::Keyframe>& k = d.keys[i];
        int prop = props[i];
        std::string name = std::string("tracks.") + Props()[prop].name;
        float ident[4];
        Identity(prop, ident);
        const imb::Keyframe& start = forward_start ? k.front() : k.back();
        if (start_rule && !SameValue(prop, ident, start.v))
            return Fail(msg, cat + " animations must start at identity: " + name + " starts at " + ValueStr(prop, start.v) +
                                 ", expected " + ValueStr(prop, ident));
        const imb::Keyframe& end = EndUsesLast(def.direction, def.iterations) ? k.back() : k.front();
        if (end_rule && !SameValue(prop, ident, end.v))
            return Fail(msg, cat + " animations must end at identity: " + name + " ends at " + ValueStr(prop, end.v) +
                                 ", expected " + ValueStr(prop, ident));
        if (wraps && !SameValue(prop, k.front().v, k.back().v))
            return Fail(msg, "loops must end where they start: " + name + " starts at " + ValueStr(prop, k.front().v) + " and ends at " +
                                 ValueStr(prop, k.back().v));
    }
    return true;
}

inline bool ParseAnim(const Json& j, const char* set_category, bool file_keys, AnimStorage& out, std::string& msg) {
    if (!j.IsObject()) return Fail(msg, "animation must be an object");
    static const char* const keys[] = {"name",   "category", "description", "tags",   "credit",
                                       "duration", "delay",  "iterations",  "direction", "fill",
                                       "origin", "revealDir", "trigger",    "ease",   "tracks",
                                       "schemaVersion", "kind"};
    if (!CheckKeys(j, keys, file_keys ? 17 : 15, "", msg)) return false;
    if (const Json* sv = j.Find("schemaVersion"))
        if (!(sv->IsNumber() && sv->num == 1)) return Fail(msg, "schemaVersion must be 1");
    if (const Json* kind = j.Find("kind"))
        if (!(kind->IsString() && kind->str == "animation")) return Fail(msg, "kind must be \"animation\"");

    AnimStorage tmp;
    AnimData& d = *tmp.data;
    imb::AnimDef& def = d.def;
    const Json& name = j["name"];
    bool name_ok = name.IsString() && !name.str.empty() && name.str[0] >= 'A' && name.str[0] <= 'Z';
    for (size_t i = 0; name_ok && i < name.str.size(); ++i) {
        char c = name.str[i];
        name_ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    }
    if (!name_ok) return Fail(msg, "name must be PascalCase (letters and digits, starting with A-Z)");
    if (name.str.size() > 48) return Fail(msg, "name is longer than 48 characters");
    static const char* const reserved[] = {"RegisterAll", "Names", "None", "True", "False", "Bool", "Status", "Success", "Always"};
    for (const char* r : reserved)
        if (name.str == r) return Fail(msg, "name " + Quote(name.str) + " is reserved");
    d.name = name.str;

    const Json* cat = j.Find("category");
    if (!cat && !set_category) return Fail(msg, "category is missing");
    std::string category = cat ? cat->AsString() : std::string(set_category);
    int ci = (cat && !cat->IsString()) ? -1 : MotionCategoryIndex(category.c_str());
    if (ci < 0) return Fail(msg, "unknown category " + Quote(category));
    if (set_category && category != set_category)
        return Fail(msg, "category " + Quote(category) + " does not match the set category " + Quote(set_category));
    d.category = category;

    for (const char* key : {"description", "credit"}) {
        const Json* s = j.Find(key);
        if (s && !s->IsString()) return Fail(msg, std::string(key) + " must be a string");
    }
    d.description = j["description"].AsString();
    d.credit = j["credit"].AsString();
    if (const Json* tags = j.Find("tags")) {
        if (!tags->IsArray()) return Fail(msg, "tags must be an array of non-empty strings");
        for (const Json& t : tags->arr) {
            if (!t.IsString() || t.str.empty()) return Fail(msg, "tags must be an array of non-empty strings");
            if (std::find(d.tags.begin(), d.tags.end(), t.str) != d.tags.end()) return Fail(msg, "tags has duplicates");
            d.tags.push_back(t.str);
        }
    }

    const Json& dur = j["duration"];
    if (!IsNum(dur) || !(dur.num > 0 && dur.num <= 60)) return Fail(msg, "duration must be a number of seconds within (0, 60]");
    def.duration = static_cast<float>(dur.num);
    def.delay = 0.0f;
    if (const Json* delay = j.Find("delay")) {
        if (!IsNum(*delay) || delay->num < 0 || delay->num > 60) return Fail(msg, "delay must be a number of seconds within 0..60");
        def.delay = static_cast<float>(delay->num);
    }
    def.iterations = category == "loop" ? 0 : 1;
    if (const Json* it = j.Find("iterations")) {
        if (!IsNum(*it) || it->num != std::floor(it->num) || it->num < 0 || it->num > 1000)
            return Fail(msg, "iterations must be an integer within 0..1000 (0 repeats forever)");
        def.iterations = static_cast<int>(it->num);
    }

    def.direction = imb::Direction::Normal;
    def.fill = imb::Fill::Both;
    def.reveal_dir = imb::RevealDir::Left;
    FromName(kTriggers, Categories()[ci * 2 + 1], &def.default_trigger);
    const Json* e = j.Find("direction");
    if (e && !(e->IsString() && FromName(kDirections, e->str, &def.direction)))
        return Fail(msg, "direction must be one of normal, reverse, alternate, alternate-reverse");
    e = j.Find("fill");
    if (e && !(e->IsString() && FromName(kFills, e->str, &def.fill))) return Fail(msg, "fill must be one of none, forwards, backwards, both");
    e = j.Find("revealDir");
    if (e && !(e->IsString() && FromName(kRevealDirs, e->str, &def.reveal_dir)))
        return Fail(msg, "revealDir must be one of left, right, up, down, center");
    e = j.Find("trigger");
    if (e && !(e->IsString() && FromName(kTriggers, e->str, &def.default_trigger)))
        return Fail(msg, "trigger must be one of enter, exit, hover, press, focus, loop, change, attention");

    def.origin = ImVec2(0.5f, 0.5f);
    if (const Json* o = j.Find("origin")) {
        static const char* const xy[] = {"x", "y"};
        std::string ignored;
        bool ok = o->IsObject() && o->obj.size() == 2 && CheckKeys(*o, xy, 2, "", ignored);
        ok = ok && IsNum((*o)["x"]) && IsNum((*o)["y"]);
        ok = ok && (*o)["x"].num >= -1 && (*o)["x"].num <= 2 && (*o)["y"].num >= -1 && (*o)["y"].num <= 2;
        if (!ok) return Fail(msg, "origin must be {\"x\": n, \"y\": n} with fractions within -1..2");
        def.origin = ImVec2(static_cast<float>((*o)["x"].num), static_cast<float>((*o)["y"].num));
    }

    imb::EaseSpec def_ease;
    if (const Json* ease = j.Find("ease")) {
        std::string why = "must be a string";
        if (!ease->IsString() || !ParseEaseSpec(ease->str, def_ease, &why)) return Fail(msg, "ease " + why);
    }

    const Json& tracks = j["tracks"];
    if (!tracks.IsObject() || tracks.obj.empty()) return Fail(msg, "tracks must be an object with at least one track");
    std::string dup;
    if (HasDuplicateKey(tracks, &dup)) return Fail(msg, "tracks: duplicate track " + Quote(dup));
    std::vector<std::pair<int, const Json*>> order;
    for (const auto& kv : tracks.obj) {
        int p = PropIndex(kv.first.c_str());
        if (p < 0) return Fail(msg, "unknown track " + Quote(kv.first));
        order.push_back(std::make_pair(p, &kv.second));
    }
    std::sort(order.begin(), order.end(),
              [](const std::pair<int, const Json*>& a, const std::pair<int, const Json*>& b) { return a.first < b.first; });
    std::vector<int> props;
    for (const auto& pt : order) {
        std::vector<imb::Keyframe> k;
        if (!ParseTrack(pt.first, *pt.second, def_ease, k, msg)) return false;
        imb::Track tr{};
        tr.prop = Props()[pt.first].prop;
        d.tracks.push_back(tr);
        d.keys.push_back(std::move(k));
        props.push_back(pt.first);
    }
    if (!CheckIdentity(d, props, msg)) return false;
    tmp.Relink();
    out = std::move(tmp);
    return true;
}

inline void SetError(Error* err, const Json& j, const std::string& msg) {
    if (!err) return;
    const Json& name = j["name"];
    err->message = name.IsString() && !name.str.empty() ? "animation " + Quote(name.str) + ": " + msg : "animation: " + msg;
}

}  // namespace motion_json_detail

// One AnimJson, either inside a set or a standalone file (then "schemaVersion" and "kind" may be present).
// On failure out is left untouched.
inline bool AnimFromJson(const Json& j, AnimStorage& out, Error* err = nullptr) {
    std::string msg;
    if (motion_json_detail::ParseAnim(j, nullptr, true, out, msg)) return true;
    motion_json_detail::SetError(err, j, msg);
    return false;
}

// {"schemaVersion":1,"kind":"animationSet","category":"...","animations":[...]}. The envelope fields are
// optional so the live protocol payload {"animations":[...]} reads too; when "category" is there every
// animation must match it.
inline bool AnimSetFromJson(const Json& j, std::vector<AnimStorage>& out, Error* err = nullptr) {
    using namespace motion_json_detail;
    std::string msg;
    auto fail = [&](const std::string& m) {
        if (err) err->message = m;
        return false;
    };
    if (!j.IsObject()) return fail("animation set must be an object");
    static const char* const keys[] = {"schemaVersion", "kind", "category", "animations"};
    if (!CheckKeys(j, keys, 4, "animation set: ", msg)) return fail(msg);
    if (const Json* sv = j.Find("schemaVersion"))
        if (!(sv->IsNumber() && sv->num == 1)) return fail("animation set: schemaVersion must be 1");
    if (const Json* kind = j.Find("kind"))
        if (!(kind->IsString() && kind->str == "animationSet")) return fail("animation set: kind must be \"animationSet\"");
    const Json* cat = j.Find("category");
    if (cat && !(cat->IsString() && MotionCategoryIndex(cat->str.c_str()) >= 0))
        return fail("animation set: unknown category " + Quote(cat->AsString()));
    const Json& items = j["animations"];
    if (!items.IsArray()) return fail("animation set: animations must be an array");
    if (items.arr.size() > static_cast<size_t>(kMotionMaxAnimations))
        return fail("animation set: more than " + std::to_string(kMotionMaxAnimations) + " animations");
    std::vector<AnimStorage> list;
    list.reserve(items.arr.size());
    for (size_t i = 0; i < items.arr.size(); ++i) {
        AnimStorage s;
        if (!ParseAnim(items.arr[i], cat ? cat->str.c_str() : nullptr, false, s, msg)) {
            const Json& name = items.arr[i]["name"];
            return fail((name.IsString() && !name.str.empty() ? "animation " + Quote(name.str)
                                                               : "animations[" + std::to_string(i) + "]") + ": " + msg);
        }
        for (const AnimStorage& prev : list)
            if (prev.data->name == s.data->name) return fail("animation " + Quote(s.data->name) + ": duplicate name in this set");
        list.push_back(std::move(s));
    }
    out = std::move(list);
    return true;
}

// Keyframes that share the most common ease get it from a top-level "ease", the rest spell theirs out,
// so AnimFromJson(AnimToJson(def)) gives the same def back.
inline Json AnimToJson(const imb::AnimDef& def) {
    using namespace motion_json_detail;
    Json o = Json::Object();
    o.Set("name", Json(def.name ? def.name : ""));
    o.Set("category", Json(def.category ? def.category : ""));
    o.Set("duration", Num(def.duration));
    o.Set("delay", Num(def.delay));
    o.Set("iterations", Json(def.iterations));
    o.Set("direction", Json(MotionDirectionName(def.direction)));
    o.Set("fill", Json(MotionFillName(def.fill)));
    Json origin = Json::Object();
    origin.Set("x", Num(def.origin.x));
    origin.Set("y", Num(def.origin.y));
    o.Set("origin", std::move(origin));
    o.Set("revealDir", Json(MotionRevealDirName(def.reveal_dir)));
    o.Set("trigger", Json(MotionTriggerName(def.default_trigger)));

    std::vector<std::pair<std::string, int>> counts;
    for (int t = 0; t < def.track_count && def.tracks; ++t) {
        const imb::Track& tr = def.tracks[t];
        for (int k = 0; k + 1 < tr.count && tr.keys; ++k) {
            std::string s = EaseSpecToString(tr.keys[k].ease);
            auto it = std::find_if(counts.begin(), counts.end(), [&](const std::pair<std::string, int>& c) { return c.first == s; });
            if (it == counts.end()) counts.push_back(std::make_pair(s, 1));
            else ++it->second;
        }
    }
    std::string common = "Linear";
    int best = 0;
    for (const auto& c : counts) {
        if (c.second > best) {
            best = c.second;
            common = c.first;
        }
    }
    if (common != "Linear") o.Set("ease", Json(common));

    Json tracks = Json::Object();
    for (int t = 0; t < def.track_count && def.tracks; ++t) {
        const imb::Track& tr = def.tracks[t];
        bool tint = tr.prop == imb::Prop::Tint;
        Json arr = Json::Array();
        for (int k = 0; k < tr.count && tr.keys; ++k) {
            const imb::Keyframe& kf = tr.keys[k];
            Json key = Json::Object();
            key.Set("t", Num(kf.t));
            key.Set("v", tint ? Json(value_detail::HexColor(Vec4{kf.v[0], kf.v[1], kf.v[2], kf.v[3]})) : Num(kf.v[0]));
            std::string s = EaseSpecToString(kf.ease);
            if (s != common) key.Set("ease", Json(s));
            arr.Push(std::move(key));
        }
        tracks.Set(MotionPropName(tr.prop), std::move(arr));
    }
    o.Set("tracks", std::move(tracks));
    return o;
}

// adds description, tags and credit; as_file also adds "schemaVersion" and "kind" for a .imanim.json
inline Json AnimToJson(const AnimStorage& s, bool as_file = false) {
    if (!s.Def()) return Json::Object();
    Json o = AnimToJson(*s.Def());
    const AnimData& d = *s.data;
    if (!d.description.empty()) o.Set("description", Json(d.description));
    if (!d.credit.empty()) o.Set("credit", Json(d.credit));
    if (!d.tags.empty()) {
        Json tags = Json::Array();
        for (const std::string& t : d.tags) tags.Push(Json(t));
        o.Set("tags", std::move(tags));
    }
    if (as_file) {
        o.Set("schemaVersion", Json(1));
        o.Set("kind", Json("animation"));
    }
    return o;
}

}  // namespace imforge
