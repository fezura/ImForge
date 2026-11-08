#include "anim_keyframe_editor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "imforge/imb_motion.h"
#include "imforge/imforge_motion_json.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/icon_table.h"
#include "../ui/motion_preview.h"
#include "../ui/ui_helpers.h"
#include "animations_project.h"

namespace imforge::builder::kfui {

namespace {

const char* const kDirections[] = {"normal", "reverse", "alternate", "alternate-reverse"};
const char* const kFills[] = {"none", "forwards", "backwards", "both"};
const char* const kRevealDirs[] = {"left", "right", "up", "down", "center"};
const char* const kTriggers[] = {"enter", "exit", "hover", "press", "focus", "loop", "change", "attention"};
const float kEndHold = 0.35f;
const int kInfinitePreviewCycles = 3;
// the editor can preview an animation the loader rejects (an entrance that doesn't end at rest yet),
// it loads a copy under this name with the identity rules off
const char* const kLenientName = "KeyframeEditorPreview";

bool ComboStr(const char* id, std::string& v, const char* const* items, int n) {
    bool changed = false;
    if (ImGui::BeginCombo(id, v.c_str())) {
        for (int i = 0; i < n; ++i) {
            bool sel = v == items[i];
            if (ImGui::Selectable(items[i], sel) && !sel) {
                v = items[i];
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void SyncFromProject(EditorContext& ctx) {
    EditorState& s = State();
    std::string target = anims::EditTarget();
    if (target.empty() || anims::IndexOf(ctx.project, target) < 0) {
        std::vector<std::string> names = anims::CustomNames(ctx.project);
        target = names.empty() ? std::string() : names.front();
        anims::SetEditTarget(target);
    }
    if (target != s.name) {
        s.name = target;
        s.source = Json();
        s.track = s.key = 0;
        s.progress = s.time = 0.0f;
        s.rename_error.clear();
        s.playing = true;
    }
    const Json* j = ctx.project.FindAnimation(s.name);
    if (!j) return;
    if (s.source.IsNull() || !(s.source == *j)) {
        s.source = *j;
        kf::AnimEdit fresh;
        std::string err;
        if (kf::AnimEditFromJson(*j, fresh, &err)) {
            s.edit = std::move(fresh);
            s.load_error.clear();
        } else {
            s.load_error = err;
        }
        s.track = std::max(0, std::min(s.track, (int)s.edit.tracks.size() - 1));
        if (const kf::TrackEdit* tr = SelectedTrack()) s.key = std::max(0, std::min(s.key, (int)tr->keys.size() - 1));
        else s.key = 0;
        if (!s.dragging) s.rename_buf = s.edit.name;
    }
}

const imb::AnimDef* PreviewDef(std::string* strict_error) {
    EditorState& s = State();
    const imb::AnimDef* def = anims::DefFromJson(s.source, strict_error);
    if (def) return def;
    Json lenient = s.source;
    lenient.Set("name", Json(kLenientName));
    lenient.Set("category", Json("attention"));
    return anims::DefFromJson(lenient, nullptr);
}

float CycleTotal(const imb::AnimDef& def) {
    int iters = def.iterations > 0 ? def.iterations : kInfinitePreviewCycles;
    return def.delay + def.duration * (float)iters;
}

void AdvancePlayhead(const imb::AnimDef* def) {
    EditorState& s = State();
    float dt = ImGui::GetIO().DeltaTime;
    if (dt > 0.1f) dt = 0.1f;
    if (!s.playing || !def || def->duration <= 0.0f) return;
    if (s.hold > 0.0f) {
        s.hold -= dt;
        if (s.hold <= 0.0f) {
            s.hold = 0.0f;
            s.time = 0.0f;
            s.progress = 0.0f;
        }
        return;
    }
    if (s.full_timing) {
        imb::detail::MotionTiming m = imb::detail::DefTiming(*def, 1.0f);
        if (m.iters == 0) m.iters = kInfinitePreviewCycles;
        float total = CycleTotal(*def);
        s.time += dt;
        if (s.time >= total) {
            s.time = total;
            if (s.loop) s.hold = kEndHold;
            else s.playing = false;
        }
        float p = 0.0f;
        if (imb::detail::TimingProgress(m, s.time, &p)) s.progress = p;
        return;
    }
    s.progress += dt / def->duration;
    if (s.progress >= 1.0f) {
        s.progress = 1.0f;
        if (s.loop) s.hold = kEndHold;
        else s.playing = false;
    }
}

bool OriginPicker(float& ox, float& oy) {
    const BuilderColors& c = Colors();
    float cell = std::floor(ImGui::GetFrameHeight() * 0.62f), gap = 2.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    for (int r = 0; r < 3; ++r) {
        for (int col = 0; col < 3; ++col) {
            ImVec2 mn(p.x + (cell + gap) * (float)col, p.y + (cell + gap) * (float)r);
            ImGui::SetCursorScreenPos(mn);
            ImGui::PushID(r * 3 + col);
            float fx = (float)col * 0.5f, fy = (float)r * 0.5f;
            if (ImGui::InvisibleButton("##o", ImVec2(cell, cell))) {
                ox = fx;
                oy = fy;
                changed = true;
            }
            bool on = std::fabs(ox - fx) < 1e-3f && std::fabs(oy - fy) < 1e-3f;
            bool hov = ImGui::IsItemHovered();
            dl->AddRectFilled(mn, ImVec2(mn.x + cell, mn.y + cell), ToU32(on ? c.accent : (hov ? c.bg_hover : c.bg_frame)), 2.0f);
            if (hov) ImGui::SetTooltip("origin %.1f, %.1f", (double)fx, (double)fy);
            ImGui::PopID();
        }
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(cell * 3.0f + gap * 2.0f, cell * 3.0f + gap * 2.0f));
    return changed;
}

void DrawSettings(EditorContext& ctx) {
    EditorState& s = State();
    kf::AnimEdit& a = s.edit;
    const float fs = ImGui::GetFontSize();
    if (!ui::BeginPropertyTable("kf_settings", fs * 5.6f)) return;

    ui::PropertyLabel("Name");
    if (ui::InputTextStd("##name", s.rename_buf, ImGuiInputTextFlags_AutoSelectAll)) s.rename_error.clear();
    if (ImGui::IsItemDeactivatedAfterEdit() && s.rename_buf != a.name) {
        std::string err;
        std::string from = a.name;
        if (anims::Rename(ctx, from, s.rename_buf, &err)) {
            s.rename_error.clear();
            s.name = s.rename_buf;
        } else {
            s.rename_error = err;
            s.rename_buf = from;
        }
    }
    if (!s.rename_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
        ImGui::TextWrapped("%s", s.rename_error.c_str());
        ImGui::PopStyleColor();
    }

    ui::PropertyLabel("Category");
    {
        std::string cat = a.category;
        std::vector<const char*> cats;
        for (int i = 0; i < kf::CategoryCount(); ++i) cats.push_back(kf::CategoryAt(i));
        if (ComboStr("##cat", cat, cats.data(), (int)cats.size())) {
            if (a.trigger == kf::CategoryTrigger(a.category)) a.trigger = kf::CategoryTrigger(cat);
            if (cat == "loop" && a.category != "loop" && a.iterations == 1) a.iterations = 0;
            if (cat != "loop" && a.category == "loop" && a.iterations == 0) a.iterations = 1;
            a.category = cat;
            Commit(ctx, "Edit animation");
        }
    }
    ui::PropertyLabel("Description");
    if (ui::InputTextStd("##desc", a.description)) Commit(ctx, "Edit animation");
    ui::PropertyLabel("Duration");
    if (ImGui::DragFloat("##dur", &a.duration, 0.005f, 0.01f, 60.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) Commit(ctx, "Edit animation");
    ui::PropertyLabel("Delay");
    if (ImGui::DragFloat("##delay", &a.delay, 0.005f, 0.0f, 60.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) Commit(ctx, "Edit animation");
    ui::PropertyLabel("Iterations", "0 repeats forever. Entrance and exit animations play a fixed number of times.");
    if (ImGui::DragInt("##iters", &a.iterations, 0.08f, 0, 1000, a.iterations == 0 ? "infinite" : "%d x", ImGuiSliderFlags_AlwaysClamp))
        Commit(ctx, "Edit animation");
    ui::PropertyLabel("Direction");
    if (ComboStr("##dir", a.direction, kDirections, 4)) Commit(ctx, "Edit animation");
    ui::PropertyLabel("Fill", "What it shows before it starts (backwards) and after it ends (forwards).");
    if (ComboStr("##fill", a.fill, kFills, 4)) Commit(ctx, "Edit animation");
    ui::PropertyLabel("Origin", "Pivot for scale, rotate and skew, as a fraction of the widget rect.");
    {
        if (OriginPicker(a.origin_x, a.origin_y)) Commit(ctx, "Edit animation");
        ImGui::SameLine();
        float o[2] = {a.origin_x, a.origin_y};
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragFloat2("##origin", o, 0.01f, -1.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
            a.origin_x = o[0];
            a.origin_y = o[1];
            Commit(ctx, "Edit animation");
        }
    }
    ui::PropertyLabel("Reveal from", "Edge the Reveal track grows from.");
    if (ComboStr("##reveal", a.reveal_dir, kRevealDirs, 5)) Commit(ctx, "Edit animation");
    ui::PropertyLabel("Trigger", "Trigger a new motion gets when you pick this animation.");
    if (ComboStr("##trigger", a.trigger, kTriggers, 8)) Commit(ctx, "Edit animation");
    ui::PropertyLabel("Ease", "Used by every segment that doesn't pick its own.");
    if (EaseField("default_ease", a.ease, nullptr, ImGui::GetContentRegionAvail().x)) Commit(ctx, "Edit easing");
    ui::PropertyLabel("Tags");
    {
        std::string tags;
        for (const std::string& t : a.tags) tags += (tags.empty() ? "" : ", ") + t;
        if (ui::InputTextWithHintStd("##tags", "comma separated", tags, ImGuiInputTextFlags_EnterReturnsTrue) ||
            ImGui::IsItemDeactivatedAfterEdit()) {
            std::vector<std::string> out;
            std::string cur;
            for (size_t i = 0; i <= tags.size(); ++i) {
                char ch = i < tags.size() ? tags[i] : ',';
                if (ch == ',') {
                    size_t b = cur.find_first_not_of(' '), e = cur.find_last_not_of(' ');
                    std::string t = b == std::string::npos ? std::string() : cur.substr(b, e - b + 1);
                    if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
                    cur.clear();
                } else {
                    cur += ch;
                }
            }
            if (out != a.tags) {
                a.tags = out;
                Commit(ctx, "Edit animation");
            }
        }
    }
    ui::EndPropertyTable();
}

void DrawPresetPicker(EditorContext& ctx) {
    EditorState& s = State();
    float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(fs * 18.0f, fs * 22.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopup("##from_preset")) return;
    ui::TextDim("Copy a built-in preset into the project");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::InputTextWithHintStd("##psearch", "Search presets", s.preset_search);
    std::string needle = Lower(s.preset_search);
    if (ImGui::BeginChild("##plist")) {
        for (const std::string& n : anims::PresetNames()) {
            const imb::AnimDef* d = imb::FindAnim(n.c_str());
            const char* cat = d && d->category ? d->category : "";
            if (!needle.empty() && Lower(n).find(needle) == std::string::npos && Lower(cat).find(needle) == std::string::npos)
                continue;
            char label[128];
            std::snprintf(label, sizeof(label), "%s##%s", n.c_str(), n.c_str());
            if (ImGui::Selectable(label)) {
                std::string made = anims::DuplicatePreset(ctx, n);
                if (!made.empty()) anims::SetEditTarget(made);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(fs * 11.0f);
            ui::TextDim("%s", cat);
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void DrawToolbar(EditorContext& ctx) {
    EditorState& s = State();
    const float fs = ImGui::GetFontSize();
    std::vector<std::string> names = anims::CustomNames(ctx.project);
    ImGui::SetNextItemWidth(fs * 12.0f);
    if (ImGui::BeginCombo("##anim", s.name.empty() ? "no custom animations" : s.name.c_str())) {
        for (const std::string& n : names)
            if (ImGui::Selectable(n.c_str(), n == s.name)) anims::SetEditTarget(n);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) anims::Create(ctx);
    ui::Tooltip("New custom animation (a fade with a small rise to start from)");
    ImGui::SameLine();
    if (ImGui::Button("From preset")) {
        s.preset_search.clear();
        ImGui::OpenPopup("##from_preset");
    }
    ui::Tooltip("Copy one of the built-in presets into the project and edit it here");
    DrawPresetPicker(ctx);
    bool has = !s.name.empty();
    if (!has) ImGui::BeginDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) anims::Duplicate(ctx, s.name);
    ImGui::SameLine();
    if (ImGui::Button("Delete")) ImGui::OpenPopup("##kf_delete");
    if (!has) ImGui::EndDisabled();
    if (ImGui::BeginPopup("##kf_delete")) {
        int uses = anims::CountUses(ctx.project, s.name);
        ImGui::Text("Delete %s?", s.name.c_str());
        if (uses > 0) ui::TextDim("%d widget%s use it, those motions go too.", uses, uses == 1 ? "" : "s");
        else ui::TextDim("No widget uses it.");
        if (ImGui::Button("Delete")) {
            anims::Remove(ctx, s.name);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!has) return;
    int uses = anims::CountUses(ctx.project, s.name);
    ImGui::SameLine();
    ui::TextDim(uses ? "used by %d widget%s" : "not used yet", uses, uses == 1 ? "" : "s");
}

void DrawTransport(const imb::AnimDef* def) {
    EditorState& s = State();
    const float fs = ImGui::GetFontSize();
    if (s.playing) {
        if (ImGui::Button("Pause", ImVec2(fs * 4.0f, 0))) s.playing = false;
    } else if (ui::AccentButton("Play", ImVec2(fs * 4.0f, 0))) {
        if (s.progress >= 1.0f || (s.full_timing && def && s.time >= CycleTotal(*def))) s.progress = s.time = 0.0f;
        s.playing = true;
        s.hold = 0.0f;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &s.loop);
    ImGui::SameLine();
    if (ImGui::Checkbox("Full timing", &s.full_timing)) s.time = s.edit.delay + s.progress * s.edit.duration;
    ui::Tooltip("Play delay, iterations and direction too, instead of one pass over the keyframes");
    ImGui::SameLine();
    float dur = s.edit.duration > 0 ? s.edit.duration : 1.0f;
    float shown = s.progress * dur;
    ImGui::SetNextItemWidth(std::max(fs * 8.0f, ImGui::GetContentRegionAvail().x - fs * 7.0f));
    if (ImGui::SliderFloat("##scrub", &shown, 0.0f, dur, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
        s.progress = shown / dur;
        s.time = s.edit.delay + shown;
        s.playing = false;
        s.hold = 0.0f;
    }
    ImGui::SameLine();
    ui::TextDim("t %.3f", (double)s.progress);
}

void DrawPreviewColumn(const imb::AnimDef* def, const std::string& strict_error) {
    EditorState& s = State();
    const BuilderColors& c = Colors();
    float w = ImGui::GetContentRegionAvail().x;
    float h = std::max(60.0f, std::min(w * 0.8f, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 3.0f));
    ImVec2 mn = ImGui::GetCursorScreenPos(), mx(mn.x + w, mn.y + h);
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(mn, mx, ToU32(c.bg_window), 6.0f);
    dl->AddRect(mn, mx, ToU32(c.border), 6.0f);
    if (def) {
        dl->PushClipRect(mn, mx, true);
        DrawScrubPreview(*def, s.progress, mn, mx);
        dl->PopClipRect();
    } else {
        ui::TextDim("Nothing to preview.");
    }
    if (def) {
        imb::Pose p = imb::detail::SampleProgress(*def, s.progress);
        ui::TextDim("opacity %.2f  scale %.2f x %.2f", (double)p.opacity, (double)p.scale_x, (double)p.scale_y);
        ui::TextDim("move %.0f, %.0f  rotate %.0f", (double)p.translate.x, (double)p.translate.y, (double)p.rotate);
    }
    if (!strict_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, c.warning);
        ImGui::TextWrapped("Widgets can't play it yet: %s", strict_error.c_str());
        ImGui::PopStyleColor();
    }
}

void DrawEmpty(EditorContext& ctx) {
    const float fs = ImGui::GetFontSize();
    ui::EmptyState(IconUtf8("fa-wand-magic-sparkles"), "No custom animations",
                   "Make your own keyframe animation, or copy a built-in preset and tweak it.",
                   ImGui::GetFrameHeight() + fs);
    ui::CenteredRowStart(ui::ButtonWidth("New animation") + ui::ButtonWidth("Start from a preset") + ImGui::GetStyle().ItemSpacing.x);
    if (ui::AccentButton("New animation")) anims::Create(ctx);
    ImGui::SameLine();
    if (ImGui::Button("Start from a preset")) {
        State().preset_search.clear();
        ImGui::OpenPopup("##from_preset");
    }
    DrawPresetPicker(ctx);
}

}  // namespace

EditorState& State() {
    static EditorState s;
    return s;
}

kf::TrackEdit* SelectedTrack() {
    EditorState& s = State();
    if (s.track < 0 || s.track >= (int)s.edit.tracks.size()) return nullptr;
    return &s.edit.tracks[(size_t)s.track];
}

void Commit(EditorContext& ctx, const char* label) {
    EditorState& s = State();
    if (s.name.empty()) return;
    s.edit.name = s.name;
    Json j = kf::AnimEditToJson(s.edit);
    if (anims::Replace(ctx, s.name, j, label)) s.source = j;
}

void DrawScrubPreview(const imb::AnimDef& def, float progress, const ImVec2& min, const ImVec2& max) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    MotionPreviewKind kind = MotionPreviewKindOf(def);
    if (kind != MotionPreviewKind::Text) kind = MotionPreviewKind::OneShot;
    MotionPreviewStyle style;
    style.size = 0.46f;
    ImVec2 emn, emx;
    MotionSampleRect(min, max, style, kind, emn, emx);
    const float rounding = std::floor((emx.y - emn.y) * 0.24f);
    dl->AddRect(emn, emx, ToU32(Colors().text_disabled, 0.45f), rounding, 0, 1.0f);

    imb::Pose pose = imb::detail::SampleProgress(def, progress);
    const float dpi = ImGui::GetFontSize() / 13.0f;
    if (pose.shadow > 0.0f) {
        float k = imb::detail::Clamp01(pose.shadow);
        imb::Shadow(dl, emn, emx, rounding, imb::detail::WithAlpha(IM_COL32(0, 0, 0, 110), k), 16.0f * k * dpi, ImVec2(0, 4.0f * k * dpi));
    }
    if (pose.glow > 0.0f) {
        float k = imb::detail::Clamp01(pose.glow);
        imb::Glow(dl, emn, emx, rounding, imb::detail::WithAlpha(ToU32(Colors().accent), k), 12.0f * k * dpi);
    }
    bool clip = pose.reveal < 1.0f;
    if (clip) {
        ImVec2 a = emn, b = emx;
        imb::detail::RevealRect(def.reveal_dir, imb::detail::Clamp01(pose.reveal), a, b);
        dl->PushClipRect(a, b, true);
    }
    int start = dl->VtxBuffer.Size;
    DrawMotionSample(dl, min, max, style, kind, pose.text_reveal);
    if (clip) dl->PopClipRect();
    ImVec2 o(emn.x + (emx.x - emn.x) * def.origin.x, emn.y + (emx.y - emn.y) * def.origin.y);
    imb::detail::TransformVerts(dl, start, pose, o, false);
    dl->AddCircle(o, 3.0f, ToU32(Colors().accent), 12, 1.5f);
}

void DrawKeyframesTab(EditorContext& ctx) {
    SyncFromProject(ctx);
    EditorState& s = State();
    DrawToolbar(ctx);
    if (s.name.empty()) {
        DrawEmpty(ctx);
        return;
    }
    ImGui::Separator();
    if (!s.load_error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
        ImGui::TextWrapped("This animation's JSON can't be edited here: %s", s.load_error.c_str());
        ImGui::PopStyleColor();
        return;
    }
    std::string strict_error;
    const imb::AnimDef* def = PreviewDef(&strict_error);
    AdvancePlayhead(def);

    const float fs = ImGui::GetFontSize();
    ImGuiTableFlags tf = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("##kf_layout", 3, tf, ImGui::GetContentRegionAvail())) return;
    ImGui::TableSetupColumn("settings", ImGuiTableColumnFlags_WidthFixed, fs * 17.0f);
    ImGui::TableSetupColumn("timeline", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthFixed, fs * 13.0f);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (ImGui::BeginChild("##kf_settings_scroll", ImVec2(0, 0))) DrawSettings(ctx);
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    DrawTransport(def);
    float inspector_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    float timeline_h = std::max(fs * 6.0f, ImGui::GetContentRegionAvail().y - inspector_h);
    DrawTimeline(ctx, timeline_h);
    ImGui::Separator();
    DrawKeyInspector(ctx);

    ImGui::TableSetColumnIndex(2);
    DrawPreviewColumn(def, strict_error);
    ImGui::EndTable();
}

}  // namespace imforge::builder::kfui
