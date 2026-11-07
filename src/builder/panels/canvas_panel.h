#pragma once

#include <map>
#include <string>
#include <vector>

#include "imgui.h"

#include "../editor_context.h"
#include "../ui/theme_apply.h"

#if IMFORGE_HAS_INTERP

namespace imforge::builder {

struct CanvasItem {
    const Node* node = nullptr;
    std::string id;
    ImVec2 min, max;
    ImVec2 emin, emax;
    bool hovered = false;
    int depth = 0;
};

struct DropTarget {
    enum class Kind { None, Line, Cell, Free, Empty };
    Kind kind = Kind::None;
    std::string parent_id;
    int index = -1;
    ImVec2 a, b;
    ImVec2 rect_min, rect_max;
    Vec2 offset;
    bool Valid() const { return kind != Kind::None; }
};

class CanvasPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Canvas"; }
    const char* DefaultDock() const override { return "center"; }
    const char* Icon() const override;
    void Draw(EditorContext& ctx) override;
    void OnProjectChanged(EditorContext& ctx) override;

private:
    static constexpr float kMinZoom = 0.5f, kMaxZoom = 2.0f;
    static constexpr float kMargin = 48.0f;
    static constexpr float kGridStep = 8.0f;
    static constexpr float kSnapStep = 4.0f;

    void DrawToolbar(EditorContext& ctx, const Component* comp);
    void DrawViewport(EditorContext& ctx, const Panel& panel, Node& root, PanelInstance& inst);
    void DrawFrameChrome(ImDrawList* dl, const ImVec2& frame_min, const ImVec2& frame_max, bool title_bar);
    void HandleZoom(const ImVec2& region_min, const ImVec2& region_max);
    void HandleKeyboard(EditorContext& ctx, Node& root);
    void ForwardDiagnostics(EditorContext& ctx);
    void UpdateAnimationClock();
    void ResetFrameData();

    void IndexTree(const Node& node, const Node* parent, int depth);
    void FinalizeItems(const Node& root);
    const CanvasItem* Item(const std::string& id) const;
    const Node* ParentOf(const std::string& id) const;
    bool InCanvasContainer(const std::string& id) const;
    bool IsContainerNode(EditorContext& ctx, const Node& node) const;
    std::string ResolveHover() const;
    std::string EnclosingInstance(const ImVec2& pos) const;
    void ClipRectFor(const std::string& id, ImVec2& mn, ImVec2& mx) const;
    void DrawOverlays(EditorContext& ctx, ImDrawList* dl);
    void DrawSelectionHandles(EditorContext& ctx, Node& root);
    void DrawContextMenu(EditorContext& ctx, Node& root);
    void NudgeOrStep(EditorContext& ctx, Node& root, int dx, int dy, bool big);
    void SetCanvasOffset(EditorContext& ctx, Node& node, const Vec2& offset, const char* label);

    void HandleDragDrop(EditorContext& ctx, Node& root, const ImVec2& region_min, const ImVec2& region_max);
    DropTarget ComputeDropTarget(EditorContext& ctx, const Node& root, const std::string& type,
                                 const std::string& moving_id, const ImVec2& mouse) const;
    void DrawDropMarker(ImDrawList* dl, const DropTarget& t) const;
    void ApplyDrop(EditorContext& ctx, Node& root, const DropTarget& t, bool is_new, const std::string& payload);

    const Panel* ResolvePanel(EditorContext& ctx, std::string& instance_key);
    void BuildComponentPreview(EditorContext& ctx, const Component& comp);
    Json MockParamValue(const Component& comp, const Param& p);
    void DrawMockParamsPopup(EditorContext& ctx, const Component& comp);
    StyleScope PushCanvasStyle(EditorContext& ctx, float S);
    ImFont* BaseFont(EditorContext& ctx) const;
    ImFont* PickFont(ImFont* base, float target_px) const;
    float DpiMultiplier() const;

    float zoom_ = 1.0f;
    int dpi_index_ = 0;
    bool show_grid_ = true;
    bool interactive_ = false;
    bool show_bounds_ = false;
    bool anim_playing_ = true;
    int anim_speed_index_ = 2;
    unsigned last_anim_gen_ = 0;
    bool fit_requested_ = false;

    std::vector<CanvasItem> items_;
    std::map<std::string, size_t> item_index_;
    std::map<std::string, const Node*> parents_;
    std::map<std::string, int> depths_;
    ImVec2 frame_min_, frame_max_;
    ImVec2 clip_min_, clip_max_;
    ImVec2 viewport_size_;
    ImVec2 canvas_pad_;
    float scale_ = 1.0f;
    float title_bar_h_ = 0.0f;
    ImU32 title_fg_ = 0, title_btn_ = 0;
    std::string hovered_id_;
    bool canvas_hovered_ = false;
    bool click_on_item_ = false;
    bool deferred_instance_click_ = false;
    bool tree_changed_ = false;

    bool moving_ = false;
    std::map<std::string, Vec2> move_origins_;
    ImVec2 move_start_mouse_;

    std::string context_id_;
    bool open_context_ = false;

    Panel preview_panel_;
    std::map<std::string, std::map<std::string, Json>> mock_params_;
};

}  // namespace imforge::builder

#endif
