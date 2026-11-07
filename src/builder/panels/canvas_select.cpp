#include "canvas_panel.h"

#if IMFORGE_HAS_INTERP

#include <algorithm>
#include <cmath>
#include <cstring>

#include "imgui_internal.h"

#include "../builder_style.h"
#include "../ui/canvas_overlay.h"
#include "../ui/tree_ops.h"

namespace imforge::builder {

namespace {

const char* const kWrapTypes[] = {"VStack", "HStack", "Group", "Child", "Canvas"};

float Area(const ImVec2& mn, const ImVec2& mx) { return ImMax(mx.x - mn.x, 0.0f) * ImMax(mx.y - mn.y, 0.0f); }

bool Contains(const ImVec2& mn, const ImVec2& mx, const ImVec2& p) {
    return p.x >= mn.x && p.y >= mn.y && p.x < mx.x && p.y < mx.y;
}

}  // namespace

void CanvasPanel::IndexTree(const Node& node, const Node* parent, int depth) {
    depths_[node.id] = depth;
    parents_[node.id] = parent;
    for (const Node& c : node.children) IndexTree(c, &node, depth + 1);
    for (const auto& s : node.slots)
        for (const Node& c : s.second) IndexTree(c, &node, depth + 1);
}

void CanvasPanel::FinalizeItems(const Node& root) {
    (void)root;
    std::vector<size_t> order(items_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return items_[a].depth > items_[b].depth; });
    for (size_t i : order) {
        CanvasItem& it = items_[i];
        const Node* parent = ParentOf(it.id);
        if (!parent) continue;
        auto pi = item_index_.find(parent->id);
        if (pi == item_index_.end()) continue;
        CanvasItem& p = items_[pi->second];
        p.emin = ImVec2(ImMin(p.emin.x, it.emin.x), ImMin(p.emin.y, it.emin.y));
        p.emax = ImVec2(ImMax(p.emax.x, it.emax.x), ImMax(p.emax.y, it.emax.y));
    }
}

const CanvasItem* CanvasPanel::Item(const std::string& id) const {
    auto it = item_index_.find(id);
    return it == item_index_.end() ? nullptr : &items_[it->second];
}

const Node* CanvasPanel::ParentOf(const std::string& id) const {
    auto it = parents_.find(id);
    return it == parents_.end() ? nullptr : it->second;
}

bool CanvasPanel::InCanvasContainer(const std::string& id) const {
    const Node* p = ParentOf(id);
    return p && p->type == "Canvas";
}

bool CanvasPanel::IsContainerNode(EditorContext& ctx, const Node& node) const {
    if (const WidgetDef* d = ctx.catalog.Find(node.type.c_str())) return d->container;
    return IsContainerType(node.type);
}

std::string CanvasPanel::ResolveHover() const {
    const CanvasItem* best = nullptr;
    for (const CanvasItem& it : items_) {
        if (!it.hovered) continue;
        if (!best || it.depth > best->depth || (it.depth == best->depth && Area(it.min, it.max) < Area(best->min, best->max)))
            best = &it;
    }
    return best ? best->id : std::string();
}

std::string CanvasPanel::EnclosingInstance(const ImVec2& pos) const {
    const CanvasItem* best = nullptr;
    for (const CanvasItem& it : items_) {
        if (!it.node || it.node->type != "Component" || !Contains(it.min, it.max, pos)) continue;
        if (!best || Area(it.min, it.max) < Area(best->min, best->max)) best = &it;
    }
    return best ? best->id : std::string();
}

void CanvasPanel::ClipRectFor(const std::string& id, ImVec2& mn, ImVec2& mx) const {
    mn = clip_min_;
    mx = clip_max_;
    for (const Node* p = ParentOf(id); p; p = ParentOf(p->id)) {
        if (p->type != "Child" && p->type != "Canvas" && p->type != "Window" && p->type != "Popup" && p->type != "Modal")
            continue;
        const CanvasItem* it = Item(p->id);
        if (!it) continue;
        mn = ImVec2(ImMax(mn.x, it->min.x), ImMax(mn.y, it->min.y));
        mx = ImVec2(ImMin(mx.x, it->max.x), ImMin(mx.y, it->max.y));
        return;
    }
}

void CanvasPanel::DrawOverlays(EditorContext& ctx, ImDrawList* dl) {
    const BuilderColors& c = Colors();
    const ImU32 accent = ToU32(c.accent);
    dl->PushClipRect(clip_min_, clip_max_, true);
    if (title_bar_h_ > 0.0f) {
        float r = title_bar_h_ * 0.32f;
        overlay::CloseBox(dl, ImVec2(frame_max_.x - r - title_bar_h_ * 0.3f, frame_min_.y + title_bar_h_ * 0.5f), r,
                          title_btn_, title_fg_);
    }
    if (show_bounds_) {
        const ImU32 bounds = ToU32(c.info, 0.45f);
        for (const CanvasItem& it : items_) overlay::BoundsRect(dl, it.min, it.max, bounds);
    }
    if (!interactive_) {
        const std::string& primary = ctx.SelectedId();
        if (const Node* parent = primary.empty() ? nullptr : ParentOf(primary)) {
            if (const CanvasItem* pit = Item(parent->id)) {
                ImVec2 cmn, cmx;
                ClipRectFor(parent->id, cmn, cmx);
                dl->PushClipRect(cmn, cmx, true);
                overlay::DashedRect(dl, pit->min, pit->max, ToU32(c.accent, 0.55f), 4.0f * scale_, 3.0f * scale_, 1.0f);
                dl->PopClipRect();
            }
        }
        if (!hovered_id_.empty() && !ctx.IsSelected(hovered_id_)) {
            if (const CanvasItem* hit = Item(hovered_id_)) {
                ImVec2 cmn, cmx;
                ClipRectFor(hovered_id_, cmn, cmx);
                dl->PushClipRect(cmn, cmx, true);
                overlay::HoverOutline(dl, hit->min, hit->max, ToU32(c.hover_outline.w > 0 ? c.hover_outline : c.accent, 0.9f));
                dl->PopClipRect();
            }
        }
        for (const std::string& id : ctx.selection) {
            const CanvasItem* it = Item(id);
            if (!it) continue;
            ImVec2 cmn, cmx;
            ClipRectFor(id, cmn, cmx);
            dl->PushClipRect(cmn, cmx, true);
            dl->AddRectFilled(it->min, it->max, ToU32(c.accent, 0.07f));
            overlay::SelectionOutline(dl, it->min, it->max, accent, IM_COL32(255, 255, 255, 255), 2.0f, 6.0f);
            dl->PopClipRect();
        }
        if (const CanvasItem* it = primary.empty() ? nullptr : Item(primary)) {
            std::string text = it->node->type;
            if (it->node->type == "Component" && !it->node->component.empty()) text += " " + it->node->component;
            const size_t type_len = text.size();
            text += "  " + it->id;
            ImVec2 pad(6.0f, 2.0f);
            ImVec2 size = overlay::LabelSize(text.c_str(), pad);
            ImVec2 pos(it->min.x - 1, it->min.y - size.y - 4);
            if (pos.y < clip_min_.y) pos.y = it->min.y + 1;
            dl->AddRectFilled(ImVec2(pos.x + 1, pos.y + 2), ImVec2(pos.x + size.x + 1, pos.y + size.y + 2), IM_COL32(0, 0, 0, 70), 4.0f);
            dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), accent, 4.0f);
            const char* s = text.c_str();
            ImVec2 tp(pos.x + pad.x, pos.y + pad.y);
            dl->AddText(tp, IM_COL32(255, 255, 255, 255), s, s + type_len);
            tp.x += ImGui::CalcTextSize(s, s + type_len).x;
            dl->AddText(tp, IM_COL32(255, 255, 255, 185), s + type_len);
        }
    }
    dl->PopClipRect();
}

void CanvasPanel::DrawSelectionHandles(EditorContext& ctx, Node& root) {
    ImGuiIO& io = ImGui::GetIO();
    const std::vector<std::string> selection = ctx.selection;
    const std::string& primary = ctx.SelectedId();
    bool any_active = false;
    for (const std::string& id : selection) {
        const CanvasItem* it = Item(id);
        if (!it || !it->node) continue;
        const Node* node = it->node;
        bool container = IsContainerNode(ctx, *node);
        ImVec2 hmin = it->min, hmax = it->max;
        if (container) {
            std::string text = node->type + "  " + node->id;
            ImVec2 size = overlay::LabelSize(text.c_str(), ImVec2(5.0f, 2.0f));
            hmin = ImVec2(it->min.x - 1, it->min.y - size.y - 3);
            if (hmin.y < clip_min_.y) hmin.y = it->min.y + 1;
            hmax = ImVec2(hmin.x + size.x, hmin.y + size.y);
            if (id != primary) continue;
        }
        ImVec2 size(ImMax(hmax.x - hmin.x, 2.0f), ImMax(hmax.y - hmin.y, 2.0f));
        ImGui::SetCursorScreenPos(hmin);
        ImGui::PushID(id.c_str());
        ImGui::InvisibleButton("##handle", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        if (ImGui::IsItemHovered()) {
            hovered_id_ = id;
            ImGui::SetMouseCursor(InCanvasContainer(id) ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            context_id_ = id;
            open_context_ = true;
        }
        bool locked = node->locked;
        if (!locked && ImGui::IsItemActive()) {
            any_active = true;
            if (InCanvasContainer(id)) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
                    if (!moving_) {
                        moving_ = true;
                        move_origins_.clear();
                        move_start_mouse_ = io.MouseClickedPos[0];
                        for (const std::string& sid : selection)
                            if (InCanvasContainer(sid))
                                if (const Node* sn = FindNode(root, sid)) move_origins_[sid] = sn->layout.offset;
                    }
                    ImVec2 delta((io.MousePos.x - move_start_mouse_.x) / scale_, (io.MousePos.y - move_start_mouse_.y) / scale_);
                    ctx.BeginEdit("Move");
                    for (const auto& kv : move_origins_) {
                        Node* sn = FindNode(root, kv.first);
                        if (!sn) continue;
                        Vec2 o;
                        o.x = kv.second.x + delta.x;
                        o.y = kv.second.y + delta.y;
                        if (io.KeyShift) {
                            o.x = std::round(o.x / kSnapStep) * kSnapStep;
                            o.y = std::round(o.y / kSnapStep) * kSnapStep;
                        }
                        SetCanvasOffset(ctx, *sn, o, "Move");
                    }
                    ctx.EndEdit();
                }
            } else if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                ImGui::SetDragDropPayload("IMFORGE_NODE", id.c_str(), id.size() + 1);
                ImGui::Text("%s  %s", node->type.c_str(), node->id.c_str());
                ImGui::EndDragDropSource();
            }
        }
        ImGui::PopID();
    }
    if (!any_active || !io.MouseDown[0]) {
        moving_ = false;
        move_origins_.clear();
    }
}

void CanvasPanel::SetCanvasOffset(EditorContext& ctx, Node& node, const Vec2& offset, const char* label) {
    if (node.layout.offset.x == offset.x && node.layout.offset.y == offset.y) return;
    ctx.BeginEdit(label);
    node.layout.offset = offset;
    ctx.EndEdit();
}

void CanvasPanel::NudgeOrStep(EditorContext& ctx, Node& root, int dx, int dy, bool big) {
    const std::string primary = ctx.SelectedId();
    if (primary.empty()) return;
    if (InCanvasContainer(primary)) {
        float step = big ? 10.0f : 1.0f;
        std::vector<std::string> ids = ctx.selection;
        for (const std::string& id : ids) {
            if (!InCanvasContainer(id)) continue;
            Node* n = FindNode(root, id);
            if (!n || n->locked) continue;
            Vec2 o = n->layout.offset;
            o.x += dx * step;
            o.y += dy * step;
            SetCanvasOffset(ctx, *n, o, "Nudge");
        }
        return;
    }
    const Node* parent = ParentOf(primary);
    if (!parent) return;
    const std::vector<Node>* list = &parent->children;
    int idx = -1;
    for (size_t i = 0; i < list->size(); ++i)
        if ((*list)[i].id == primary) idx = (int)i;
    if (idx < 0) {
        for (const auto& s : parent->slots)
            for (size_t i = 0; i < s.second.size(); ++i)
                if (s.second[i].id == primary) {
                    list = &s.second;
                    idx = (int)i;
                }
    }
    if (idx < 0 || list->empty()) return;
    int delta = (dx > 0 || dy > 0) ? 1 : -1;
    int next = ImClamp(idx + delta, 0, (int)list->size() - 1);
    if (next != idx) ctx.Select((*list)[next].id);
}

void CanvasPanel::DrawContextMenu(EditorContext& ctx, Node& root) {
    if (open_context_) {
        ImGui::OpenPopup("##canvas_ctx");
        open_context_ = false;
    }
    if (!ImGui::BeginPopup("##canvas_ctx")) return;
    Node* node = FindNode(root, context_id_);
    if (!node) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const bool is_root = node == &root;
    const Node* parent = ParentOf(context_id_);
    ImGui::TextDisabled("%s  %s", node->type.c_str(), node->id.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Select parent", nullptr, false, parent != nullptr)) ctx.Select(parent->id);
    const unsigned serial0 = ctx.edit_serial;
    auto leave_if_edited = [&]() {
        if (ctx.edit_serial == serial0) return false;
        tree_changed_ = true;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return true;
    };
    if (ImGui::MenuItem("Move up", nullptr, false, !is_root && !node->locked)) {
        ctx.BeginEdit("Move");
        tree::MoveSibling(root, context_id_, -1);
        ctx.EndEdit();
        if (leave_if_edited()) return;
    }
    if (ImGui::MenuItem("Move down", nullptr, false, !is_root && !node->locked)) {
        ctx.BeginEdit("Move");
        tree::MoveSibling(root, context_id_, 1);
        ctx.EndEdit();
        if (leave_if_edited()) return;
    }
    ImGui::Separator();
    std::vector<std::string> ids = tree::TopLevelIds(root, ctx.selection);
    if (ids.empty()) ids.push_back(context_id_);
    if (ImGui::BeginMenu("Wrap in", !is_root)) {
        for (const char* t : kWrapTypes) {
            bool can = tree::CanWrap(root, ids, t, &ctx.catalog);
            if (ImGui::MenuItem(t, nullptr, false, can)) {
                std::string wid;
                ctx.BeginEdit("Wrap");
                bool ok = tree::WrapNodes(root, ids, t, &wid);
                ctx.EndEdit();
                if (ok && !wid.empty()) ctx.Select(wid);
                tree_changed_ = true;
                break;
            }
        }
        ImGui::EndMenu();
        if (tree_changed_ || ctx.edit_serial != serial0) {
            tree_changed_ = true;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
    }
    if (ImGui::MenuItem("Unwrap", nullptr, false, !is_root && tree::CanUnwrap(root, context_id_, &ctx.catalog))) {
        ctx.BeginEdit("Unwrap");
        tree::UnwrapNode(root, context_id_, &ctx.catalog);
        ctx.EndEdit();
        tree_changed_ = true;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (ImGui::MenuItem(node->locked ? "Unlock" : "Lock", nullptr, false, !is_root)) {
        ctx.BeginEdit("Lock");
        node->locked = !node->locked;
        ctx.EndEdit();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Cut", "Ctrl+X", false, !is_root)) {
        ctx.CutSelection();
        tree_changed_ = true;
    }
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, !is_root)) ctx.CopySelection();
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, ctx.CanPaste())) {
        ctx.Paste();
        tree_changed_ = true;
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !is_root)) {
        ctx.DuplicateSelection();
        tree_changed_ = true;
    }
    if (ImGui::MenuItem("Delete", "Del", false, !is_root)) {
        ctx.DeleteSelection();
        tree_changed_ = true;
    }
    ImGui::EndPopup();
}

}  // namespace imforge::builder

#endif
