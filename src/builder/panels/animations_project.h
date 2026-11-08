#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imb {
struct AnimDef;
}

namespace imforge::builder {

struct EditorContext;

// custom animations stored in the project (Project::animations, animations/<Name>.imanim.json)
namespace anims {

std::vector<std::string> CustomNames(const Project& p);
int IndexOf(const Project& p, const std::string& name);
// shipped preset from imb_motion_presets.h (not a project animation)
bool IsPresetName(const std::string& name);
std::vector<std::string> PresetNames();
std::vector<std::string> TakenNames(const Project& p);
// widgets in panels and components with a motion that uses this custom animation
int CountUses(const Project& p, const std::string& name);

// each one is a single undo step and goes out to the live client with the project
std::string Create(EditorContext& ctx, const std::string& base = "MyAnimation");
std::string Duplicate(EditorContext& ctx, const std::string& name);
std::string DuplicatePreset(EditorContext& ctx, const std::string& preset);
bool Rename(EditorContext& ctx, const std::string& from, const std::string& to, std::string* err);
// also drops the motions that used it
bool Remove(EditorContext& ctx, const std::string& name);
bool Replace(EditorContext& ctx, const std::string& name, const Json& anim, const char* undo_label);

// which animation the Keyframes tab edits. RequestEdit also brings up the Anim Timeline on that tab
void RequestEdit(EditorContext& ctx, const std::string& name);
const std::string& EditTarget();
void SetEditTarget(const std::string& name);
bool TakeTabRequest();

// built through AnimStorage and cached on the JSON content; nullptr (with *err) when it doesn't load
const imb::AnimDef* DefFromJson(const Json& anim, std::string* err = nullptr);
const imb::AnimDef* CustomDef(const Project& p, const std::string& name, std::string* err = nullptr);
// preset or custom, whichever the motion names
const imb::AnimDef* MotionDef(const Project& p, const MotionSpec& m);

}  // namespace anims

}  // namespace imforge::builder
