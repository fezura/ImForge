#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "imgui.h"

#include "../builder_style.h"
#include "../editor_context.h"
#include "../ui/editor_flags.h"
#include "../ui/tree_actions.h"
#include "../ui/tree_ops.h"
#include "../ui/ui_helpers.h"
#include "editor_panel.h"

namespace imforge::builder {

namespace {

const char* const kWrapTypes[] = {"VStack", "HStack", "Group", "Child", "Grid", "Canvas", "CollapsingHeader", "TabBar"};

bool ContainsCI(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
    });
    return it != hay.end();
}

std::string PayloadString(const ImGuiPayload* p) {
    if (!p || !p->Data || p->DataSize <= 0) return std::string();
    std::string s((const char*)p->Data, (size_t)p->DataSize);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

bool HasAnyChild(const Node& n) {
    if (!n.children.empty()) return true;
    for (const auto& s : n.slots)
        if (!s.second.empty()) return true;
    return false;
}

bool Key(ImGuiKeyChord chord, bool repeat = false) {
#if IMGUI_VERSION_NUM >= 19100
    return ImGui::Shortcut(chord, ImGuiInputFlags_RouteFocused | (repeat ? ImGuiInputFlags_Repeat : 0));
#else
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = (chord & ImGuiMod_Ctrl) != 0, shift = (chord & ImGuiMod_Shift) != 0, alt = (chord & ImGuiMod_Alt) != 0;
    if (io.KeyCtrl != ctrl || io.KeyShift != shift || io.KeyAlt != alt) return false;
    return ImGui::IsKeyPressed((ImGuiKey)(chord & ~ImGuiMod_Mask_), repeat);
#endif
}

class HierarchyPanel final : public EditorPanel {
public:
    const char* Name() const override { return "Hierarchy"; }
    const char* DefaultDock() const override { return "left"; }
    const char* Icon() const override { return tree_actions::Icon("fa-sitemap", nullptr); }
    void OnProjectChanged(EditorContext&) override {
        open_.clear();
        rename_id_.clear();
        anchor_.clear();
        last_selected_.clear();
    }

    void Draw(EditorContext& ctx) override {
        Node* root = ctx.ActiveRoot();
        force_open_ = force_open_next_;
        force_open_next_ = -1;
        DrawToolbar(ctx, root);
        if (!root) {
            ui::EmptyState(tree_actions::Icon("fa-sitemap", nullptr), "No document open",
                           "Open a panel or component to see its widget tree.");
            return;
        }
        const std::string& sel = ctx.SelectedId();
        if (sel != last_selected_) {
            last_selected_ = sel;
            if (!sel.empty()) {
                std::vector<std::string> path = tree::PathToNode(*root, sel);
                for (size_t i = 0; i + 1 < path.size(); ++i) expand_once_.insert(path[i]);
                scroll_to_ = sel;
            }
        }
        filtered_.clear();
        if (!filter_.empty()) BuildFilter(*root);
        rows_.clear();
        hovered_row_ = false;
        marker_.active = false;
        if (ImGui::BeginChild("hier_tree", ImVec2(0, 0), ImGuiChildFlags_None)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
            DrawNode(ctx, *root, *root, 0, false);
            ImGui::PopStyleVar();
            ApplyPendingDrop(ctx);
            DrawTail(ctx, *root);
            if (marker_.active) DrawMarker();
            DrawContextMenu(ctx, *root);
            HandleKeyboard(ctx, *root);
        }
        ImGui::EndChild();
        DrawConvertPopup(ctx);
        if (!hovered_row_ && !hover_set_.empty()) {
            if (ctx.hovered_id == hover_set_) ctx.hovered_id.clear();
            hover_set_.clear();
        }
        expand_once_.clear();
        last_rows_ = rows_;
    }

private:
    enum class Zone { None, Before, Into, After };
    struct Marker {
        bool active = false;
        Zone zone = Zone::None;
        ImVec2 a, b;
        ImU32 col = 0;
    };

    std::string filter_, rename_id_, rename_buf_;
    bool rename_focus_ = false;
    std::string anchor_, pending_click_, ctx_menu_id_, last_selected_, scroll_to_, hover_set_;
    std::vector<std::string> rows_, last_rows_;
    std::unordered_map<std::string, bool> open_, pending_open_;
    std::unordered_set<std::string> expand_once_;
    std::unordered_set<const Node*> filtered_;
    int force_open_ = -1, force_open_next_ = -1;
    bool hovered_row_ = false, open_convert_ = false;
    std::string convert_name_, convert_err_;
    std::vector<std::string> convert_ids_;
    Marker marker_;
    struct PendingDrop {
        bool active = false, is_new = false, after = false;
        std::string type, component, parent, anchor, open_id;
        std::vector<std::string> ids;
    } pending_drop_;

    void ApplyPendingDrop(EditorContext& ctx) {
        if (!pending_drop_.active) return;
        PendingDrop d = std::move(pending_drop_);
        pending_drop_ = PendingDrop();
        if (d.is_new) tree_actions::DropNew(ctx, d.type, d.component, d.parent, d.anchor, d.after);
        else tree_actions::DropNodes(ctx, d.ids, d.parent, d.anchor, d.after);
        if (!d.open_id.empty()) pending_open_[d.open_id] = true;
    }

    void DrawToolbar(EditorContext& ctx, Node* root) {
        if (ctx.ActivePanel() || ctx.ActiveComponent()) {
            const bool panel = ctx.ActivePanel() != nullptr;
            const char* icon = tree_actions::Icon(panel ? "fa-window-maximize" : "fa-cube", nullptr);
            if (icon) {
                ImGui::PushStyleColor(ImGuiCol_Text, Colors().accent);
                ImGui::TextUnformatted(icon);
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            ImGui::TextUnformatted(ctx.active_panel.c_str());
            ImGui::SameLine();
            ui::TextDim("%s", panel ? "panel" : "component");
        } else {
            ui::TextDim("No document");
        }
        float btn = ImGui::GetFrameHeight();
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - (btn + spacing) * 2.0f, 60.0f));
        ui::InputTextWithHintStd("##hier_filter", "Filter by id, type or name", filter_);
        ImGui::SameLine();
        if (ui::IconButton(tree_actions::Icon("fa-expand", "+"), "Expand all", ImVec2(btn, btn), root != nullptr))
            force_open_next_ = 1;
        ImGui::SameLine();
        if (ui::IconButton(tree_actions::Icon("fa-compress", "-"), "Collapse all", ImVec2(btn, btn), root != nullptr))
            force_open_next_ = 0;
        ImGui::Separator();
    }

    bool BuildFilter(const Node& n) {
        bool any = ContainsCI(n.id, filter_) || ContainsCI(n.type, filter_) || ContainsCI(n.name, filter_) ||
                   ContainsCI(n.component, filter_);
        for (const Node& c : n.children)
            if (BuildFilter(c)) any = true;
        for (const auto& s : n.slots)
            for (const Node& c : s.second)
                if (BuildFilter(c)) any = true;
        if (any) filtered_.insert(&n);
        return any;
    }

    void DrawNode(EditorContext& ctx, Node& root, Node& node, int depth, bool parent_locked) {
        if (!filter_.empty() && !filtered_.count(&node)) return;
        const bool is_root = &node == &root;
        const bool locked_sub = parent_locked || node.locked;
        const bool hidden = IsHiddenInEditor(node.id);
        const bool selected = ctx.IsSelected(node.id);
        const bool has_children = HasAnyChild(node);
        ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding;
        if (!has_children) node_flags |= ImGuiTreeNodeFlags_Leaf;
        if (selected) node_flags |= ImGuiTreeNodeFlags_Selected;
        if (is_root) node_flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (force_open_ >= 0) ImGui::SetNextItemOpen(force_open_ == 1);
        else if (auto it = pending_open_.find(node.id); it != pending_open_.end()) {
            ImGui::SetNextItemOpen(it->second);
            pending_open_.erase(it);
        } else if (expand_once_.count(node.id) || !filter_.empty()) {
            ImGui::SetNextItemOpen(true);
        }

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        bool node_open = ImGui::TreeNodeEx(node.id.c_str(), node_flags, "%s", "");
        node_open = node_open && has_children;
        open_[node.id] = node_open;
        rows_.push_back(node.id);
        const ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
        const float text_x = cursor.x + ImGui::GetTreeNodeToLabelSpacing();
        const ImVec2 text_pos(text_x, rmin.y + ImGui::GetStyle().FramePadding.y);
        const bool toggled = ImGui::IsItemToggledOpen();
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ctx.hovered_id = node.id;
            hover_set_ = node.id;
            hovered_row_ = true;
        }
        if (!toggled) HandleClick(ctx, node.id);
        if (!toggled && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !locked_sub) StartRename(node.id);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            if (!selected) {
                ctx.Select(node.id);
                anchor_ = node.id;
            }
            ctx_menu_id_ = node.id;
            ImGui::OpenPopup("hier_ctx");
        }
        if (!is_root && !locked_sub && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(tree_actions::kPayloadNode, node.id.c_str(), node.id.size() + 1);
            std::vector<std::string> ids = tree_actions::DraggedIds(ctx, node.id);
            const char* icon = tree_actions::WidgetIcon(ctx, node);
            if (ids.size() > 1) ImGui::Text("%s %s  +%d more", icon ? icon : "", node.id.c_str(), (int)ids.size() - 1);
            else ImGui::Text("%s %s  (%s)", icon ? icon : "", node.id.c_str(), node.type.c_str());
            ImGui::EndDragDropSource();
        }
        HandleDrop(ctx, root, node, rmin, rmax, text_x, is_root);
        if (rename_id_ == node.id) DrawRename(ctx, rmin, rmax, text_x);
        else DrawRowText(ctx, node, text_pos, rmin, rmax, hidden, node.locked, node_open, has_children);
        if (scroll_to_ == node.id) {
            ImGui::SetScrollHereY(0.5f);
            scroll_to_.clear();
        }
        if (!node_open) return;
        const float indent = ImGui::GetStyle().IndentSpacing;
        ImGui::Indent(indent);
        for (Node& c : node.children) DrawNode(ctx, root, c, depth + 1, locked_sub);
        for (auto& s : node.slots) {
            if (!filter_.empty()) {
                bool any = false;
                for (const Node& c : s.second) any = any || filtered_.count(&c) != 0;
                if (!any) continue;
            }
            ImGui::TextDisabled("%s slot: %s", tree_actions::Icon("fa-plug", ""), s.first.c_str());
            for (Node& c : s.second) DrawNode(ctx, root, c, depth + 1, locked_sub);
        }
        ImGui::Unindent(indent);
    }

    void DrawRowText(EditorContext& ctx, const Node& node, const ImVec2& text_pos, const ImVec2& rmin, const ImVec2& rmax,
                     bool hidden, bool locked, bool is_open, bool has_children) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const BuilderColors& c = Colors();
        const ImGuiStyle& st = ImGui::GetStyle();
        const ImU32 col_text = ToU32(hidden ? c.text_disabled : c.text);
        const ImU32 col_dim = ToU32(hidden ? c.text_disabled : c.text_dim);
        float right = rmax.x - st.FramePadding.x;
        std::string badges;
        if (locked) badges += tree_actions::Icon("fa-lock", "[locked]");
        if (hidden) badges += std::string(badges.empty() ? "" : " ") + tree_actions::Icon("fa-eye-slash", "[hidden]");
        if (!badges.empty()) {
            right -= ImGui::CalcTextSize(badges.c_str()).x;
            dl->AddText(ImVec2(right, text_pos.y), col_dim, badges.c_str());
            right -= st.ItemSpacing.x;
        }
        if (!is_open && has_children) {
            std::string count = "(" + std::to_string(node.children.size()) + ")";
            right -= ImGui::CalcTextSize(count.c_str()).x;
            dl->AddText(ImVec2(right, text_pos.y), col_dim, count.c_str());
            right -= st.ItemSpacing.x;
        }
        dl->PushClipRect(ImVec2(text_pos.x, rmin.y), ImVec2(std::max(right, text_pos.x), rmax.y), true);
        float x = text_pos.x;
        if (const char* icon = tree_actions::WidgetIcon(ctx, node)) {
            dl->AddText(ImVec2(x, text_pos.y), ToU32(hidden ? c.text_disabled : c.accent), icon);
            x += ImGui::CalcTextSize(icon).x + st.ItemInnerSpacing.x;
        }
        const std::string& head = node.type == "Component" && !node.component.empty() ? node.component : node.type;
        dl->AddText(ImVec2(x, text_pos.y), col_text, head.c_str());
        x += ImGui::CalcTextSize(head.c_str()).x + st.ItemSpacing.x;
        dl->AddText(ImVec2(x, text_pos.y), col_dim, node.id.c_str());
        x += ImGui::CalcTextSize(node.id.c_str()).x + st.ItemSpacing.x;
        if (!node.name.empty()) {
            std::string name = "\"" + node.name + "\"";
            dl->AddText(ImVec2(x, text_pos.y), col_dim, name.c_str());
        }
        dl->PopClipRect();
    }

    void DrawRename(EditorContext& ctx, const ImVec2& rmin, const ImVec2& rmax, float text_x) {
        ImGui::SetCursorScreenPos(ImVec2(text_x, rmin.y));
        ImGui::SetNextItemWidth(std::max(rmax.x - text_x - ImGui::GetStyle().FramePadding.x, 60.0f));
        if (rename_focus_) {
            ImGui::SetKeyboardFocusHere();
            rename_focus_ = false;
        }
        bool enter = ui::InputTextStd("##hier_rename", rename_buf_,
                                      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll |
                                          ImGuiInputTextFlags_CharsNoBlank);
        bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (enter || (ImGui::IsItemDeactivated() && !escape)) {
            if (!rename_buf_.empty() && rename_buf_ != rename_id_) ctx.RenameNode(rename_id_, rename_buf_);
            rename_id_.clear();
        } else if (ImGui::IsItemDeactivated()) {
            rename_id_.clear();
        }
    }

    void HandleClick(EditorContext& ctx, const std::string& id) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl) {
                if (ctx.IsSelected(id)) ctx.Deselect(id);
                else ctx.Select(id, true);
                anchor_ = id;
            } else if (io.KeyShift && !anchor_.empty()) {
                SelectRange(ctx, anchor_, id);
            } else if (!ctx.IsSelected(id)) {
                ctx.Select(id);
                anchor_ = id;
            } else {
                pending_click_ = id;
            }
        }
        if (pending_click_ == id && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            if (ImGui::IsItemHovered() && drag.x == 0.0f && drag.y == 0.0f) {
                ctx.Select(id);
                anchor_ = id;
            }
            pending_click_.clear();
        }
    }

    void SelectRange(EditorContext& ctx, const std::string& from, const std::string& to) {
        const std::vector<std::string>& rows = last_rows_.empty() ? rows_ : last_rows_;
        auto a = std::find(rows.begin(), rows.end(), from);
        auto b = std::find(rows.begin(), rows.end(), to);
        if (a == rows.end() || b == rows.end()) {
            ctx.Select(to);
            anchor_ = to;
            return;
        }
        if (a > b) std::swap(a, b);
        ctx.ClearSelection();
        for (auto it = a; it <= b; ++it) ctx.Select(*it, true);
    }

    void StartRename(const std::string& id) {
        rename_id_ = id;
        rename_buf_ = id;
        rename_focus_ = true;
    }

    void HandleDrop(EditorContext& ctx, Node& root, Node& node, const ImVec2& rmin, const ImVec2& rmax, float text_x,
                    bool is_root) {
        if (!ImGui::BeginDragDropTarget()) return;
        const ImGuiDragDropFlags f = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
        bool is_new = false;
        const ImGuiPayload* p = ImGui::AcceptDragDropPayload(tree_actions::kPayloadNode, f);
        if (!p) {
            p = ImGui::AcceptDragDropPayload(tree_actions::kPayloadNew, f);
            is_new = p != nullptr;
        }
        if (p) {
            std::string type, component;
            std::vector<std::string> ids;
            if (is_new) {
                tree_actions::ParseNewPayload(p, type, component);
            } else {
                ids = tree_actions::DraggedIds(ctx, PayloadString(p));
                if (const Node* n = ids.empty() ? nullptr : FindNode(root, ids.front())) type = n->type;
            }
            const float t = (ImGui::GetMousePos().y - rmin.y) / std::max(rmax.y - rmin.y, 1.0f);
            const bool can_into = tree::AcceptsChild(&ctx.catalog, node, type);
            Zone zone = Zone::Into;
            if (!is_root) {
                if (can_into) zone = t < 0.25f ? Zone::Before : (t > 0.75f ? Zone::After : Zone::Into);
                else zone = t < 0.5f ? Zone::Before : Zone::After;
            }
            std::string parent, anchor;
            bool after = zone == Zone::After, valid = true;
            if (zone == Zone::Into) {
                parent = node.id;
            } else {
                tree::NodeLocation loc;
                valid = tree::LocateNode(root, node.id, loc) && loc.list == &loc.parent->children;
                if (valid) {
                    parent = loc.parent->id;
                    anchor = node.id;
                }
            }
            if (valid) valid = is_new ? tree_actions::CanDropNew(ctx, type, parent) : tree_actions::CanDropNodes(ctx, ids, parent, anchor);
            SetMarker(zone, rmin, rmax, text_x, valid);
            if (!valid) ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
            if (p->IsDelivery() && valid) {
                pending_drop_ = PendingDrop{true, is_new, after, type, component, parent, anchor,
                                            zone == Zone::Into ? node.id : std::string(), ids};
            }
        }
        ImGui::EndDragDropTarget();
    }

    void SetMarker(Zone zone, const ImVec2& rmin, const ImVec2& rmax, float text_x, bool valid) {
        const BuilderColors& c = Colors();
        marker_.active = true;
        marker_.zone = zone;
        marker_.col = ToU32(valid ? c.accent : c.error);
        if (zone == Zone::Into) {
            marker_.a = rmin;
            marker_.b = rmax;
        } else {
            float y = zone == Zone::Before ? rmin.y : rmax.y;
            marker_.a = ImVec2(text_x - 4.0f, y);
            marker_.b = ImVec2(rmax.x, y);
        }
    }

    void DrawMarker() {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (marker_.zone == Zone::Into) {
            dl->AddRect(marker_.a, marker_.b, marker_.col, 2.0f, 0, 2.0f);
        } else {
            dl->AddLine(marker_.a, marker_.b, marker_.col, 2.0f);
            dl->AddCircleFilled(marker_.a, 3.0f, marker_.col);
        }
    }

    void DrawTail(EditorContext& ctx, Node& root) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 size(std::max(avail.x, 1.0f), std::max(avail.y, ImGui::GetFrameHeight()));
        ImGui::InvisibleButton("##hier_tail", size);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            ctx.ClearSelection();
            anchor_.clear();
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ctx_menu_id_ = root.id;
            ImGui::OpenPopup("hier_ctx");
        }
        if (!ImGui::BeginDragDropTarget()) return;
        const ImGuiDragDropFlags f = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
        bool is_new = false;
        const ImGuiPayload* p = ImGui::AcceptDragDropPayload(tree_actions::kPayloadNode, f);
        if (!p) {
            p = ImGui::AcceptDragDropPayload(tree_actions::kPayloadNew, f);
            is_new = p != nullptr;
        }
        if (p) {
            std::string type, component;
            std::vector<std::string> ids;
            bool valid;
            if (is_new) {
                tree_actions::ParseNewPayload(p, type, component);
                valid = tree_actions::CanDropNew(ctx, type, root.id);
            } else {
                ids = tree_actions::DraggedIds(ctx, PayloadString(p));
                valid = tree_actions::CanDropNodes(ctx, ids, root.id, "");
            }
            ImVec2 rmin = ImGui::GetItemRectMin(), rmax = ImGui::GetItemRectMax();
            SetMarker(Zone::Before, rmin, ImVec2(rmax.x, rmin.y), rmin.x + ImGui::GetTreeNodeToLabelSpacing(), valid);
            if (!valid) ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
            if (p->IsDelivery() && valid) {
                if (is_new) tree_actions::DropNew(ctx, type, component, root.id, "", false);
                else tree_actions::DropNodes(ctx, ids, root.id, "", false);
            }
        }
        ImGui::EndDragDropTarget();
    }

    void DrawContextMenu(EditorContext& ctx, Node& root) {
        if (!ImGui::BeginPopup("hier_ctx")) return;
        Node* node = FindNode(root, ctx_menu_id_);
        if (!node) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        const unsigned serial0 = ctx.edit_serial;
        const bool is_root = node == &root;
        const bool locked_sub = tree_actions::IsLockedOrInherited(root, node->id);
        std::vector<std::string> ids = tree::TopLevelIds(root, ctx.selection);
        if (ids.empty()) ids.push_back(node->id);
        const WidgetDef* def = ctx.catalog.Find(node->type.c_str());
        const bool container = def && def->container && node->type != "Component";
        if (ImGui::BeginMenu("Add child", container && !locked_sub)) {
            tree_actions::DrawAddMenu(ctx, node->id, -1);
            ImGui::EndMenu();
        }
        tree::NodeLocation loc;
        const bool located = !is_root && tree::LocateNode(root, node->id, loc);
        const bool sibling_ok = located && loc.list == &loc.parent->children &&
                                !tree_actions::IsLockedOrInherited(root, loc.parent->id);
        if (ImGui::BeginMenu("Add sibling", sibling_ok)) {
            tree_actions::DrawAddMenu(ctx, loc.parent->id, (int)loc.index + 1);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        bool acted = false;
        if (ImGui::BeginMenu("Wrap in", !is_root && !locked_sub)) {
            for (const char* t : kWrapTypes) {
                bool ok = tree::CanWrap(root, ids, t, &ctx.catalog);
                const char* label = std::strcmp(t, "TabBar") == 0 ? "TabBar + Tab" : t;
                if (ImGui::MenuItem(label, nullptr, false, ok)) {
                    tree_actions::WrapSelection(ctx, t);
                    acted = true;
                }
            }
            ImGui::EndMenu();
        }
        if (acted || ctx.edit_serial != serial0) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        if (ImGui::MenuItem("Unwrap", nullptr, false, !is_root && !locked_sub && tree::CanUnwrap(root, node->id, &ctx.catalog))) {
            tree_actions::UnwrapNode(ctx, node->id);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !is_root)) {
            ctx.DuplicateSelection();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        if (ImGui::MenuItem("Delete", "Del", false, !is_root && !locked_sub)) {
            ctx.DeleteSelection();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        if (ImGui::MenuItem("Rename id", "F2", false, !locked_sub)) StartRename(node->id);
        ImGui::Separator();
        if (ImGui::MenuItem(node->locked ? "Unlock" : "Lock")) tree_actions::SetLocked(ctx, ids, !node->locked);
        const bool hidden = IsHiddenInEditor(node->id);
        if (ImGui::MenuItem(hidden ? "Show in editor" : "Hide in editor"))
            for (const std::string& id : ids) SetHiddenInEditor(id, !hidden);
        ImGui::Separator();
        if (ImGui::MenuItem("Convert to component...", nullptr, false, !is_root && !locked_sub)) {
            convert_ids_ = ids;
            convert_name_ = tree_actions::SuggestComponentName(ctx, ids);
            convert_err_.clear();
            open_convert_ = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Expand all")) force_open_next_ = 1;
        if (ImGui::MenuItem("Collapse all")) force_open_next_ = 0;
        ImGui::EndPopup();
    }

    void DrawConvertPopup(EditorContext& ctx) {
        if (open_convert_) {
            ImGui::OpenPopup("Convert to component");
            open_convert_ = false;
        }
        ImGui::SetNextWindowSize(ImVec2(360.0f * ctx.dpi_scale, 0), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Convert to component", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
        ImGui::TextUnformatted("Component name");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ui::InputTextStd("##cname", convert_name_,
                                      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank);
        ui::TextDim("%d node%s will be replaced by an instance of the new component.", (int)convert_ids_.size(),
                    convert_ids_.size() == 1 ? "" : "s");
        if (!convert_err_.empty()) ImGui::TextColored(Colors().error, "%s", convert_err_.c_str());
        if (ui::AccentButton("Create") || enter) {
            std::string err;
            if (tree_actions::ConvertToComponent(ctx, convert_ids_, convert_name_, &err)) ImGui::CloseCurrentPopup();
            else convert_err_ = err;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    void HandleKeyboard(EditorContext& ctx, Node& root) {
        if (!rename_id_.empty() || ImGui::GetIO().WantTextInput || !ImGui::IsWindowFocused()) return;
        int cur = -1;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i] == ctx.SelectedId()) cur = (int)i;
        auto select_row = [&](int i, bool extend) {
            if (rows_.empty()) return;
            i = std::max(0, std::min(i, (int)rows_.size() - 1));
            const std::string id = rows_[(size_t)i];
            if (extend && !anchor_.empty()) {
                SelectRange(ctx, anchor_, id);
            } else {
                ctx.Select(id);
                anchor_ = id;
            }
            scroll_to_ = id;
        };
        if (Key(ImGuiKey_DownArrow, true)) select_row(cur + 1, false);
        if (Key(ImGuiKey_UpArrow, true)) select_row(cur < 0 ? 0 : cur - 1, false);
        if (Key(ImGuiMod_Shift | ImGuiKey_DownArrow, true)) select_row(cur + 1, true);
        if (Key(ImGuiMod_Shift | ImGuiKey_UpArrow, true)) select_row(cur < 0 ? 0 : cur - 1, true);
        if (Key(ImGuiKey_Home)) select_row(0, false);
        if (Key(ImGuiKey_End)) select_row((int)rows_.size() - 1, false);
        if (cur >= 0 && Key(ImGuiKey_LeftArrow, true)) {
            const std::string& id = rows_[(size_t)cur];
            if (open_[id]) {
                pending_open_[id] = false;
            } else {
                std::vector<std::string> path = tree::PathToNode(root, id);
                if (path.size() >= 2) select_row((int)(std::find(rows_.begin(), rows_.end(), path[path.size() - 2]) - rows_.begin()), false);
            }
        }
        if (cur >= 0 && Key(ImGuiKey_RightArrow, true)) {
            const std::string& id = rows_[(size_t)cur];
            const Node* n = FindNode(root, id);
            if (n && HasAnyChild(*n)) {
                if (!open_[id]) pending_open_[id] = true;
                else select_row(cur + 1, false);
            }
        }
        if (Key(ImGuiKey_Delete)) {
            bool blocked = false;
            for (const std::string& id : ctx.selection)
                blocked = blocked || id == root.id || tree_actions::IsLockedOrInherited(root, id);
            if (blocked) ctx.SetStatus("Selection contains the root or a locked node");
            else ctx.DeleteSelection();
        }
        if (Key(ImGuiKey_F2) && cur >= 0 && !tree_actions::IsLockedOrInherited(root, rows_[(size_t)cur]))
            StartRename(rows_[(size_t)cur]);
        if (Key(ImGuiMod_Ctrl | ImGuiKey_D)) ctx.DuplicateSelection();
        if (Key(ImGuiMod_Ctrl | ImGuiKey_UpArrow, true)) tree_actions::MoveSelection(ctx, -1);
        if (Key(ImGuiMod_Ctrl | ImGuiKey_DownArrow, true)) tree_actions::MoveSelection(ctx, 1);
        if (Key(ImGuiMod_Ctrl | ImGuiKey_A)) {
            ctx.ClearSelection();
            for (const std::string& id : rows_)
                if (id != root.id) ctx.Select(id, true);
        }
    }
};

}  // namespace

IMFORGE_REGISTER_PANEL(HierarchyPanel, 10);

}  // namespace imforge::builder
