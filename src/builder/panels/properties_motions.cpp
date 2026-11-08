#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "imgui.h"

#include "imforge/imb_motion.h"
#include "imforge/imforge_motion_json.h"

#include "../ui/anim_preview_driver.h"
#include "../ui/motion_preview.h"
#include "animations_project.h"
#include "properties_shared.h"

namespace imforge::builder::props {

namespace {

const char* const kTriggerNames[] = {"enter", "exit", "hover", "press", "focus", "loop", "change", "attention"};
const int kTriggerCount = 8;
const char* const kTriggerHelp[] = {
    "Plays when the widget appears.",
    "Plays when the widget goes away (visibleIf turns false); the widget stays drawn until it ends.",
    "Plays forward while hovered, backward when the mouse leaves.",
    "Plays forward while held down, backward on release.",
    "Plays forward while the widget has keyboard focus.",
    "Repeats for as long as the widget is shown.",
    "Restarts when the \"on\" event fires (value changed and so on).",
    "Restarts when the \"on\" event fires, to draw the eye.",
};
const char* const kTabLabels[] = {"All", "Entrance", "Exit", "Attention", "Hover", "Press",
                                  "Loop", "Text", "Change", "Stagger", "Custom"};
const int kTabCount = 11;
const int kCustomTab = 10;
const float kPreviewHold = 0.4f;
const float kMaxPreviewSeconds = 6.0f;

int TriggerIndex(const std::string& t) {
    for (int i = 0; i < kTriggerCount; ++i)
        if (t == kTriggerNames[i]) return i;
    return -1;
}

// a Preview click plays one motion of one node on the Canvas, through Hooks::motion_options
struct PreviewRun {
    std::string node_id;
    imb::Trigger trigger = imb::Trigger::Enter;
    double until = 0;
    bool fired = false;
};

PreviewRun& Run() {
    static PreviewRun r;
    return r;
}

void ApplyPreview(const Node& n, imb::MotionOptions& o) {
    PreviewRun& r = Run();
    if (r.node_id.empty() || r.node_id != n.id) return;
    if (ImGui::GetTime() > r.until) {
        r.node_id.clear();
        return;
    }
    switch (r.trigger) {
    case imb::Trigger::Hover: o.force_hover = true; break;
    case imb::Trigger::Press: o.force_press = true; break;
    // hidden while the exit plays; when the run ends it comes back and enters again
    case imb::Trigger::Exit: o.visible = false; break;
    default:
        // the hook runs inside the node's id scope, right after the interpreter took its id
        if (!r.fired) imb::TriggerMotion(ImGui::GetID("##imforge_node"), r.trigger);
        r.fired = true;
        break;
    }
}

void EnsurePreviewHook(EditorContext& ctx) {
#if IMFORGE_HAS_INTERP
    if (!ctx.interp.hooks.motion_options) ctx.interp.hooks.motion_options = ApplyPreview;
#else
    (void)ctx;
#endif
}

float MotionLength(const imb::AnimDef& def, const MotionSpec& m) {
    float scale = m.duration_scale > 0 ? m.duration_scale : 1.0f;
    int iters = m.iterations >= 0 ? m.iterations : def.iterations;
    float len = def.delay + m.delay + def.duration * scale * (float)(iters > 0 ? iters : 1);
    return std::min(len, kMaxPreviewSeconds);
}

bool CanPreview(const MotionSpec& m) { return m.trigger != "focus"; }

void StartPreview(PropsUI& ui, const MotionSpec& m) {
    const imb::AnimDef* def = anims::MotionDef(ui.ctx.project, m);
    imb::Trigger t = imb::Trigger::Enter;
    if (!def || !TryMotionTriggerFromName(m.trigger.c_str(), &t)) return;
    EnsurePreviewHook(ui.ctx);
    PreviewRun& r = Run();
    r.node_id = ui.node.id;
    r.trigger = t;
    r.fired = false;
    r.until = ImGui::GetTime() + (t == imb::Trigger::Loop ? 0.1 : (double)(MotionLength(*def, m) + kPreviewHold));
    AnimPreviewPlay(true);
}

std::string Lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

struct PickItem {
    std::string name;
    const imb::AnimDef* def = nullptr;
    bool custom = false;
};

struct PickerState {
    std::string search;
    int tab = 0;
    int target = -1;
    bool just_opened = false;
    std::string hovered;
    ImVec2 anchor_min, anchor_max;
};

PickerState& Picker() {
    static PickerState s;
    return s;
}

// below the button, or above it when there's no room, kept inside the main viewport
void PlacePopup(const ImVec2& amin, const ImVec2& amax, const ImVec2& size) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float x = std::min(amin.x, vp->WorkPos.x + vp->WorkSize.x - size.x - 8.0f);
    x = std::max(x, vp->WorkPos.x + 8.0f);
    bool above = amax.y + size.y > vp->WorkPos.y + vp->WorkSize.y && amin.y - size.y > vp->WorkPos.y;
    ImGui::SetNextWindowPos(ImVec2(x, above ? amin.y : amax.y), ImGuiCond_Appearing, ImVec2(0.0f, above ? 1.0f : 0.0f));
}

bool TabAccepts(int tab, const PickItem& it) {
    if (tab == 0) return true;
    if (tab == kCustomTab) return it.custom;
    const char* cat = it.def && it.def->category ? it.def->category : "";
    return Lower(kTabLabels[tab]) == cat;
}

std::vector<PickItem> Candidates(const Project& p, const PickerState& ps) {
    std::vector<PickItem> out;
    std::string needle = Lower(ps.search);
    auto keep = [&](const PickItem& it) {
        if (!TabAccepts(ps.tab, it)) return false;
        if (needle.empty()) return true;
        const char* cat = it.def && it.def->category ? it.def->category : "";
        return Lower(it.name).find(needle) != std::string::npos || Lower(cat).find(needle) != std::string::npos;
    };
    for (const std::string& n : anims::CustomNames(p)) {
        PickItem it;
        it.name = n;
        it.custom = true;
        it.def = anims::CustomDef(p, n);
        if (keep(it)) out.push_back(it);
    }
    for (const std::string& n : anims::PresetNames()) {
        PickItem it;
        it.name = n;
        it.def = imb::FindAnim(n.c_str());
        if (keep(it)) out.push_back(it);
    }
    return out;
}

void ApplyPick(PropsUI& ui, int target, const PickItem& it) {
    std::string def_trigger = it.def ? MotionTriggerName(it.def->default_trigger) : "enter";
    auto& ms = ui.node.anim.motions;
    const imb::AnimDef* old = (target >= 0 && target < (int)ms.size()) ? anims::MotionDef(ui.ctx.project, ms[(size_t)target]) : nullptr;
    ui.Edit(target < 0 ? "Add motion" : "Change motion", [&] {
        MotionSpec* m = nullptr;
        if (target < 0 || target >= (int)ms.size()) {
            ms.emplace_back();
            m = &ms.back();
            m->trigger = def_trigger;
        } else {
            m = &ms[(size_t)target];
            if (!old || m->trigger == MotionTriggerName(old->default_trigger)) m->trigger = def_trigger;
        }
        m->preset = it.custom ? std::string() : it.name;
        m->custom = it.custom ? it.name : std::string();
    });
}

void DrawChips(PickerState& ps) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const BuilderColors& c = Colors();
    for (int i = 0; i < kTabCount; ++i) {
        float w = ImGui::CalcTextSize(kTabLabels[i]).x + st.FramePadding.x * 2.0f;
        if (i > 0) {
            ImGui::SameLine(0, st.ItemInnerSpacing.x);
            if (ImGui::GetCursorScreenPos().x + w > right) ImGui::NewLine();
        }
        bool on = ps.tab == i;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? c.accent : c.bg_frame);
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(1, 1, 1, 1) : c.text_dim);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5f);
        if (ImGui::SmallButton(kTabLabels[i])) ps.tab = i;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }
}

void DrawPickRow(PropsUI& ui, PickerState& ps, const PickItem& it, const std::string& current, float row_h) {
    const BuilderColors& c = Colors();
    ImGui::PushID(it.name.c_str());
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool sel = it.name == current;
    bool clicked = ImGui::Selectable("##row", sel, ImGuiSelectableFlags_None, ImVec2(0, row_h));
    bool hovered = ImGui::IsItemHovered();
    bool held = ImGui::IsItemActive();
    float w = ImGui::GetItemRectSize().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pw = std::floor(row_h * 1.9f);
    ImVec2 pmin(p.x + 2.0f, p.y + 2.0f), pmax(p.x + pw, p.y + row_h - 2.0f);
    dl->AddRectFilled(pmin, pmax, ToU32(c.bg_window), 4.0f);
    ImGuiID pid = ImGui::GetID("##prev");
    if (it.def) {
        if (hovered && ps.hovered != it.name) imb::TriggerMotion(pid, imb::Trigger::Enter);
        MotionPreviewStyle style;
        style.hovered = hovered;
        style.pressed = held;
        style.size = 0.66f;
        DrawMotionPreview(pid, *it.def, pmin, pmax, style);
    } else {
        dl->AddText(ImVec2(pmin.x + 6.0f, pmin.y + 4.0f), ToU32(c.error), "?");
    }
    if (hovered) ps.hovered = it.name;
    float tx = p.x + pw + ImGui::GetStyle().ItemSpacing.x;
    float line = ImGui::GetTextLineHeight();
    float ty = p.y + (row_h - line * 2.0f - 2.0f) * 0.5f;
    dl->AddText(ImVec2(tx, ty), ToU32(it.def ? c.text : c.error), it.name.c_str());
    char sub[160];
    if (it.def)
        std::snprintf(sub, sizeof(sub), "%s%s  \xc2\xb7  %.2f s  \xc2\xb7  %s", it.custom ? "custom, " : "",
                      it.def->category ? it.def->category : "", (double)it.def->duration, MotionTriggerName(it.def->default_trigger));
    else
        std::snprintf(sub, sizeof(sub), "does not load, open it in the Keyframes tab");
    dl->PushClipRect(ImVec2(tx, p.y), ImVec2(p.x + w, p.y + row_h), true);
    dl->AddText(ImVec2(tx, ty + line + 2.0f), ToU32(c.text_dim), sub);
    dl->PopClipRect();
    if (clicked && it.def) {
        ApplyPick(ui, ps.target, it);
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopID();
}

void DrawPickerPopup(PropsUI& ui) {
    PickerState& ps = Picker();
    float fs = ImGui::GetFontSize();
    ImVec2 size(fs * 27.0f, fs * 30.0f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    PlacePopup(ps.anchor_min, ps.anchor_max, size);
    if (!ImGui::BeginPopup("##motion_picker")) return;
    auto& ms = ui.node.anim.motions;
    std::string current = (ps.target >= 0 && ps.target < (int)ms.size()) ? ms[(size_t)ps.target].Name() : std::string();
    ImGui::TextUnformatted(ps.target < 0 ? "Add a motion" : "Change the animation");
    ImGui::SameLine();
    ui::TextDim("(%d presets, %d custom)", (int)anims::PresetNames().size(), (int)ui.ctx.project.animations.size());
    if (ps.just_opened) {
        ImGui::SetKeyboardFocusHere();
        ps.just_opened = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::InputTextWithHintStd("##search", "Search by name or category", ps.search);
    DrawChips(ps);
    ImGui::Separator();

    std::vector<PickItem> items = Candidates(ui.ctx.project, ps);
    float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("##list", ImVec2(0, -footer))) {
        if (items.empty()) {
            ui::TextDim(ps.tab == kCustomTab ? "No custom animations yet." : "Nothing matches \"%s\".", ps.search.c_str());
        }
        float row_h = std::floor(fs * 3.0f);
        ImGuiListClipper clipper;
        clipper.Begin((int)items.size(), row_h + ImGui::GetStyle().ItemSpacing.y);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) DrawPickRow(ui, ps, items[(size_t)i], current, row_h);
    }
    ImGui::EndChild();
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) ps.hovered.clear();

    if (ImGui::Button(PP_ICON_ADD " New custom animation")) {
        ui.ctx.BeginEdit("New custom animation", DocKind::Project, std::string());
        std::string name = anims::Create(ui.ctx);
        PickItem it;
        it.name = name;
        it.custom = true;
        it.def = anims::CustomDef(ui.ctx.project, name);
        ApplyPick(ui, ps.target, it);
        ui.ctx.EndEdit();
        anims::RequestEdit(ui.ctx, name);
        ImGui::CloseCurrentPopup();
    }
    ui::Tooltip("Creates a project animation, uses it here and opens it in the Keyframes tab");
    ImGui::SameLine();
    bool can_edit = !current.empty();
    if (!can_edit) ImGui::BeginDisabled();
    if (ImGui::Button("Edit keyframes")) {
        if (anims::IndexOf(ui.ctx.project, current) < 0) current = anims::DuplicatePreset(ui.ctx, current);
        if (!current.empty()) anims::RequestEdit(ui.ctx, current);
        ImGui::CloseCurrentPopup();
    }
    if (!can_edit) ImGui::EndDisabled();
    ui::Tooltip("Custom animations open in the Keyframes tab; a preset gets copied into the project first");
    ImGui::EndPopup();
}

const char* IterFormat(int it) { return it < 0 ? "default" : (it == 0 ? "infinite" : "%d x"); }

// returns true when the list changed shape (moved or removed), the caller stops drawing rows then
bool DrawMotionRow(PropsUI& ui, size_t i, bool& open_picker) {
    const BuilderColors& c = Colors();
    const ImGuiStyle& st = ImGui::GetStyle();
    auto& ms = ui.node.anim.motions;
    MotionSpec m = ms[i];
    const size_t count = ms.size();
    const imb::AnimDef* def = anims::MotionDef(ui.ctx.project, m);
    bool changed = false;
    ImGui::PushID((int)i);

    float fh = ImGui::GetFrameHeight();
    float thumb_h = fh * 3.0f + st.ItemSpacing.y * 2.0f;
    float thumb_w = std::floor(thumb_h * 1.25f);
    ImVec2 tp = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##thumb", ImVec2(thumb_w, thumb_h));
    bool th = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked() && def && CanPreview(m)) StartPreview(ui, m);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 tmax(tp.x + thumb_w, tp.y + thumb_h);
    dl->AddRectFilled(tp, tmax, ToU32(c.bg_window), st.FrameRounding);
    dl->AddRect(tp, tmax, ToU32(th ? c.accent : c.border, th ? 0.8f : 0.6f), st.FrameRounding);
    if (def) {
        MotionPreviewStyle style;
        style.hovered = th;
        style.pressed = th && ImGui::IsMouseDown(0);
        style.size = 0.62f;
        DrawMotionPreview(ImGui::GetID("##mini"), *def, ImVec2(tp.x + 2.0f, tp.y + 2.0f), ImVec2(tmax.x - 2.0f, tmax.y - 2.0f),
                          style);
    } else {
        ImVec2 ts = ImGui::CalcTextSize("?");
        dl->AddText(ImVec2(tp.x + (thumb_w - ts.x) * 0.5f, tp.y + (thumb_h - ts.y) * 0.5f), ToU32(c.error), "?");
    }
    if (th) ImGui::SetTooltip(def && CanPreview(m) ? "Click to play it on the Canvas" : "Focus motions play when the widget has keyboard focus");

    ImGui::SameLine();
    ImGui::BeginGroup();
    float avail = ImGui::GetContentRegionAvail().x;
    float bw = SmallButtonWidth() + st.ItemInnerSpacing.x;
    float trig_w = std::floor(ImGui::GetFontSize() * 5.6f);
    float name_w = std::max(avail - trig_w - st.ItemInnerSpacing.x - bw * 2.0f, ImGui::GetFontSize() * 4.0f);

    std::string label = m.Name().empty() ? std::string("pick an animation") : m.Name();
    if (!m.custom.empty()) label += "  (custom)";
    label += "##pick";
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, def ? c.text : c.error);
    if (ImGui::Button(label.c_str(), ImVec2(name_w, 0))) {
        Picker().target = (int)i;
        Picker().anchor_min = ImGui::GetItemRectMin();
        Picker().anchor_max = ImGui::GetItemRectMax();
        open_picker = true;
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        if (def)
            ImGui::SetTooltip("%s: %s, %.2f s%s\nClick to pick another one, right-click for more", m.Name().c_str(),
                              def->category ? def->category : "", (double)def->duration,
                              m.custom.empty() ? "" : "\nCustom animation from this project");
        else if (m.Name().empty())
            ImGui::SetTooltip("Click to pick an animation");
        else
            ImGui::SetTooltip(m.custom.empty() ? "Unknown preset \"%s\"" : "Custom animation \"%s\" is missing or does not load",
                              m.Name().c_str());
    }
    if (ImGui::BeginPopupContextItem("##pickctx")) {
        if (!m.custom.empty() && ImGui::MenuItem("Edit keyframes")) anims::RequestEdit(ui.ctx, m.custom);
        if (!m.preset.empty() && ImGui::MenuItem("Copy as custom and edit")) {
            ui.ctx.BeginEdit("Copy preset as custom", DocKind::Project, std::string());
            std::string name = anims::DuplicatePreset(ui.ctx, m.preset);
            if (!name.empty()) {
                ui.Edit("Edit motion", [&] {
                    MotionSpec& target = ui.node.anim.motions[i];
                    target.preset.clear();
                    target.custom = name;
                });
            }
            ui.ctx.EndEdit();
            if (!name.empty()) {
                m.preset.clear();
                m.custom = name;
                anims::RequestEdit(ui.ctx, name);
            }
        }
        if (ImGui::MenuItem("Duplicate motion")) {
            ui.Edit("Duplicate motion", [&] { ui.node.anim.motions.insert(ui.node.anim.motions.begin() + (long)i + 1, m); });
            ImGui::EndPopup();
            ImGui::EndGroup();
            ImGui::PopID();
            return true;
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(trig_w);
    int trig = TriggerIndex(m.trigger);
    if (ComboIndex("##trigger", trig, kTriggerNames, kTriggerCount)) {
        m.trigger = kTriggerNames[trig];
        changed = true;
    }
    if (trig >= 0) ui::Tooltip(kTriggerHelp[trig]);
    TrailingSameLine();
    if (SmallIconButton("prev", PP_ICON_PLAY, CanPreview(m) ? "Play it on the Canvas" : "Focus motions can't be forced from here",
                        def && CanPreview(m)))
        StartPreview(ui, m);
    TrailingSameLine();
    bool remove = SmallIconButton("rm", PP_ICON_REMOVE, "Remove motion");

    float third = std::floor((avail - bw * 2.0f - st.ItemInnerSpacing.x * 2.0f) / 3.0f);
    ImGui::SetNextItemWidth(third);
    if (ImGui::DragFloat("##dscale", &m.duration_scale, 0.01f, 0.05f, 10.0f, "x%.2f", ImGuiSliderFlags_AlwaysClamp)) changed = true;
    ui::Tooltip("Duration scale: 2 plays half as fast");
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(third);
    if (ImGui::DragFloat("##delay", &m.delay, 0.005f, 0.0f, 10.0f, "+%.2f s", ImGuiSliderFlags_AlwaysClamp)) changed = true;
    ui::Tooltip("Extra delay before it starts");
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(third);
    if (ImGui::DragInt("##iters", &m.iterations, 0.08f, -1, 1000, IterFormat(m.iterations), ImGuiSliderFlags_AlwaysClamp))
        changed = true;
    ui::Tooltip("Iterations: default uses the animation's own count, infinite repeats forever");
    TrailingSameLine();
    bool up = SmallIconButton("up", PP_ICON_UP, "Move up", i > 0);
    TrailingSameLine();
    bool down = SmallIconButton("down", PP_ICON_DOWN, "Move down", i + 1 < count);

    float half = std::floor((avail - st.ItemInnerSpacing.x) * 0.5f);
    bool container = ui.def && ui.def->container;
    if (!container) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(half);
    if (ImGui::DragFloat("##stagger", &m.stagger, 0.002f, 0.0f, 2.0f, m.stagger > 0 ? "stagger %.3f s" : "no stagger",
                         ImGuiSliderFlags_AlwaysClamp))
        changed = true;
    if (!container) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(container ? "Plays the motion on each child instead, this much later per child"
                                    : "Only containers can stagger their children");
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(-FLT_MIN);
    int event_count = ui.def ? ui.def->event_count : 0;
    if (event_count == 0) ImGui::BeginDisabled();
    std::string on_preview = m.on.empty() ? std::string("on: no event") : "on: " + m.on;
    if (ImGui::BeginCombo("##on", on_preview.c_str())) {
        if (ImGui::Selectable("no event", m.on.empty()) && !m.on.empty()) {
            m.on.clear();
            changed = true;
        }
        for (int e = 0; e < event_count; ++e) {
            const char* ev = ui.def->events[e];
            bool sel = m.on == ev;
            if (ImGui::Selectable(ev, sel) && !sel) {
                m.on = ev;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (event_count == 0) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(event_count ? "Restarts this motion whenever the event fires (handy with change/attention)"
                                      : "This widget has no events");
    ImGui::EndGroup();

    if (up || down || remove) {
        ui.Edit(remove ? "Remove motion" : "Reorder motions", [&] {
            auto& list = ui.node.anim.motions;
            if (up) std::swap(list[i], list[i - 1]);
            else if (down) std::swap(list[i], list[i + 1]);
            else list.erase(list.begin() + (long)i);
        });
        ImGui::PopID();
        return true;
    }
    if (changed) ui.Edit("Edit motion", [&] { ui.node.anim.motions[i] = m; });
    ImGui::PopID();
    return false;
}

}  // namespace

void DrawMotionsSection(PropsUI& ui) {
    Node& node = ui.node;
    if (!SectionHeader("Motions", ui.multi || !node.anim.motions.empty())) return;
    EnsurePreviewHook(ui.ctx);
    if (!ui.ctx.project.Feature("animations", true)) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors().warning);
        ImGui::TextWrapped("The animations feature is disabled in Project Settings; motions are kept but not emitted.");
        ImGui::PopStyleColor();
    }
    if (ui.multi) ui::TextDim("Edits apply to all %d selected widgets.", (int)ui.selected.size());
    bool open_picker = false;
    for (size_t i = 0; i < node.anim.motions.size(); ++i) {
        if (DrawMotionRow(ui, i, open_picker)) break;
        if (i + 1 < node.anim.motions.size()) ImGui::Separator();
    }
    if (node.anim.motions.empty()) ui::TextDim("No motions. Add one to animate this widget with a keyframe preset.");
    if (ui::AccentButton(PP_ICON_ADD " Add motion")) {
        Picker().target = -1;
        Picker().anchor_min = ImGui::GetItemRectMin();
        Picker().anchor_max = ImGui::GetItemRectMax();
        open_picker = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Replay all")) {
        imb::ResetMotions();
        AnimPreviewPlay(true);
    }
    ui::Tooltip("Restart every motion on the Canvas");
    if (open_picker) {
        Picker().just_opened = true;
        Picker().hovered.clear();
        if (Picker().target >= 0) {
            const MotionSpec& m = node.anim.motions[(size_t)Picker().target];
            Picker().tab = m.custom.empty() ? 0 : kCustomTab;
        }
        ImGui::OpenPopup("##motion_picker");
    }
    DrawPickerPopup(ui);
}

}  // namespace imforge::builder::props
