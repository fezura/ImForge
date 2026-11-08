#include <cctype>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "imforge/imb_runtime.h"

#include "../design_apply.h"
#include "../project_io.h"
#include "../ui/fonts_state.h"
#include "library_shared.h"
#include "theme_state.h"

namespace fs = std::filesystem;

namespace imforge::builder::library {

namespace {

const char* const kModeLabels[] = {"Theme only", "Theme + style classes", "Everything"};
const char* const kModeHints[] = {
    "Adds the design's theme and makes it active.",
    "Theme plus its style classes (accent-button, card, muted...), merged by name.",
    "Theme, classes, fonts, window fx and default motions on windows, buttons and lists."};
const unsigned kModes[] = {kApplyThemeOnly, kApplyThemeAndClasses, kApplyEverything};

struct DesignItem {
    std::string file, name, title, description, credit, group;
    std::vector<std::string> tags;
    Json json;
    Theme theme;
    ImU32 swatches[5] = {0, 0, 0, 0, 0};
    ImU32 accent = 0, accent_text = 0;
    float shadow = 0.0f, glow = 0.0f;
};

ImU32 ColorFrom(const Json& j, ImU32 def) {
    bool ok = false;
    Vec4 c = ParseColor(j.AsString(), &ok);
    return ok ? ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w)) : def;
}

ImU32 ThemeColor(const Theme& t, const char* name, ImU32 def) {
    for (const auto& kv : t.colors)
        if (kv.first == name) return ImGui::ColorConvertFloat4ToU32(ImVec4(kv.second.x, kv.second.y, kv.second.z, kv.second.w));
    return def;
}

float Luma(ImU32 c) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    return v.x * 0.299f + v.y * 0.587f + v.z * 0.114f;
}

ImU32 WithA(ImU32 c, float a) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

bool LoadDesign(const std::string& path, DesignItem& out) {
    Json j;
    if (!ReadJsonFile(path, j)) return false;
    Theme t;
    if (!ThemeFromDesign(j, t)) return false;
    out.file = path;
    out.json = j;
    out.theme = t;
    out.name = DesignName(j);
    out.title = DesignTitle(j);
    out.description = j["description"].AsString();
    out.credit = j["credit"].AsString();
    for (const Json& tag : j["tags"].arr)
        if (tag.IsString()) out.tags.push_back(tag.AsString());
    const ImU32 fallback = ThemeColor(t, "ButtonActive", ThemeColor(t, "CheckMark", ToU32(Colors().accent)));
    const Json& ws = j["widgetStyle"];
    out.accent = ColorFrom(ws["accent"], fallback);
    const Json& cls = j["styleClasses"]["accent-button"]["colors"];
    if (cls.IsObject()) {
        out.accent = ColorFrom(cls["Button"], out.accent);
        out.accent_text = ColorFrom(cls["Text"], 0);
    }
    out.swatches[0] = out.accent;
    out.swatches[1] = ColorFrom(ws["accent2"], ThemeColor(t, "PlotHistogram", out.accent));
    out.swatches[2] = ColorFrom(ws["success"], IM_COL32(76, 199, 120, 255));
    out.swatches[3] = ColorFrom(ws["warning"], IM_COL32(242, 178, 64, 255));
    out.swatches[4] = ColorFrom(ws["danger"], IM_COL32(235, 84, 84, 255));
    out.shadow = (float)j["fx"]["windowShadow"].AsDouble(0.0);
    out.glow = (float)j["fx"]["accentGlow"].AsDouble(0.0);
    std::string group = j["category"].AsString();
    if (group.empty()) group = Luma(ThemeColor(t, "WindowBg", IM_COL32(30, 30, 30, 255))) > 0.5f ? "light" : "dark";
    out.group = group;
    return true;
}

// a tiny fake window in the design's colors and (scaled) vars, drawn straight into the draw list
void DrawMiniWindow(ImDrawList* dl, const DesignItem& d, const ImVec2& mn, const ImVec2& mx, float s) {
    const float W = mx.x - mn.x;
    const float k = std::max(0.45f, std::min(1.0f, W / (280.0f * s)));
    StyleScope scope = PushScaledStyle(&d.theme, k * s);
    const ImGuiStyle& st = ImGui::GetStyle();
    ImFont* font = ImGui::GetFont();
    const float fs = std::max(9.0f, std::floor(ImGui::GetFontSize() * k));
    const float fh = std::floor(fs + st.FramePadding.y * 2.0f);
    const float wr = st.WindowRounding, fr = st.FrameRounding;
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 text_dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    if (d.shadow > 0.0f) imb::Shadow(dl, mn, mx, wr, IM_COL32(0, 0, 0, (int)(170 * std::min(1.0f, d.shadow))), 16.0f * s, ImVec2(0, 4.0f * s));
    dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_WindowBg), wr);
    const float th = fh;
    dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + th), ImGui::GetColorU32(ImGuiCol_TitleBgActive), wr, ImDrawFlags_RoundCornersTop);
    {
        const float tri = std::floor(fs * 0.32f), cx = mn.x + st.FramePadding.x + tri, cy = mn.y + th * 0.5f;
        dl->AddTriangleFilled(ImVec2(cx - tri, cy - tri * 0.55f), ImVec2(cx + tri, cy - tri * 0.55f), ImVec2(cx, cy + tri * 0.65f), text);
        const float tx = cx + tri + st.ItemInnerSpacing.x + 2.0f;
        TextEllipsis(dl, font, fs, ImVec2(tx, mn.y + std::floor((th - fs) * 0.5f)), mx.x - tx - fs * 1.4f, text, d.title.c_str());
        const float xr = std::floor(fs * 0.22f), xc = mx.x - st.FramePadding.x - xr;
        dl->AddLine(ImVec2(xc - xr, cy - xr), ImVec2(xc + xr, cy + xr), text_dim, std::max(1.0f, fs * 0.09f));
        dl->AddLine(ImVec2(xc - xr, cy + xr), ImVec2(xc + xr, cy - xr), text_dim, std::max(1.0f, fs * 0.09f));
    }
    if (st.WindowBorderSize > 0.0f) dl->AddRect(mn, mx, ImGui::GetColorU32(ImGuiCol_Border), wr, 0, std::max(1.0f, st.WindowBorderSize));

    const float px = std::floor(st.WindowPadding.x), gap = std::floor(std::max(3.0f, st.ItemSpacing.y));
    float x = mn.x + px, y = mn.y + th + std::floor(st.WindowPadding.y);
    const float right = mx.x - px;
    dl->PushClipRect(ImVec2(mn.x, mn.y + th), mx, true);

    // heading + toggle
    dl->AddText(font, fs, ImVec2(x, y + std::floor((fh - fs) * 0.5f)), text, "Settings");
    {
        const float h = std::floor(fh * 0.72f), w = std::floor(h * 1.8f);
        const ImVec2 a(right - w, y + std::floor((fh - h) * 0.5f));
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), d.accent, h * 0.5f);
        dl->AddCircleFilled(ImVec2(a.x + w - h * 0.5f, a.y + h * 0.5f), h * 0.5f - std::max(1.5f, h * 0.12f), IM_COL32(255, 255, 255, 255), 16);
    }
    y += fh + gap;
    // checkbox
    if (y + fh <= mx.y) {
        const float b = fh;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + b, y + b), ImGui::GetColorU32(ImGuiCol_FrameBg), fr);
        const float pad = std::max(2.0f, std::floor(b / 5.0f)), sz = b - pad * 2.0f;
        const ImVec2 o(x + pad, y + pad);
        const float t = std::max(1.5f, sz / 5.0f);
        dl->PathLineTo(ImVec2(o.x + sz * 0.10f, o.y + sz * 0.52f));
        dl->PathLineTo(ImVec2(o.x + sz * 0.40f, o.y + sz * 0.80f));
        dl->PathLineTo(ImVec2(o.x + sz * 0.92f, o.y + sz * 0.18f));
        dl->PathStroke(ImGui::GetColorU32(ImGuiCol_CheckMark), 0, t);
        dl->AddText(font, fs, ImVec2(x + b + st.ItemInnerSpacing.x, y + std::floor((fh - fs) * 0.5f)), text, "Enabled");
        dl->AddText(font, fs, ImVec2(right - font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "on").x, y + std::floor((fh - fs) * 0.5f)), text_dim, "on");
        y += fh + gap;
    }
    // slider
    if (y + fh <= mx.y) {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(right, y + fh), ImGui::GetColorU32(ImGuiCol_FrameBg), fr);
        const float gw = std::max(6.0f, std::floor(st.GrabMinSize)), gp = std::max(2.0f, std::floor(fh * 0.12f));
        const float gx = x + gp + (right - x - gp * 2.0f - gw) * 0.62f;
        dl->AddRectFilled(ImVec2(gx, y + gp), ImVec2(gx + gw, y + fh - gp), ImGui::GetColorU32(ImGuiCol_SliderGrab),
                          std::min(st.GrabRounding, fr));
        const char* val = "0.62";
        const float vw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, val).x;
        dl->AddText(font, fs, ImVec2(std::floor((x + right - vw) * 0.5f), y + std::floor((fh - fs) * 0.5f)), text, val);
        y += fh + gap;
    }
    // buttons
    if (y + fh <= mx.y) {
        const char* a = "Apply";
        const char* b = "Cancel";
        const float aw = std::floor(font->CalcTextSizeA(fs, FLT_MAX, 0.0f, a).x + st.FramePadding.x * 2.0f);
        const float bw = std::floor(font->CalcTextSizeA(fs, FLT_MAX, 0.0f, b).x + st.FramePadding.x * 2.0f);
        const ImVec2 a0(x, y), a1(x + aw, y + fh);
        if (d.glow > 0.0f) imb::Glow(dl, a0, a1, fr, WithA(d.accent, std::min(1.0f, d.glow)), 10.0f * k * s);
        dl->AddRectFilled(a0, a1, d.accent, fr);
        const ImU32 at = d.accent_text ? d.accent_text : (Luma(d.accent) > 0.62f ? IM_COL32(20, 20, 24, 255) : IM_COL32(255, 255, 255, 255));
        dl->AddText(font, fs, ImVec2(x + st.FramePadding.x, y + std::floor((fh - fs) * 0.5f)), at, a);
        const float bx = x + aw + std::floor(st.ItemSpacing.x);
        if (bx + bw <= right) {
            dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw, y + fh), ImGui::GetColorU32(ImGuiCol_Button), fr);
            if (st.FrameBorderSize > 0.0f)
                dl->AddRect(ImVec2(bx, y), ImVec2(bx + bw, y + fh), ImGui::GetColorU32(ImGuiCol_Border), fr, 0, st.FrameBorderSize);
            dl->AddText(font, fs, ImVec2(bx + st.FramePadding.x, y + std::floor((fh - fs) * 0.5f)), text, b);
        }
    }
    dl->PopClipRect();
    PopStyleScope(scope);
}

class DesignsTab final : public Tab {
public:
    ~DesignsTab() override { StopPreview(); }
    const char* Title() const override { return "Designs"; }
    const char* IconName() const override { return "fa-palette"; }
    int Count(EditorContext&) override {
        Load();
        return (int)items_.size();
    }
    void OnProjectChanged(EditorContext&) override { StopPreview(); }

    bool Command(EditorContext& ctx, const std::string& verb, const std::string& arg) override {
        Load();
        if (verb == "search") {
            search_ = arg;
            return true;
        }
        if (verb == "select") {
            for (const DesignItem& d : items_)
                if (d.name == arg || (ContainsCI(d.title, arg) && d.title.size() == arg.size())) {
                    Select(d);
                    return true;
                }
            return false;
        }
        const DesignItem* sel = Selected();
        if (!sel) return false;
        if (verb == "preview") {
            if (arg == "off") StopPreview();
            else StartPreview(*sel);
            return true;
        }
        if (verb == "apply") {
            if (arg == "theme") mode_ = 0;
            else if (arg == "classes") mode_ = 1;
            else if (arg == "everything") mode_ = 2;
            else if (!arg.empty()) return false;
            return Apply(ctx, *sel);
        }
        return false;
    }

    void Draw(EditorContext& ctx) override {
        Load();
        FilterBar(ctx, "Search designs", search_, tags_, tag_);
        if (!groups_.empty()) ChipRow("##design_chips", groups_, group_, (int)items_.size());
        std::vector<const DesignItem*> shown;
        for (const DesignItem& d : items_)
            if (Matches(d)) shown.push_back(&d);
        const float s = Dpi(ctx);
        const float footer = std::floor(14.0f * s) + ImGui::GetTextLineHeight() * 2.0f + std::floor(1.0f * s);
        const char* hint = dir_.empty() ? "No presets/designs folder was found next to the builder or in the source tree."
                                        : "Try another search, category or tag.";
        CardGrid(ctx, "##design_grid", (int)shown.size(), 190.0f, 0.66f, footer, ActionBarHeight(), "fa-palette",
                 items_.empty() ? "No designs found" : "No design matches", hint, [&](const Card& card) {
                     const DesignItem& d = *shown[(size_t)card.index];
                     const bool selected = d.name == selected_;
                     if (card.tooltip) {
                         std::string text = d.description;
                         if (!d.tags.empty()) {
                             text += "\n";
                             for (const std::string& t : d.tags) text += " #" + t;
                         }
                         if (!d.credit.empty()) text += "\nCredit: " + d.credit;
                         WrappedTooltip(d.title.c_str(), text.c_str());
                     }
                     if (card.clicked) Select(d);
                     if (card.double_clicked) Apply(ctx, d);
                     DrawCard(ctx, card, d, selected);
                 });
        DrawActionBar(ctx);
    }

private:
    std::vector<DesignItem> items_;
    Counts groups_, tags_;
    bool loaded_ = false;
    std::string dir_;
    std::string search_, group_, tag_, selected_;
    int mode_ = 1;
    bool preview_ = false;
    Theme preview_theme_;

    bool Matches(const DesignItem& d) const {
        if (!group_.empty() && d.group != group_) return false;
        if (!tag_.empty() && std::find(d.tags.begin(), d.tags.end(), tag_) == d.tags.end()) return false;
        if (search_.empty()) return true;
        if (ContainsCI(d.title, search_) || ContainsCI(d.name, search_) || ContainsCI(d.description, search_)) return true;
        for (const std::string& t : d.tags)
            if (ContainsCI(t, search_)) return true;
        return false;
    }

    void Load() {
        if (loaded_) return;
        loaded_ = true;
        dir_ = FindDataDir("presets/designs");
        if (dir_.empty()) return;
        std::error_code ec;
        std::vector<std::string> files;
        for (fs::directory_iterator it(fs::u8path(dir_), ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = it->path().filename().u8string();
            if (n.size() > 14 && n.compare(n.size() - 14, 14, ".imdesign.json") == 0) files.push_back(it->path().u8string());
        }
        std::sort(files.begin(), files.end());
        for (const std::string& f : files) {
            DesignItem d;
            if (LoadDesign(f, d)) items_.push_back(std::move(d));
        }
        for (const char* g : {"dark", "light"}) groups_.emplace_back(g, 0);
        for (const DesignItem& d : items_) {
            AddCount(groups_, d.group);
            for (const std::string& t : d.tags) AddCount(tags_, t);
        }
        groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [](const std::pair<std::string, int>& g) { return g.second <= 0; }),
                      groups_.end());
        std::sort(tags_.begin(), tags_.end(), [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
    }

    const DesignItem* Selected() const {
        for (const DesignItem& d : items_)
            if (d.name == selected_) return &d;
        return nullptr;
    }

    void Select(const DesignItem& d) {
        selected_ = d.name;
        if (preview_) StartPreview(d);
    }

    void StartPreview(const DesignItem& d) {
        preview_theme_ = d.theme;
        preview_ = true;
        GetThemePreviewState().override_theme = &preview_theme_;
    }

    void StopPreview() {
        if (GetThemePreviewState().override_theme == &preview_theme_) GetThemePreviewState().override_theme = nullptr;
        preview_ = false;
    }

    bool Apply(EditorContext& ctx, const DesignItem& d) {
        selected_ = d.name;
        if (!ctx.has_project) {
            ctx.SetStatus("Open a project to apply a design");
            return false;
        }
        const unsigned mode = kModes[mode_ < 0 || mode_ > 2 ? 1 : mode_];
        DesignApplyResult r;
        Error err;
        bool ok = false;
        const std::string label = "Apply design " + d.title;
        ctx.EditProject(label.c_str(), [&] { ok = ApplyDesign(ctx.project, d.json, mode, &r, &err); });
        if (!ok) {
            ctx.Log(LogLevel::Error, "design %s: %s", d.title.c_str(), err.message.c_str());
            ctx.SetStatus("Could not apply " + d.title + ": " + err.message);
            return false;
        }
        for (const std::string& w : r.warnings) ctx.Log(LogLevel::Warning, "design %s: %s", d.title.c_str(), w.c_str());
        if (r.fonts > 0) FontsDirtyFlag() = true;
        std::string msg = "Applied " + d.title + ": theme";
        if (r.classes) msg += ", " + std::to_string(r.classes) + " classes";
        if (r.fonts) msg += ", " + std::to_string(r.fonts) + " fonts";
        if (r.fx) msg += ", " + std::to_string(r.fx) + " effects";
        if (r.motions) msg += ", " + std::to_string(r.motions) + " motions";
        ctx.SetStatus(msg + " (Ctrl+Z undoes it)");
        StopPreview();
        return true;
    }

    void DrawCard(EditorContext& ctx, const Card& card, const DesignItem& d, bool selected) {
        const BuilderColors& c = Colors();
        const float s = Dpi(ctx);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        CardBackground(dl, ctx, card, selected);
        PreviewInset(dl, ctx, card, ToU32(c.bg_window));
        const ImVec2 pmn(card.pmin.x + 1.0f, card.pmin.y + 1.0f), pmx(card.pmax.x - 1.0f, card.pmax.y);
        dl->PushClipRect(pmn, pmx, true);
        const float pw = pmx.x - pmn.x, ph = pmx.y - pmn.y;
        imb::RadialGradient(dl, ImVec2(pmn.x + pw * 0.2f, pmn.y + ph * 0.15f), pw * 0.75f, WithA(d.accent, 0.38f), WithA(d.accent, 0.0f));
        imb::RadialGradient(dl, ImVec2(pmx.x - pw * 0.1f, pmx.y), pw * 0.55f, WithA(d.swatches[1], 0.22f), WithA(d.swatches[1], 0.0f));
        const float m = std::floor(std::max(10.0f * s, pw * 0.08f));
        DrawMiniWindow(dl, d, ImVec2(pmn.x + m, pmn.y + m), ImVec2(pmx.x - m, pmx.y - std::floor(m * 0.6f)), s);
        dl->PopClipRect();
        if (preview_ && selected) CornerBadge(dl, ctx, card, "on canvas", ToU32(c.success));

        const float pad = std::floor(10.0f * s);
        const float r = std::floor(4.0f * s + 0.5f), sw_gap = std::floor(3.0f * s);
        const float sw_w = 5.0f * (r * 2.0f) + 4.0f * sw_gap;
        std::string sub = d.group;
        if (!sub.empty()) sub[0] = (char)std::toupper((unsigned char)sub[0]);
        int extra = 0;
        for (const std::string& t : d.tags)
            if (t != d.group && extra++ < 2) sub += ", " + t;
        CardFooter(dl, ctx, card, d.title.c_str(), nullptr, selected);
        const float line = ImGui::GetTextLineHeight();
        const float y = card.pmax.y + std::floor(7.0f * s) + line + std::floor(1.0f * s);
        TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(card.min.x + pad, y), card.max.x - card.min.x - pad * 3.0f - sw_w,
                     ToU32(c.text_dim), sub.c_str());
        float sx = card.max.x - pad - sw_w + r;
        for (ImU32 col : d.swatches) {
            dl->AddCircleFilled(ImVec2(sx, std::floor(y + line * 0.5f) + 0.5f), r, col, 16);
            sx += r * 2.0f + sw_gap;
        }
    }

    void DrawActionBar(EditorContext& ctx) {
        const BuilderColors& c = Colors();
        const ImGuiStyle& st = ImGui::GetStyle();
        BeginActionBar();
        const DesignItem* sel = Selected();
        const float combo_w = std::floor(ImGui::GetFontSize() * 11.0f);
        const float apply_w = ui::ButtonWidth("Apply");
        const float toggle_w = ImGui::GetFrameHeight() * 0.8f * 1.8f + st.ItemInnerSpacing.x + ImGui::CalcTextSize("Preview on canvas").x;
        const float right_w = sel ? combo_w + apply_w + toggle_w + st.ItemSpacing.x * 4.0f : 0.0f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float left_w = ImGui::GetContentRegionAvail().x - right_w - st.ItemSpacing.x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float ty = p.y + st.FramePadding.y;
        if (sel) {
            TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x, ty), left_w, ToU32(c.text), sel->title.c_str());
            const float dx = p.x + ImGui::CalcTextSize(sel->title.c_str()).x + st.ItemSpacing.x * 1.5f;
            const char* info = preview_ ? "Previewing on the canvas, the project is unchanged" : kModeHints[mode_];
            if (dx < p.x + left_w - 20.0f)
                TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(dx, ty), p.x + left_w - dx,
                             ToU32(preview_ ? c.success : c.text_dim), info);
        } else {
            TextEllipsis(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x, ty), left_w, ToU32(c.text_dim),
                         "Click a design to pick it, double-click to apply. Every apply is one undo step.");
        }
        if (!sel) {
            EndActionBar();
            return;
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + ImGui::GetContentRegionAvail().x - right_w + st.ItemSpacing.x, p.y));
        bool on = preview_;
        const float toggle_y = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(toggle_y + std::floor(ImGui::GetFrameHeight() * 0.1f));
        if (ui::ToggleButton("##design_preview", &on)) {
            if (on) StartPreview(*sel);
            else StopPreview();
        }
        ui::Tooltip("Show this design's theme on the canvas without changing the project");
        ImGui::SameLine(0, st.ItemInnerSpacing.x);
        ImGui::SetCursorPosY(toggle_y);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Preview on canvas");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(combo_w);
        ImGui::Combo("##design_mode", &mode_, kModeLabels, 3);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", kModeHints[mode_]);
        ImGui::SameLine();
        if (!ctx.has_project) ImGui::BeginDisabled();
        if (ui::AccentButton("Apply")) Apply(ctx, *sel);
        if (!ctx.has_project) ImGui::EndDisabled();
        EndActionBar();
    }
};

}  // namespace

std::unique_ptr<Tab> MakeDesignsTab() { return std::unique_ptr<Tab>(new DesignsTab()); }

}  // namespace imforge::builder::library
