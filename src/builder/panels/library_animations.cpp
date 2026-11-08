#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <system_error>

#include "../project_io.h"
#include "../ui/canvas_overlay.h"
#include "../ui/motion_preview.h"
#include "animations_project.h"
#include "library_actions.h"
#include "library_shared.h"

#if IMFORGE_HAS_MOTION && __has_include("imforge/imb_motion_presets.h") && __has_include("imforge/imforge_motion_json.h")
#include "imforge/imb_motion_presets.h"
#include "imforge/imforge_motion_json.h"
#define IMFORGE_LIBRARY_ANIMS 1
#else
#define IMFORGE_LIBRARY_ANIMS 0
#endif

namespace fs = std::filesystem;

namespace imforge::builder::library {

namespace {

#if IMFORGE_LIBRARY_ANIMS

const char* const kCategories[] = {"entrance", "exit", "attention", "hover", "press", "loop", "text", "change", "stagger"};
const char* const kTriggers[] = {"enter", "exit", "hover", "press", "focus", "loop", "change", "attention"};

int CategoryOrder(const std::string& c) {
    for (int i = 0; i < (int)(sizeof(kCategories) / sizeof(kCategories[0])); ++i)
        if (c == kCategories[i]) return i;
    return 100;
}

ImU32 CategoryColor(const std::string& c) {
    struct Entry {
        const char* name;
        ImU32 col;
    };
    static const Entry kColors[] = {
        {"entrance", IM_COL32(108, 140, 255, 255)}, {"exit", IM_COL32(255, 107, 125, 255)},
        {"attention", IM_COL32(255, 181, 71, 255)}, {"hover", IM_COL32(63, 208, 201, 255)},
        {"press", IM_COL32(199, 125, 255, 255)},    {"loop", IM_COL32(76, 217, 138, 255)},
        {"text", IM_COL32(122, 162, 255, 255)},     {"change", IM_COL32(71, 181, 255, 255)},
        {"stagger", IM_COL32(167, 139, 250, 255)},
    };
    for (const Entry& e : kColors)
        if (c == e.name) return e.col;
    return ToU32(Colors().accent);
}

std::string Capitalized(const std::string& s) {
    std::string out = s;
    if (!out.empty()) out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}


struct AnimItem {
    std::string name, category, group, description, credit, trigger;
    std::vector<std::string> tags;
    const imb::AnimDef* def = nullptr;
    bool custom = false;
};

struct Meta {
    std::string description, credit;
    std::vector<std::string> tags;
};

std::map<std::string, Meta> LoadPresetMeta() {
    std::map<std::string, Meta> out;
    const std::string dir = FindDataDir("presets/animations");
    if (dir.empty()) return out;
    std::error_code ec;
    std::vector<std::string> files;
    for (fs::directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().u8string();
        if (name.size() > 13 && name.compare(name.size() - 13, 13, ".imanims.json") == 0) files.push_back(it->path().u8string());
    }
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        Json set;
        if (!ReadJsonFile(f, set)) continue;
        for (const Json& a : set["animations"].arr) {
            Meta m;
            m.description = a["description"].AsString();
            m.credit = a["credit"].AsString();
            for (const Json& t : a["tags"].arr)
                if (t.IsString()) m.tags.push_back(t.AsString());
            out[a["name"].AsString()] = std::move(m);
        }
    }
    return out;
}

class AnimationsTab final : public Tab {
public:
    const char* Title() const override { return "Animations"; }
    const char* IconName() const override { return "fa-wand-magic-sparkles"; }
    int Count(EditorContext& ctx) override {
        Refresh(ctx);
        return (int)items_.size();
    }
    void OnProjectChanged(EditorContext&) override {
        custom_serial_ = ~0u;
        selected_.clear();
    }

    bool Command(EditorContext& ctx, const std::string& verb, const std::string& arg) override {
        Refresh(ctx);
        if (verb == "search") search_ = arg;
        else if (verb == "category") group_ = arg;
        else if (verb == "select") {
            for (const AnimItem& it : items_)
                if (it.name == arg) {
                    Select(ctx, it);
                    return true;
                }
            return false;
        } else if (verb == "add") {
            const AnimItem* sel = Selected();
            return sel && AddMotionToNodes(ctx, ctx.selection, MotionPayloadFor(sel->name, sel->custom), arg.empty() ? trigger_ : arg) > 0;
        } else if (verb == "duplicate") {
            const AnimItem* sel = Selected();
            if (!sel || !ctx.has_project) return false;
            const size_t before = ctx.project.animations.size();
            DuplicateOrEdit(ctx, *sel);
            return ctx.project.animations.size() > before;
        } else if (verb == "new") {
            const size_t before = ctx.project.animations.size();
            NewAnimation(ctx);
            return ctx.project.animations.size() > before;
        } else {
            return false;
        }
        return true;
    }

    void Draw(EditorContext& ctx) override {
        Refresh(ctx);
        FilterBar(ctx, "Search animations, tags or descriptions", search_, tags_, tag_);
        ChipRow("##anim_chips", groups_, group_, (int)items_.size());
        std::vector<const AnimItem*> shown;
        for (const AnimItem& it : items_)
            if (Matches(it)) shown.push_back(&it);
        const float s = Dpi(ctx);
        const float footer = std::floor(14.0f * s) + ImGui::GetTextLineHeight() * 2.0f + std::floor(1.0f * s);
        int animated = 0;
        CardGrid(ctx, "##anim_grid", (int)shown.size(), 150.0f, 0.62f, footer, ActionBarHeight(), "fa-wand-magic-sparkles",
                 "No animation matches", "Try another search, category or tag.", [&](const Card& card) {
                     const AnimItem& it = *shown[(size_t)card.index];
                     const bool selected = it.name == selected_ && it.custom == selected_custom_;
                     DrawContextMenu(ctx, it);
                     if (ImGui::BeginDragDropSource()) {
                         std::string payload = MotionPayloadFor(it.name, it.custom);
                         ImGui::SetDragDropPayload(kPayloadMotion, payload.c_str(), payload.size() + 1);
                         const char* icon = Icon("fa-wand-magic-sparkles");
                         ImGui::Text("%s%s%s", icon ? icon : "", icon ? "  " : "", it.name.c_str());
                         ui::TextDim("Drop on a node in the canvas or hierarchy");
                         ImGui::EndDragDropSource();
                     } else if (card.tooltip && !it.description.empty()) {
                         std::string text = it.description;
                         if (!it.tags.empty()) {
                             text += "\n";
                             for (const std::string& t : it.tags) text += " #" + t;
                         }
                         if (!it.credit.empty()) text += "\nBased on " + it.credit;
                         WrappedTooltip(it.name.c_str(), text.c_str());
                     }
                     if (card.clicked) Select(ctx, it);
                     if (card.double_clicked) AddToSelection(ctx, it, it.trigger);
                     DrawCard(ctx, card, it, selected, animated);
                 });
        DrawActionBar(ctx);
    }

private:
    std::vector<AnimItem> items_;
    std::vector<AnimStorage> custom_;
    Counts groups_, tags_;
    std::map<std::string, Meta> meta_;
    bool meta_loaded_ = false;
    bool presets_loaded_ = false;
    unsigned custom_serial_ = ~0u;
    size_t custom_count_ = 0;
    std::string search_, group_, tag_;
    std::string selected_, trigger_;
    bool selected_custom_ = false;

    bool Matches(const AnimItem& it) const {
        if (!group_.empty() && it.group != group_) return false;
        if (!tag_.empty() && std::find(it.tags.begin(), it.tags.end(), tag_) == it.tags.end()) return false;
        if (search_.empty()) return true;
        if (ContainsCI(it.name, search_) || ContainsCI(it.category, search_) || ContainsCI(it.description, search_)) return true;
        for (const std::string& t : it.tags)
            if (ContainsCI(t, search_)) return true;
        return false;
    }

    void Refresh(EditorContext& ctx) {
        if (!meta_loaded_) {
            meta_ = LoadPresetMeta();
            meta_loaded_ = true;
        }
        if (presets_loaded_ && custom_serial_ == ctx.edit_serial && custom_count_ == ctx.project.animations.size()) return;
        presets_loaded_ = true;
        custom_serial_ = ctx.edit_serial;
        custom_count_ = ctx.project.animations.size();
        custom_.clear();
        for (const Json& a : ctx.project.animations) {
            AnimStorage st;
            if (AnimFromJson(a, st)) custom_.push_back(std::move(st));
        }
        items_.clear();
        for (int i = 0; i < imb::AnimCount(); ++i) {
            const imb::AnimDef* def = imb::AnimAt(i);
            if (!def || !def->name || ctx.project.FindAnimation(def->name)) continue;
            AnimItem it;
            it.name = def->name;
            it.category = def->category ? def->category : "";
            it.group = it.category;
            it.trigger = MotionTriggerName(def->default_trigger);
            it.def = def;
            auto m = meta_.find(it.name);
            if (m != meta_.end()) {
                it.description = m->second.description;
                it.credit = m->second.credit;
                it.tags = m->second.tags;
            }
            items_.push_back(std::move(it));
        }
        std::stable_sort(items_.begin(), items_.end(), [](const AnimItem& a, const AnimItem& b) {
            int ca = CategoryOrder(a.category), cb = CategoryOrder(b.category);
            return ca != cb ? ca < cb : a.name < b.name;
        });
        for (const AnimStorage& st : custom_) {
            AnimItem it;
            it.name = st.Name();
            it.category = st.data->category;
            it.group = "project";
            it.description = st.data->description;
            it.tags = st.data->tags;
            it.trigger = MotionTriggerName(st.Def()->default_trigger);
            it.def = st.Def();
            it.custom = true;
            items_.push_back(std::move(it));
        }
        groups_.clear();
        tags_.clear();
        for (const char* c : kCategories) groups_.emplace_back(c, 0);
        for (const AnimItem& it : items_) {
            AddCount(groups_, it.group);
            for (const std::string& t : it.tags) AddCount(tags_, t);
        }
        groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [](const std::pair<std::string, int>& g) { return g.second <= 0; }),
                      groups_.end());
        std::sort(tags_.begin(), tags_.end(), [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        if (!Selected()) selected_.clear();
    }

    const AnimItem* Selected() const {
        for (const AnimItem& it : items_)
            if (it.name == selected_ && it.custom == selected_custom_) return &it;
        return nullptr;
    }

    void Select(EditorContext& ctx, const AnimItem& it) {
        (void)ctx;
        if (selected_ == it.name && selected_custom_ == it.custom) return;
        selected_ = it.name;
        selected_custom_ = it.custom;
        trigger_ = it.trigger;
    }

    void AddToSelection(EditorContext& ctx, const AnimItem& it, const std::string& trigger) {
        Select(ctx, it);
        if (ctx.selection.empty()) {
            ctx.SetStatus("Select a node first, or drag the card onto one");
            return;
        }
        AddMotionToNodes(ctx, ctx.selection, MotionPayloadFor(it.name, it.custom), trigger);
    }

    void DrawCard(EditorContext& ctx, const Card& card, const AnimItem& it, bool selected, int& animated) {
        const BuilderColors& c = Colors();
        const float s = Dpi(ctx);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        CardBackground(dl, ctx, card, selected);
        PreviewInset(dl, ctx, card, ToU32(c.bg_window));
        const ImVec2 pmn(card.pmin.x + 1.0f, card.pmin.y + 1.0f), pmx(card.pmax.x - 1.0f, card.pmax.y);
        overlay::DottedGrid(dl, pmn, pmx, pmn, std::floor(12.0f * s), ToU32(c.text, 0.07f), 1.0f);
        MotionPreviewStyle st;
        st.accent = it.custom ? ToU32(c.accent) : CategoryColor(it.category);
        st.hovered = card.hovered;
        st.pressed = card.held;
        st.phase = -0.07f * (float)card.index;
        const MotionPreviewKind kind = MotionPreviewKindOf(*it.def);
        if (animated < kMaxAnimatedPreviews) {
            ++animated;
            DrawMotionPreview(ImGui::GetID("##motion"), *it.def, pmn, pmx, st);
        } else {
            dl->PushClipRect(pmn, pmx, true);
            DrawMotionSample(dl, pmn, pmx, st, kind);
            dl->PopClipRect();
        }
        if (it.custom) CornerBadge(dl, ctx, card, "project", ToU32(c.accent_hover));
        else if (kind == MotionPreviewKind::Hover || kind == MotionPreviewKind::Press) {
            const char* icon = Icon("fa-hand-pointer");
            CornerBadge(dl, ctx, card, icon ? icon : (kind == MotionPreviewKind::Hover ? "hover" : "press"), ToU32(c.text_dim));
        }
        char sub[96];
        const float dur = it.def->duration;
        if (it.def->iterations == 0 || kind == MotionPreviewKind::Loop)
            std::snprintf(sub, sizeof(sub), "%s  %.2gs, loops", Capitalized(it.category).c_str(), dur);
        else
            std::snprintf(sub, sizeof(sub), "%s  %.2gs", Capitalized(it.category).c_str(), dur);
        CardFooter(dl, ctx, card, it.name.c_str(), sub, selected, it.custom ? ToU32(c.accent) : CategoryColor(it.category));
    }

    void DrawActionBar(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        const ImGuiStyle& st = ImGui::GetStyle();
        BeginActionBar();
        const AnimItem* sel = Selected();
        const char* add_label = "Add to selection";
        const float combo_w = std::floor(ImGui::GetFontSize() * 7.0f);
        const float add_w = ui::ButtonWidth(add_label);
        const char* new_icon = Icon("fa-plus");
        const std::string new_label = std::string(new_icon ? new_icon : "+") + "  New animation";
        const float new_w = ui::ButtonWidth(new_label.c_str());
        const char* dup_icon = Icon(sel && sel->custom ? "fa-pen" : "fa-copy");
        const char* dup_label = dup_icon ? dup_icon : (sel && sel->custom ? "Edit" : "Duplicate");
        const float dup_w = ui::ButtonWidth(dup_label);
        const float right_w = (sel ? combo_w + add_w + dup_w + st.ItemSpacing.x * 3.0f + ImGui::CalcTextSize("Trigger").x + st.ItemSpacing.x : 0.0f) +
                              new_w + st.ItemSpacing.x;
        const float left_w = ImGui::GetContentRegionAvail().x - right_w - st.ItemSpacing.x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float ty = p.y + st.FramePadding.y;
        if (sel) {
            const ImU32 dot = sel->custom ? ToU32(c.accent) : CategoryColor(sel->category);
            const float r = std::floor(ImGui::GetFontSize() * 0.22f);
            dl->AddCircleFilled(ImVec2(p.x + r, std::floor(ty + ImGui::GetTextLineHeight() * 0.5f) + 0.5f), r, dot, 12);
            const float nx = p.x + r * 2.0f + st.ItemInnerSpacing.x;
            TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(nx, ty), left_w - (nx - p.x), ToU32(c.text), sel->name.c_str());
            const float dx = nx + ImGui::CalcTextSize(sel->name.c_str()).x + st.ItemSpacing.x * 1.5f;
            std::string desc = sel->description.empty() ? Capitalized(sel->category) : sel->description;
            if (dx < p.x + left_w - 20.0f)
                TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(dx, ty), p.x + left_w - dx, ToU32(c.text_dim), desc.c_str());
        } else {
            TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x, ty), left_w, ToU32(c.text_dim),
                         "Click a card to pick it, drag it onto a node, double-click to add it to the selection.");
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + ImGui::GetContentRegionAvail().x - right_w + st.ItemSpacing.x, p.y));
        if (sel) {
            ImGui::AlignTextToFramePadding();
            ui::TextDim("Trigger");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(combo_w);
            if (ImGui::BeginCombo("##anim_trigger", trigger_.c_str())) {
                for (const char* t : kTriggers) {
                    std::string label = t;
                    if (label == sel->trigger) label += "  (default)";
                    if (ImGui::Selectable(label.c_str(), trigger_ == t)) trigger_ = t;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            const int n = (int)ctx.selection.size();
            const bool can = n > 0 && ctx.ActiveRoot();
            if (!can) ImGui::BeginDisabled();
            if (ui::AccentButton(add_label)) AddToSelection(ctx, *sel, trigger_);
            if (!can) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
                if (can) ImGui::SetTooltip("Adds %s on %s to %d selected node%s", sel->name.c_str(), trigger_.c_str(), n, n == 1 ? "" : "s");
                else ImGui::SetTooltip("Select one or more nodes in the canvas or hierarchy first");
            }
            ImGui::SameLine();
            if (!ctx.has_project) ImGui::BeginDisabled();
            if (ImGui::Button(dup_label)) DuplicateOrEdit(ctx, *sel);
            if (!ctx.has_project) ImGui::EndDisabled();
            ui::Tooltip(sel->custom ? "Edit the keyframes in the Anim Timeline" : "Duplicate as a custom project animation and open its keyframes");
            ImGui::SameLine();
        }
        if (!ctx.has_project) ImGui::BeginDisabled();
        if (ImGui::Button(new_label.c_str())) NewAnimation(ctx);
        if (!ctx.has_project) ImGui::EndDisabled();
        ui::Tooltip("Start a custom keyframe animation for this project in the Anim Timeline");
        EndActionBar();
    }

    void NewAnimation(EditorContext& ctx) {
        const std::string name = anims::Create(ctx);
        if (name.empty()) return;
        selected_ = name;
        selected_custom_ = true;
        trigger_ = "enter";
        group_ = "project";
        anims::RequestEdit(ctx, name);
    }

    void DuplicateOrEdit(EditorContext& ctx, const AnimItem& it) {
        if (it.custom) {
            anims::RequestEdit(ctx, it.name);
            return;
        }
        const std::string name = anims::DuplicatePreset(ctx, it.name);
        if (name.empty()) return;
        selected_ = name;
        selected_custom_ = true;
        anims::RequestEdit(ctx, name);
        ctx.SetStatus(it.name + " copied to the project as " + name);
    }

    void DrawContextMenu(EditorContext& ctx, const AnimItem& it) {
        if (!ImGui::BeginPopupContextItem("##anim_ctx")) return;
        Select(ctx, it);
        ui::TextDim("%s", it.name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Add to selection", nullptr, false, !ctx.selection.empty() && ctx.ActiveRoot()))
            AddToSelection(ctx, it, it.trigger);
        if (ImGui::MenuItem(it.custom ? "Edit keyframes" : "Duplicate as custom", nullptr, false, ctx.has_project)) DuplicateOrEdit(ctx, it);
        if (ImGui::MenuItem("Copy name")) ImGui::SetClipboardText(it.name.c_str());
        ImGui::EndPopup();
    }
};

#else

class AnimationsTab final : public Tab {
public:
    const char* Title() const override { return "Animations"; }
    const char* IconName() const override { return "fa-wand-magic-sparkles"; }
    int Count(EditorContext&) override { return 0; }
    void Draw(EditorContext&) override {
        ui::EmptyState(Icon("fa-wand-magic-sparkles"), "Motion presets are not part of this build",
                       "imb_motion.h and imb_motion_presets.h were not found when the builder was compiled.");
    }
};

#endif

}  // namespace

std::unique_ptr<Tab> MakeAnimationsTab() { return std::unique_ptr<Tab>(new AnimationsTab()); }

}  // namespace imforge::builder::library
