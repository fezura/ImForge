#include <cfloat>
#include <cstdio>
#include <cstring>
#include <map>

#include "../bundled_packs.h"
#include "../ui/tree_actions.h"
#include "../ui/widget_descriptions.h"
#include "library_shared.h"

namespace imforge::builder::library {

namespace {

struct WidgetItem {
    std::string type, pack, category, description, icon;
    Json preview;
    bool live = false, container = false;
};

ImU32 JsonColor(const Json& j, ImU32 def) {
    bool ok = false;
    Vec4 c = ParseColor(j.AsString(), &ok);
    return ok ? ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w)) : def;
}

// manifest "preview" shapes, fitted into the card
void DrawShapes(ImDrawList* dl, const Json& shapes, const ImVec2& mn, const ImVec2& mx, float s) {
    float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
    ImFont* font = ImGui::GetFont();
    const float base_fs = 13.0f;
    auto grow = [&](float ax, float ay, float bx, float by) {
        x0 = std::min(x0, ax);
        y0 = std::min(y0, ay);
        x1 = std::max(x1, bx);
        y1 = std::max(y1, by);
    };
    for (const Json& sh : shapes.arr) {
        const std::string kind = sh["shape"].AsString();
        const float x = (float)sh["x"].AsDouble(), y = (float)sh["y"].AsDouble();
        if (kind == "rect") grow(x, y, x + (float)sh["w"].AsDouble(), y + (float)sh["h"].AsDouble());
        else if (kind == "circle") {
            const float r = (float)sh["r"].AsDouble(4);
            grow(x - r, y - r, x + r, y + r);
        } else if (kind == "text") {
            const ImVec2 ts = font->CalcTextSizeA(base_fs, FLT_MAX, 0.0f, sh["text"].AsString().c_str());
            grow(x, y, x + ts.x, y + ts.y);
        }
    }
    if (x1 <= x0 || y1 <= y0) return;
    const float pad = 12.0f * s;
    const float k = std::min(2.0f * s, std::min((mx.x - mn.x - pad * 2.0f) / (x1 - x0), (mx.y - mn.y - pad * 2.0f) / (y1 - y0)));
    const ImVec2 o(std::floor((mn.x + mx.x - (x1 - x0) * k) * 0.5f - x0 * k), std::floor((mn.y + mx.y - (y1 - y0) * k) * 0.5f - y0 * k));
    for (const Json& sh : shapes.arr) {
        const std::string kind = sh["shape"].AsString();
        const ImVec2 p(o.x + (float)sh["x"].AsDouble() * k, o.y + (float)sh["y"].AsDouble() * k);
        if (kind == "rect") {
            const ImVec2 q(p.x + (float)sh["w"].AsDouble() * k, p.y + (float)sh["h"].AsDouble() * k);
            const float r = (float)sh["rounding"].AsDouble() * k;
            dl->AddRectFilled(p, q, JsonColor(sh["fill"], 0), r);
            if (sh["stroke"].IsString()) dl->AddRect(p, q, JsonColor(sh["stroke"], 0), r, 0, (float)sh["thickness"].AsDouble(1) * k);
        } else if (kind == "circle") {
            const float r = (float)sh["r"].AsDouble(4) * k;
            dl->AddCircleFilled(p, r, JsonColor(sh["fill"], 0), 24);
            if (sh["stroke"].IsString()) dl->AddCircle(p, r, JsonColor(sh["stroke"], 0), 24, (float)sh["thickness"].AsDouble(1) * k);
        } else if (kind == "text") {
            dl->AddText(font, base_fs * k, p, JsonColor(sh["color"], IM_COL32(230, 230, 230, 255)), sh["text"].AsString().c_str());
        }
    }
}

#if IMFORGE_HAS_INTERP
struct LivePreview {
    Panel panel;
    PanelInstance inst;
    ImVec2 size = ImVec2(0, 0);
    bool measured = false;
};
#endif

class WidgetsTab final : public Tab {
public:
    const char* Title() const override { return "Widgets"; }
    const char* IconName() const override { return "fa-shapes"; }
    int Count(EditorContext& ctx) override {
        Refresh(ctx);
        return (int)items_.size();
    }
    void OnProjectChanged(EditorContext&) override {
        catalog_size_ = (size_t)-1;
#if IMFORGE_HAS_INTERP
        previews_.clear();
#endif
    }

    bool Command(EditorContext& ctx, const std::string& verb, const std::string& arg) override {
        Refresh(ctx);
        if (verb == "search") {
            search_ = arg;
            return true;
        }
        if (verb == "pack") {
            group_ = arg;
            return true;
        }
        if (verb == "add") return ctx.ActiveRoot() && ctx.catalog.FindCustom(arg.c_str()) && tree_actions::AddAtSelection(ctx, arg, std::string());
        return false;
    }

    void Draw(EditorContext& ctx) override {
        Refresh(ctx);
        FilterBar(ctx, "Search widgets", search_, Counts(), tag_);
        if (!groups_.empty()) ChipRow("##widget_chips", groups_, group_, (int)items_.size());
        std::vector<const WidgetItem*> shown;
        for (const WidgetItem& it : items_)
            if (Matches(it)) shown.push_back(&it);
        const float s = Dpi(ctx);
        const float footer = std::floor(14.0f * s) + ImGui::GetTextLineHeight() * 2.0f + std::floor(1.0f * s);
        int live_budget = 0;
        CardGrid(ctx, "##widget_grid", (int)shown.size(), 170.0f, 0.62f, footer, ActionBarHeight(), "fa-shapes",
                 items_.empty() ? "No widget packs loaded" : "No widget matches",
                 items_.empty() ? "Pack widgets show up here once their manifests are in the catalog." : "Try another search or pack.",
                 [&](const Card& card) {
                     const WidgetItem& it = *shown[(size_t)card.index];
                     if (ImGui::BeginDragDropSource()) {
                         std::string payload = tree_actions::NewPayloadFor(it.type, std::string());
                         ImGui::SetDragDropPayload(tree_actions::kPayloadNew, payload.c_str(), payload.size() + 1);
                         const char* icon = tree_actions::Icon(it.icon.c_str(), nullptr);
                         ImGui::Text("%s%s%s", icon ? icon : "", icon ? "  " : "", it.type.c_str());
                         ui::TextDim("Drop on the canvas or the hierarchy");
                         ImGui::EndDragDropSource();
                     } else if (card.tooltip) {
                         std::string text = it.description;
                         text += std::string(text.empty() ? "" : "\n") + it.pack + " pack" + (it.live ? "" : ", static preview");
                         WrappedTooltip(it.type.c_str(), text.c_str());
                     }
                     if (card.clicked) {
                         if (ctx.ActiveRoot()) tree_actions::AddAtSelection(ctx, it.type, std::string());
                         else ctx.SetStatus("Open a panel or component to add widgets");
                     }
                     DrawCard(ctx, card, it, live_budget);
                 });
        DrawActionBar(ctx);
    }

private:
    std::vector<WidgetItem> items_;
    Counts groups_;
    size_t catalog_size_ = (size_t)-1;
    std::string search_, group_, tag_;
#if IMFORGE_HAS_INTERP
    std::map<std::string, LivePreview> previews_;
#endif

    bool Matches(const WidgetItem& it) const {
        if (!group_.empty() && it.pack != group_) return false;
        return search_.empty() || ContainsCI(it.type, search_) || ContainsCI(it.description, search_) || ContainsCI(it.category, search_);
    }

    void Refresh(EditorContext& ctx) {
        if (catalog_size_ == ctx.catalog.custom.size()) return;
        catalog_size_ = ctx.catalog.custom.size();
        items_.clear();
        groups_.clear();
        for (const BundledPack& p : BundledPacks()) groups_.emplace_back(p.title, 0);
        for (const CustomWidgetDef& cw : ctx.catalog.custom) {
            WidgetItem it;
            it.type = cw.type;
            it.category = cw.category;
            it.icon = cw.icon.empty() ? "fa-puzzle-piece" : cw.icon;
            it.container = cw.container;
            const BundledPack* pack = BundledPackForWidget(cw.type);
            it.pack = pack ? pack->title : std::string("Project");
            const Json& m = cw.manifest;
            it.description = m["description"].IsString() ? m["description"].AsString() : m["hint"].AsString(WidgetDescription(cw.type.c_str()));
            if (m["preview"].IsArray()) it.preview = m["preview"];
#if IMFORGE_HAS_INTERP
            it.live = ctx.interp.FindWidgetFn(cw.type) != nullptr;
#endif
            items_.push_back(std::move(it));
            AddCount(groups_, items_.back().pack);
        }
        groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [](const std::pair<std::string, int>& g) { return g.second <= 0; }),
                      groups_.end());
#if IMFORGE_HAS_INTERP
        previews_.clear();
#endif
    }

    void DrawCard(EditorContext& ctx, const Card& card, const WidgetItem& it, int& live_budget) {
        const BuilderColors& c = Colors();
        const float s = Dpi(ctx);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        CardBackground(dl, ctx, card, false);
        const Theme* theme = ctx.has_project ? ctx.ActiveTheme() : nullptr;
        ImU32 bg = ToU32(c.bg_window);
        if (theme)
            for (const auto& kv : theme->colors)
                if (kv.first == "WindowBg") bg = ImGui::ColorConvertFloat4ToU32(ImVec4(kv.second.x, kv.second.y, kv.second.z, 1.0f));
        PreviewInset(dl, ctx, card, bg);
        const ImVec2 pmn(card.pmin.x + 1.0f, card.pmin.y + 1.0f), pmx(card.pmax.x - 1.0f, card.pmax.y);
        bool drawn = false;
#if IMFORGE_HAS_INTERP
        if (it.live && live_budget < kMaxAnimatedPreviews) {
            ++live_budget;
            DrawLive(ctx, it, theme, pmn, pmx);
            drawn = true;
        }
#else
        (void)live_budget;
#endif
        if (!drawn && it.preview.IsArray()) {
            dl->PushClipRect(pmn, pmx, true);
            DrawShapes(dl, it.preview, pmn, pmx, s);
            dl->PopClipRect();
            drawn = true;
        }
        if (!drawn) ui::IconDisc(tree_actions::Icon(it.icon.c_str(), "?"), ImVec2((pmn.x + pmx.x) * 0.5f, (pmn.y + pmx.y) * 0.5f),
                                 std::floor(18.0f * s), c.accent);
        if (!it.live) CornerBadge(dl, ctx, card, "static", ToU32(c.text_dim));
        else if (it.container) CornerBadge(dl, ctx, card, "container", ToU32(c.info));
        std::string sub = it.pack + (it.category.empty() || it.category == it.pack ? std::string() : ", " + it.category);
        CardFooter(dl, ctx, card, it.type.c_str(), sub.c_str(), false);
    }

#if IMFORGE_HAS_INTERP
    void DrawLive(EditorContext& ctx, const WidgetItem& it, const Theme* theme, const ImVec2& mn, const ImVec2& mx) {
        LivePreview& lp = previews_[it.type];
        if (lp.panel.root.type.empty()) {
            lp.panel.name = "LibraryPreview";
            lp.panel.root.id = "lib_root";
            lp.panel.root.type = "Window";
            Json flags = Json::Array();
            flags.Push(Json("NoTitleBar"));
            lp.panel.root.SetProp("flags", std::move(flags));
            Node n;
            n.id = "lib_widget";
            n.type = it.type;
            lp.panel.root.children.push_back(std::move(n));
        }
        const float s = Dpi(ctx);
        const ImVec2 area(mx.x - mn.x, mx.y - mn.y);
        const float pad = std::floor(12.0f * s);
        const ImVec2 inner(area.x - pad * 2.0f, area.y - pad * 2.0f);
        float k = 1.0f;
        if (lp.measured && lp.size.x > 0.0f && lp.size.y > 0.0f)
            k = std::max(0.5f, std::min(1.0f, std::min(inner.x / lp.size.x, inner.y / lp.size.y)));
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(mn);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        const ImGuiWindowFlags wf = ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
        const bool open = ImGui::BeginChild("##live", area, ImGuiChildFlags_AlwaysUseWindowPadding, wf);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (open) {
#if IMGUI_VERSION_NUM < 19200
            ImGui::SetWindowFontScale(k);
#endif
            StyleScope scope = PushScaledStyle(theme, s * k);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImVec2 off(pad, pad);
            if (lp.measured) {
                off.x = pad + std::max(0.0f, std::floor((inner.x - lp.size.x * k) * 0.5f));
                off.y = pad + std::max(0.0f, std::floor((inner.y - lp.size.y * k) * 0.5f));
            }
            ImGui::SetCursorPos(off);
            Interpreter& in = ctx.interp;
            InterpScope keep(in);
            ImVec2 got(0, 0);
            bool have = false;
            in.hooks.on_item = [&](const Node& n, const ImVec2& a, const ImVec2& b, bool) {
                if (n.id != "lib_widget") return;
                got = ImVec2(b.x - a.x, b.y - a.y);
                have = true;
            };
            InterpOptions opts;
            opts.design_mode = true;
            opts.dpi_scale = s * k;
            opts.run_handlers = false;
            opts.animations = true;
            opts.fx = true;
            static const std::vector<BindingRoot> kNoRoots;
            in.DrawPanel(lp.panel, lp.inst, kNoRoots, opts);
            ImGui::PopStyleVar();
            PopStyleScope(scope);
            if (have && got.x > 0.0f && got.y > 0.0f) {
                lp.size = ImVec2(got.x / k, got.y / k);
                lp.measured = true;
            }
        }
        ImGui::EndChild();
        ImGui::SetCursorScreenPos(cursor);
    }
#endif

    void DrawActionBar(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        BeginActionBar();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float ty = p.y + ImGui::GetStyle().FramePadding.y;
        std::string text;
        if (Node* sel = ctx.SelectedNode()) text = "Click a card to add it next to " + sel->id + " (" + sel->type + "), or drag it onto the canvas.";
        else if (ctx.ActiveRoot()) text = "Click a card to add it to the open document, or drag it onto the canvas or hierarchy.";
        else text = "Open a panel or component to add widgets.";
        TextEllipsis(ImGui::GetWindowDrawList(), ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x, ty), ImGui::GetContentRegionAvail().x,
                     ToU32(c.text_dim), text.c_str());
        ImGui::Dummy(ImVec2(1, ImGui::GetFrameHeight()));
        EndActionBar();
    }
};

}  // namespace

std::unique_ptr<Tab> MakeWidgetsTab() { return std::unique_ptr<Tab>(new WidgetsTab()); }

}  // namespace imforge::builder::library
