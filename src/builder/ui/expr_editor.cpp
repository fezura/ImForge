#include "expr_editor.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "../builder_style.h"
#include "editor_type_env.h"
#include "icon_picker.h"
#include "icon_table.h"
#include "ui_helpers.h"

namespace imforge::builder {

namespace {

bool IsPathChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
           c == '[' || c == ']' || c == ':';
}

enum class AcMode { Expr, Interp, Path };

// one shared completion state is enough, only one InputText can be active at a time
struct AcState {
    ImGuiID id = 0;
    AcMode mode = AcMode::Expr;
    EditorTypeEnv* env = nullptr;
    int cursor = 0;
    int token_start = 0;
    std::string prefix;
    bool have_prefix = false;
    std::vector<std::string> items;
    int selected = 0;
    int pending_cursor = -1;
    bool want_focus = false;
    bool scroll_to_selected = false;
    bool show_on_empty = false;
};

AcState& Ac() {
    static AcState s;
    return s;
}

bool FindToken(AcMode mode, const char* buf, int len, int cursor, int& token_start, std::string& prefix) {
    if (cursor < 0 || cursor > len) return false;
    if (mode == AcMode::Interp) {
        int open = -1;
        for (int i = cursor - 1; i >= 0; --i) {
            if (buf[i] == '}') return false;
            if (buf[i] == '{') {
                open = i;
                break;
            }
        }
        if (open < 0) return false;
        if (open > 0 && buf[open - 1] == '{') return false;
        if (std::strncmp(buf + open + 1, "icon:", 5) == 0) return false;
        for (int i = open + 1; i < cursor; ++i) {
            if (buf[i] != ':') continue;
            if (i + 1 < len && buf[i + 1] == ':') {
                ++i;
                continue;
            }
            return false;
        }
    }
    int start = cursor;
    while (start > 0 && IsPathChar(buf[start - 1])) --start;
    while (start < cursor && (buf[start] == '.' || buf[start] == '[' || buf[start] == ']' || buf[start] == ':' ||
                              (buf[start] >= '0' && buf[start] <= '9')))
        ++start;
    token_start = start;
    prefix.assign(buf + start, buf + cursor);
    if (mode == AcMode::Expr && (prefix == "true" || prefix == "false")) return false;
    return true;
}

void RefreshItems(AcState& s) {
    s.items.clear();
    s.selected = 0;
    if (!s.env || !s.have_prefix) return;
    if (s.prefix.empty() && !s.show_on_empty) return;
    std::vector<std::string> items = s.env->Complete(s.prefix);
    for (const std::string& it : items)
        if (it != s.prefix) s.items.push_back(it);
    if (s.items.size() > 200) s.items.resize(200);
}

int InputCallback(ImGuiInputTextCallbackData* data) {
    AcState& s = Ac();
    std::string* str = static_cast<std::string*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        str->resize((size_t)data->BufTextLen);
        data->Buf = const_cast<char*>(str->c_str());
        return 0;
    }
    if (ImGui::GetActiveID() != s.id) return 0;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        if (s.pending_cursor >= 0) {
            int c = std::min(s.pending_cursor, data->BufTextLen);
            data->CursorPos = c;
            data->SelectionStart = data->SelectionEnd = c;
            s.pending_cursor = -1;
        }
        s.cursor = data->CursorPos;
        int ts = 0;
        std::string prefix;
        bool have = FindToken(s.mode, data->Buf, data->BufTextLen, data->CursorPos, ts, prefix);
        if (have != s.have_prefix || prefix != s.prefix || ts != s.token_start) {
            s.have_prefix = have;
            s.prefix = prefix;
            s.token_start = ts;
            RefreshItems(s);
        }
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
        if (s.items.empty()) return 0;
        const std::string& item = s.items[(size_t)std::clamp(s.selected, 0, (int)s.items.size() - 1)];
        int start = s.token_start;
        int len = data->CursorPos - start;
        if (start < 0 || len < 0 || start + len > data->BufTextLen) return 0;
        data->DeleteChars(start, len);
        data->InsertChars(start, item.c_str());
        data->CursorPos = start + (int)item.size();
        s.items.clear();
        s.have_prefix = false;
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        if (s.items.empty()) return 0;
        int n = (int)s.items.size();
        if (data->EventKey == ImGuiKey_UpArrow) s.selected = (s.selected + n - 1) % n;
        else if (data->EventKey == ImGuiKey_DownArrow) s.selected = (s.selected + 1) % n;
        s.scroll_to_selected = true;
    }
    return 0;
}

bool AutocompleteInput(const char* label, std::string& text, expr::TypeEnv* env, AcMode mode, bool show_on_empty,
                       bool error_border) {
    AcState& s = Ac();
    EditorTypeEnv* editor_env = dynamic_cast<EditorTypeEnv*>(env);
    ImGuiID id = ImGui::GetID(label);
    bool is_ours = s.id == id;
    if (is_ours && s.want_focus) {
        ImGui::SetKeyboardFocusHere();
        s.want_focus = false;
    }
    if (error_border) {
        ImGui::PushStyleColor(ImGuiCol_Border, Colors().error);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    }
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways |
                                ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
    bool changed = ImGui::InputText(label, const_cast<char*>(text.c_str()), text.capacity() + 1, flags, InputCallback,
                                    static_cast<void*>(&text));
    if (error_border) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemActivated()) {
        if (!is_ours) {
            s.id = id;
            s.items.clear();
            s.have_prefix = false;
            s.prefix.clear();
            s.selected = 0;
            s.pending_cursor = -1;
            is_ours = true;
        }
    }
    if (is_ours) {
        s.mode = mode;
        s.env = editor_env;
        s.show_on_empty = show_on_empty;
    }
    bool active = ImGui::IsItemActive();
    ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (!active) {
        if (is_ours && !s.want_focus) {
            s.items.clear();
            s.have_prefix = false;
        }
        return changed;
    }
    if (!is_ours || s.items.empty()) return changed;

    const ImGuiStyle& st = ImGui::GetStyle();
    float item_h = ImGui::GetTextLineHeightWithSpacing();
    int shown = std::min((int)s.items.size(), 8);
    ImVec2 size(std::max(rect.GetWidth(), 160.0f), item_h * (float)shown + st.WindowPadding.y);
    ImGui::SetNextWindowPos(ImVec2(rect.Min.x, rect.Max.y + 2.0f));
    ImGui::SetNextWindowSize(size);
    ImGuiWindowFlags wf = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                          ImGuiWindowFlags_NoNavFocus;
#ifdef IMGUI_HAS_DOCK
    wf |= ImGuiWindowFlags_NoDocking;
#endif
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(st.WindowPadding.x * 0.5f, st.WindowPadding.y * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors().bg_panel);
    ImGui::PushStyleColor(ImGuiCol_Border, Colors().border);
    int clicked = -1;
    if (ImGui::Begin("##imforge_autocomplete", nullptr, wf)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        for (int i = 0; i < (int)s.items.size(); ++i) {
            bool sel = i == s.selected;
            if (ImGui::Selectable(s.items[(size_t)i].c_str(), sel)) clicked = i;
            if (sel && s.scroll_to_selected) ImGui::SetScrollHereY();
        }
        s.scroll_to_selected = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    if (clicked >= 0) {
        const std::string& item = s.items[(size_t)clicked];
        int start = std::clamp(s.token_start, 0, (int)text.size());
        int end = std::clamp(s.cursor, start, (int)text.size());
        text = text.substr(0, (size_t)start) + item + text.substr((size_t)end);
        s.pending_cursor = start + (int)item.size();
        s.want_focus = true;
        s.items.clear();
        s.have_prefix = false;
        ImGui::ClearActiveID();
        changed = true;
    }
    return changed;
}

void DrawErrorUnderline(const std::string& text, int col) {
    if (col <= 0) return;
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    float pad = ImGui::GetStyle().FramePadding.x;
    size_t at = (size_t)std::min<int>(col - 1, (int)text.size());
    float x0 = mn.x + pad + ImGui::CalcTextSize(text.c_str(), text.c_str() + at).x;
    float cw = at < text.size() ? ImGui::CalcTextSize(text.c_str() + at, text.c_str() + at + 1).x : 0;
    float x1 = x0 + std::max(cw, 6.0f);
    if (x0 >= mx.x - pad) return;
    x1 = std::min(x1, mx.x - pad);
    ImU32 c = ToU32(Colors().error);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(x0, mx.y - 2.0f), ImVec2(x1, mx.y - 2.0f), c, 2.0f);
    dl->AddTriangleFilled(ImVec2(x0 - 3.0f, mx.y), ImVec2(x0 + 3.0f, mx.y), ImVec2(x0, mx.y - 4.0f), c);
}

void DrawErrorText(const ExprCheck& check) {
    ImGui::PushStyleColor(ImGuiCol_Text, Colors().error);
    ImGui::PushTextWrapPos(0.0f);
    if (check.col > 0) ImGui::Text("%s (col %d)", check.error.c_str(), check.col);
    else ImGui::TextUnformatted(check.error.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

struct CheckCacheEntry {
    ImGuiID key;
    std::string text;
    ExprCheck check;
};

std::vector<CheckCacheEntry>& CheckCache() {
    static std::vector<CheckCacheEntry> cache;
    return cache;
}

bool FindCached(const std::string& text, ImGuiID key, ExprCheck& out) {
    for (CheckCacheEntry& e : CheckCache()) {
        if (e.key == key) {
            if (e.text != text) return false;
            out = e.check;
            return true;
        }
    }
    return false;
}

void StoreCached(const std::string& text, ImGuiID key, const ExprCheck& check) {
    std::vector<CheckCacheEntry>& cache = CheckCache();
    for (CheckCacheEntry& e : cache) {
        if (e.key == key) {
            e.text = text;
            e.check = check;
            return;
        }
    }
    if (cache.size() > 256) cache.erase(cache.begin());
    cache.push_back(CheckCacheEntry{key, text, check});
}

struct InterpHelperState {
    std::string icon;
    std::string path;
    std::string fmt;
    bool fmt_auto = true;
};

InterpHelperState& Helper() {
    static InterpHelperState s;
    return s;
}

}  // namespace

bool CheckExpr(const std::string& src, expr::TypeEnv* env, expr::Type expected, ExprCheck& out) {
    out = ExprCheck();
    if (src.find_first_not_of(" \t\r\n") == std::string::npos) {
        out.empty = true;
        return true;
    }
    out.empty = false;
    expr::Ast ast;
    Error err;
    if (!expr::Parse(src, ast, &err)) {
        out.ok = false;
        out.error = err.message;
        out.col = err.col;
        return false;
    }
    if (!env) return true;
    expr::Type t = expr::Type::Error;
    if (!expr::TypeCheck(ast, *env, t, &err)) {
        out.ok = false;
        out.error = err.message;
        out.col = err.col;
        return false;
    }
    out.type = t;
    if (expected != expr::Type::Error && t != expected && !(expected == expr::Type::Float && t == expr::Type::Int)) {
        out.ok = false;
        out.error = std::string("expression is ") + expr::TypeName(t) + ", expected " + expr::TypeName(expected);
        out.col = 1;
        return false;
    }
    return true;
}

bool CheckInterpolation(const std::string& text, expr::TypeEnv* env, ExprCheck& out) {
    out = ExprCheck();
    out.empty = text.empty();
    std::vector<expr::InterpSegment> segs;
    Error err;
    if (!expr::ParseInterpolation(text, segs, &err)) {
        out.ok = false;
        out.error = err.message;
        out.col = err.col;
        return false;
    }
    size_t icon_count = 0;
    IconTable(icon_count);
    for (const expr::InterpSegment& seg : segs) {
        if (!seg.is_expr) continue;
        if (seg.is_icon) {
            if (icon_count > 0 && !FindIcon(seg.text_or_expr.c_str())) {
                out.ok = false;
                out.error = "unknown icon \"" + seg.text_or_expr + "\"";
                size_t at = text.find("{icon:" + seg.text_or_expr);
                out.col = at == std::string::npos ? 0 : (int)at + 1;
                return false;
            }
            continue;
        }
        ExprCheck inner;
        if (!CheckExpr(seg.text_or_expr, env, expr::Type::Error, inner)) {
            out.ok = false;
            out.error = "{" + seg.text_or_expr + "}: " + inner.error;
            size_t at = text.find("{" + seg.text_or_expr);
            out.col = at == std::string::npos ? 0 : (int)at + 1 + std::max(inner.col, 1);
            return false;
        }
    }
    return true;
}

bool ExprEditor(const char* label, std::string& expr_text, expr::TypeEnv* env, expr::Type expected) {
    ImGui::PushID(label);
    ImGuiID key = ImGui::GetID("check");
    ExprCheck check;
    if (!FindCached(expr_text, key, check)) {
        CheckExpr(expr_text, env, expected, check);
        StoreCached(expr_text, key, check);
    }
    bool changed = AutocompleteInput(label, expr_text, env, AcMode::Expr, false, !check.ok);
    if (changed) {
        CheckExpr(expr_text, env, expected, check);
        StoreCached(expr_text, key, check);
    }
    if (!check.ok) {
        DrawErrorUnderline(expr_text, check.col);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", check.error.c_str());
        DrawErrorText(check);
    } else if (!check.empty && check.type != expr::Type::Error && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("type: %s", expr::TypeName(check.type));
    }
    ImGui::PopID();
    return changed;
}

const char* DefaultFormatFor(const CType& t) {
    switch (t.kind) {
    case CKind::I8: case CKind::U8: case CKind::I16: case CKind::U16: case CKind::I32: case CKind::U32:
    case CKind::I64: case CKind::U64: case CKind::Enum: return "%d";
    case CKind::Float: case CKind::Double: return "%.2f";
    case CKind::StdString: case CKind::CharArray: return "%s";
    default: return "";
    }
}

bool InterpStringEditor(const char* label, std::string& text, expr::TypeEnv* env) {
    ImGui::PushID(label);
    ImGuiID key = ImGui::GetID("icheck");
    ExprCheck check;
    if (!FindCached(text, key, check)) {
        CheckInterpolation(text, env, check);
        StoreCached(text, key, check);
    }
    const ImGuiStyle& st = ImGui::GetStyle();
    float total = ImGui::CalcItemWidth();
    float btn_w = ImGui::GetFrameHeight() * 1.25f;
    float input_w = std::max(total - (btn_w + st.ItemInnerSpacing.x) * 2, 60.0f);
    ImGui::SetNextItemWidth(input_w);
    bool changed = AutocompleteInput(label, text, env, AcMode::Interp, false, !check.ok);
    if (changed) {
        CheckInterpolation(text, env, check);
        StoreCached(text, key, check);
    }
    if (!check.ok) {
        DrawErrorUnderline(text, check.col);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", check.error.c_str());
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && text.find('{') != std::string::npos) {
        ImGui::SetTooltip("Interpolation: {path}, {path:%%fmt}, {icon:fa-name}; {{ and }} for literal braces");
    }

    InterpHelperState& h = Helper();
    ImGui::SameLine(0, st.ItemInnerSpacing.x);
#ifdef ICON_FA_ICONS
    const char* icon_label = ICON_FA_ICONS "##icon";
#else
    const char* icon_label = "ico##icon";
#endif
    if (ImGui::Button(icon_label, ImVec2(btn_w, 0))) {
        h.icon.clear();
        ImGui::OpenPopup("##insert_icon");
    }
    ui::Tooltip("Insert an icon token {icon:fa-name}");
    if (ImGui::BeginPopup("##insert_icon")) {
        ImGui::TextDisabled("Pick an icon to insert");
        ImGui::SetNextItemWidth(260.0f);
        if (IconPicker("pick", h.icon) && !h.icon.empty()) {
            if (!text.empty() && text.back() != ' ') text += ' ';
            text += IconToken(h.icon);
            changed = true;
            h.icon.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine(0, st.ItemInnerSpacing.x);
    if (ImGui::Button("{}##path", ImVec2(btn_w, 0))) {
        h.path.clear();
        h.fmt.clear();
        h.fmt_auto = true;
        ImGui::OpenPopup("##insert_path");
    }
    ui::Tooltip("Insert a bound value {path:fmt}");
    if (ImGui::BeginPopup("##insert_path")) {
        ImGui::TextDisabled("Insert a bound value");
        ImGui::SetNextItemWidth(260.0f);
        PathStatus ps;
        PathEditor("path##ins", h.path, env, nullptr, &ps);
        const char* auto_fmt = ps.kind == PathStatus::Kind::Ok ? DefaultFormatFor(ps.type) : "";
        if (h.fmt_auto) h.fmt = auto_fmt;
        ImGui::SetNextItemWidth(120.0f);
        if (ui::InputTextStd("format", h.fmt)) h.fmt_auto = false;
        ui::Tooltip("printf-style format such as %.0f, %d or %s; empty = default for the type");
        bool can = !h.path.empty() && ps.kind != PathStatus::Kind::Unknown;
        if (!can) ImGui::BeginDisabled();
        if (ImGui::Button("Insert", ImVec2(120, 0))) {
            std::string token = "{" + h.path;
            if (!h.fmt.empty() && h.fmt != auto_fmt) token += ":" + h.fmt;
            token += "}";
            text += token;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        if (!can) ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (!check.ok) DrawErrorText(check);
    ImGui::PopID();
    return changed;
}

std::string BindTypeName(const CType& t) {
    switch (t.kind) {
    case CKind::Bool: return "bool";
    case CKind::I8: case CKind::U8: case CKind::I16: case CKind::U16: case CKind::I32: case CKind::U32:
    case CKind::I64: case CKind::U64: return "int";
    case CKind::Float: return "float";
    case CKind::Double: return "double";
    case CKind::StdString: return "string";
    case CKind::CharArray: return "char[" + std::to_string(t.count) + "]";
    case CKind::Vec2: return "vec2";
    case CKind::Vec4: return "vec4";
    case CKind::FloatArray: return "float[" + std::to_string(t.count) + "]";
    case CKind::IntArray: return "int[" + std::to_string(t.count) + "]";
    case CKind::Enum: return t.name.empty() ? "enum" : "enum " + t.name;
    case CKind::Struct: return t.strct ? "struct " + t.strct->name : (t.name.empty() ? "struct" : t.name);
    case CKind::Vector: return "vector<" + (t.elem ? BindTypeName(*t.elem) : std::string("?")) + ">";
    case CKind::Array: return (t.elem ? BindTypeName(*t.elem) : std::string("?")) + "[" + std::to_string(t.count) + "]";
    case CKind::TexID: return "texture";
    default: return t.name.empty() ? "unknown" : t.name;
    }
}

bool CTypeMatchesBindTypes(const CType& t, const char* accepted) {
    if (!accepted || !*accepted) return true;
    if (t.kind == CKind::Unknown) return true;
    bool is_int = t.kind == CKind::I8 || t.kind == CKind::U8 || t.kind == CKind::I16 || t.kind == CKind::U16 ||
                  t.kind == CKind::I32 || t.kind == CKind::U32 || t.kind == CKind::I64 || t.kind == CKind::U64;
    for (const std::string& a : SplitPipe(accepted)) {
        if (a == "any") return true;
        if (a == "bool" && t.kind == CKind::Bool) return true;
        if (a == "int" && is_int) return true;
        if (a == "float" && (t.kind == CKind::Float || t.kind == CKind::Double)) return true;
        if (a == "double" && t.kind == CKind::Double) return true;
        if (a == "string" && t.kind == CKind::StdString) return true;
        if (a == "char[N]" && t.kind == CKind::CharArray) return true;
        if (a == "vec2" && t.kind == CKind::Vec2) return true;
        if (a == "vec4" && t.kind == CKind::Vec4) return true;
        if ((a == "float[N]" || a == "float[]") && t.kind == CKind::FloatArray) return true;
        if (a == "float[4]" && t.kind == CKind::FloatArray && t.count == 4) return true;
        if (a == "float[3]" && t.kind == CKind::FloatArray && t.count == 3) return true;
        if (a == "float[2]" && t.kind == CKind::FloatArray && t.count == 2) return true;
        if ((a == "int[N]" || a == "int[]") && t.kind == CKind::IntArray) return true;
        if (a == "enum" && t.kind == CKind::Enum) return true;
        if (a == "texture" && t.kind == CKind::TexID) return true;
        if (a == "vector<float>" && t.kind == CKind::Vector && t.elem && t.elem->kind == CKind::Float) return true;
        if (a == "vector<int>" && t.kind == CKind::Vector && t.elem && t.elem->kind == CKind::I32) return true;
    }
    return false;
}

PathStatus CheckPath(const std::string& path, expr::TypeEnv* env, const char* accepted) {
    PathStatus st;
    if (path.empty()) return st;
    if (!env) {
        st.kind = PathStatus::Kind::Ok;
        st.message = "no type information";
        return st;
    }
    CType t;
    bool ok = false;
    std::string err;
    if (EditorTypeEnv* ee = dynamic_cast<EditorTypeEnv*>(env)) ok = ee->TypeOfPathErr(path, t, &err);
    else ok = env->TypeOfPath(path, t);
    if (!ok) {
        st.kind = PathStatus::Kind::Unknown;
        st.message = err.empty() ? "unknown path" : err;
        return st;
    }
    st.type = t;
    st.type_name = BindTypeName(t);
    if (!CTypeMatchesBindTypes(t, accepted)) {
        st.kind = PathStatus::Kind::Mismatch;
        st.message = "got " + st.type_name + ", expected " + accepted;
        return st;
    }
    st.kind = PathStatus::Kind::Ok;
    st.message = st.type_name;
    return st;
}

void PathStatusBadge(const PathStatus& st) {
    const BuilderColors& c = Colors();
    switch (st.kind) {
    case PathStatus::Kind::Empty: return;
    case PathStatus::Kind::Ok: ui::Badge(st.message.empty() ? "ok" : st.message.c_str(), c.success); return;
    case PathStatus::Kind::Mismatch:
        ui::Badge("type mismatch", c.warning);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", st.message.c_str());
        return;
    case PathStatus::Kind::Unknown:
        ui::Badge("unknown path", c.error);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", st.message.c_str());
        return;
    }
}

bool PathEditor(const char* label, std::string& path, expr::TypeEnv* env, const char* accepted, PathStatus* status_out) {
    ImGui::PushID(label);
    PathStatus st = CheckPath(path, env, accepted);
    bool bad = st.kind == PathStatus::Kind::Unknown || st.kind == PathStatus::Kind::Mismatch;
    bool changed = AutocompleteInput(label, path, env, AcMode::Path, true, bad);
    if (changed) st = CheckPath(path, env, accepted);
    if (st.kind != PathStatus::Kind::Empty && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", st.message.c_str());
    if (status_out) *status_out = st;
    ImGui::PopID();
    return changed;
}

}  // namespace imforge::builder
