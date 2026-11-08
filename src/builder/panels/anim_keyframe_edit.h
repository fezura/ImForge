#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imforge/imforge_json.h"

// editable model of one AnimJson document (presets/animations, project animations/*.imanim.json).
// no ImGui in here so the tests can poke at it directly
namespace imforge::builder::kf {

const float kSnapStep = 0.05f;
const float kMinKeyGap = 0.005f;
const int kMaxKeysPerTrack = 64;

struct PropInfo {
    const char* key;
    const char* label;
    float identity;
    float min, max, speed;
    const char* format;
    bool color;
};

int CategoryCount();
const char* CategoryAt(int i);
const char* CategoryTrigger(const std::string& category);

int PropCount();
const PropInfo& PropAt(int i);
const PropInfo* FindProp(const std::string& key);

struct Key {
    float t = 0.0f;
    float v[4] = {0, 0, 0, 0};
    // empty = the animation's default ease
    std::string ease;
};

struct TrackEdit {
    std::string prop;
    std::vector<Key> keys;
};

struct AnimEdit {
    std::string name, category = "entrance", description, credit;
    std::vector<std::string> tags;
    float duration = 0.5f, delay = 0.0f;
    int iterations = 1;
    std::string direction = "normal", fill = "both", reveal_dir = "left", trigger = "enter", ease = "Linear";
    float origin_x = 0.5f, origin_y = 0.5f;
    std::vector<TrackEdit> tracks;
};

bool AnimEditFromJson(const Json& j, AnimEdit& out, std::string* err = nullptr);
Json AnimEditToJson(const AnimEdit& a);
// a fresh custom animation: fade + small rise, ready to tweak
AnimEdit MakeDefaultAnim(const std::string& name);

// ease strings: any imb::Ease name, cubic-bezier(a,b,c,d), steps(n), spring(stiffness,damping)
enum class EaseType { Named, CubicBezier, Steps, Spring };
struct EaseParsed {
    EaseType type = EaseType::Named;
    std::string name = "Linear";
    float p[4] = {0, 0, 0, 0};
};
bool ParseEase(const std::string& s, EaseParsed& out);
std::string FormatEase(const EaseParsed& e);
bool IsValidEase(const std::string& s);

float SnapTime(float t, bool free);
// keeps the endpoints at 0 and 1 and the keys strictly sorted; returns the clamped t
float RetimeKey(TrackEdit& tr, int index, float new_t, bool free);
// returns the index of the new (or already existing) key at t, -1 when the track is full.
// value: nullptr samples the track at t through `sample` (linear when sample is empty)
using EaseFn = std::function<float(const std::string& ease, float t)>;
int InsertKey(TrackEdit& tr, float t, const float* value, const std::string& default_ease, const EaseFn& sample = EaseFn());
// endpoints can't go, a track keeps at least two keys
bool CanDeleteKey(const TrackEdit& tr, int index);
bool DeleteKey(TrackEdit& tr, int index);
// sorts, clamps to 0..1, drops duplicates and pins the first/last key to 0/1
void NormalizeTrack(TrackEdit& tr);
TrackEdit MakeTrack(const std::string& prop);
void SampleTrack(const TrackEdit& tr, float t, const std::string& default_ease, const EaseFn& sample, float out[4]);
bool IsSortedWithEndpoints(const TrackEdit& tr);

// PascalCase, letters and digits, at most 48 chars, not a reserved word (the loader's rules)
bool IsValidAnimName(const std::string& name);
// valid name derived from base that isn't in taken
std::string UniqueAnimName(const std::string& base, const std::vector<std::string>& taken);

}  // namespace imforge::builder::kf
