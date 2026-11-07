#include "code_text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>
#include <unordered_set>

#include "../builder_style.h"
#include "ui_helpers.h"

namespace imforge::builder::ui {

namespace {

bool IsIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool IsIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

const std::unordered_set<std::string_view>& Keywords() {
    static const std::unordered_set<std::string_view> s = {
        "if",        "else",     "for",       "while",     "do",        "return",   "switch",    "case",
        "break",     "continue", "default",   "namespace", "struct",    "class",    "enum",      "union",
        "using",     "typedef",  "template",  "typename",  "static",    "inline",   "const",     "constexpr",
        "virtual",   "override", "final",     "public",    "private",   "protected", "new",      "delete",
        "nullptr",   "true",     "false",     "sizeof",    "this",      "operator", "friend",    "extern",
        "volatile",  "mutable",  "explicit",  "noexcept",  "try",       "catch",    "throw",     "goto",
        "static_cast", "reinterpret_cast", "const_cast", "dynamic_cast", "decltype", "alignas", "alignof",
        "static_assert", "and", "or", "not", "NULL", "IM_COL32", "IM_ASSERT", "IMGUI_VERSION_NUM"};
    return s;
}

const std::unordered_set<std::string_view>& Types() {
    static const std::unordered_set<std::string_view> s = {
        "void",     "bool",     "char",     "int",      "float",    "double",   "long",     "short",
        "unsigned", "signed",   "auto",     "size_t",   "int8_t",   "uint8_t",  "int16_t",  "uint16_t",
        "int32_t",  "uint32_t", "int64_t",  "uint64_t", "wchar_t",  "char16_t", "char32_t", "intptr_t",
        "uintptr_t", "ptrdiff_t", "std",    "string",   "vector",   "ImU32",    "ImS32",    "ImU8",
        "ImS8",     "ImU16",    "ImS16",    "ImU64",    "ImS64",    "ImWchar",  "ImGuiID",  "ImTextureID"};
    return s;
}

bool LooksLikeType(std::string_view w) {
    if (Types().count(w)) return true;
    return w.size() > 2 && w[0] == 'I' && w[1] == 'm' && std::isupper((unsigned char)w[2]);
}

void TokenizeLine(CodeLine& ln, bool& in_block_comment) {
    const std::string& t = ln.text;
    const size_t n = t.size();
    auto push = [&](size_t b, size_t e, CodeTok k) {
        if (e <= b) return;
        if (!ln.spans.empty() && ln.spans.back().kind == k && ln.spans.back().end == (int)b) ln.spans.back().end = (int)e;
        else ln.spans.push_back({(int)b, (int)e, k});
    };
    size_t first = t.find_first_not_of(' ');
    if (first != std::string::npos && t.compare(first, 2, "//") == 0 && t.find("@imb:") != std::string::npos)
        ln.marker = true;
    if (!in_block_comment && first != std::string::npos && t[first] == '#') {
        push(0, n, CodeTok::Preproc);
        return;
    }
    size_t i = 0;
    while (i < n) {
        if (in_block_comment) {
            size_t e = t.find("*/", i);
            if (e == std::string::npos) {
                push(i, n, CodeTok::Comment);
                i = n;
            } else {
                push(i, e + 2, CodeTok::Comment);
                i = e + 2;
                in_block_comment = false;
            }
            continue;
        }
        char c = t[i];
        if (c == '/' && i + 1 < n && t[i + 1] == '/') {
            push(i, n, ln.marker ? CodeTok::Marker : CodeTok::Comment);
            i = n;
            continue;
        }
        if (c == '/' && i + 1 < n && t[i + 1] == '*') {
            in_block_comment = true;
            push(i, i + 2, CodeTok::Comment);
            i += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < n && t[j] != c) {
                if (t[j] == '\\') ++j;
                ++j;
            }
            j = std::min(j + 1, n);
            push(i, j, CodeTok::String);
            i = j;
            continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)t[i + 1]))) {
            size_t j = i + 1;
            while (j < n && (std::isalnum((unsigned char)t[j]) || t[j] == '.' || t[j] == '\'')) ++j;
            push(i, j, CodeTok::Number);
            i = j;
            continue;
        }
        if (IsIdentStart(c)) {
            size_t j = i + 1;
            while (j < n && IsIdentChar(t[j])) ++j;
            std::string_view w(t.data() + i, j - i);
            CodeTok k = CodeTok::Plain;
            if (Keywords().count(w)) k = CodeTok::Keyword;
            else if (LooksLikeType(w)) k = CodeTok::Type;
            else {
                size_t p = j;
                while (p < n && t[p] == ' ') ++p;
                if (p < n && t[p] == '(') k = CodeTok::Function;
                else if (p + 1 < n && t[p] == ':' && t[p + 1] == ':') k = CodeTok::Type;
            }
            push(i, j, k);
            i = j;
            continue;
        }
        size_t j = i + 1;
        while (j < n && !IsIdentStart(t[j]) && !std::isdigit((unsigned char)t[j]) && t[j] != '"' && t[j] != '\'' &&
               t[j] != '/')
            ++j;
        push(i, j, CodeTok::Plain);
        i = j;
    }
}

void ParseMarker(const std::string& t, std::string& kind, std::string& id) {
    size_t p = t.find("@imb:");
    if (p == std::string::npos) return;
    p += 5;
    size_t e = p;
    while (e < t.size() && std::isalpha((unsigned char)t[e])) ++e;
    kind = t.substr(p, e - p);
    size_t q = t.find("id=", e);
    if (q == std::string::npos) return;
    q += 3;
    size_t z = q;
    while (z < t.size() && !std::isspace((unsigned char)t[z])) ++z;
    id = t.substr(q, z - q);
}

bool MatchAt(const std::string& hay, size_t pos, const std::string& needle, bool case_sensitive) {
    if (pos + needle.size() > hay.size()) return false;
    for (size_t k = 0; k < needle.size(); ++k) {
        unsigned char a = (unsigned char)hay[pos + k], b = (unsigned char)needle[k];
        if (!case_sensitive) {
            a = (unsigned char)std::tolower(a);
            b = (unsigned char)std::tolower(b);
        }
        if (a != b) return false;
    }
    return true;
}

int FindInLine(const std::string& line, const std::string& needle, bool case_sensitive, int from) {
    if (needle.empty() || from < 0) return -1;
    for (size_t p = (size_t)from; p + needle.size() <= line.size(); ++p)
        if (MatchAt(line, p, needle, case_sensitive)) return (int)p;
    return -1;
}

int FindLastInLine(const std::string& line, const std::string& needle, bool case_sensitive, int before) {
    int best = -1;
    for (int p = 0; p < before; ++p) {
        if (MatchAt(line, (size_t)p, needle, case_sensitive)) best = p;
    }
    return best;
}

ImU32 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f));
}

}  // namespace

CodeTextColors DefaultCodeColors() {
    const BuilderColors& c = Colors();
    CodeTextColors k;
    k.plain = ToU32(c.text);
    k.keyword = ToU32(c.accent);
    k.type = ToU32(c.info);
    k.function = Mix(c.text, c.accent, 0.35f);
    k.string = ToU32(c.warning);
    k.number = ToU32(c.success);
    k.comment = ToU32(c.text_dim);
    k.marker = ToU32(c.text_disabled);
    k.preproc = ToU32(c.accent_dim);
    k.line_number = ToU32(c.text_disabled);
    k.gutter_bg = ToU32(c.bg_panel);
    k.highlight_bg = ToU32(c.selection, 0.18f);
    k.highlight_bar = ToU32(c.selection_border);
    k.find_bg = ToU32(c.warning, 0.30f);
    k.find_current_bg = ToU32(c.warning, 0.65f);
    k.hover_bg = ToU32(c.bg_hover, 0.5f);
    return k;
}

std::string StripEventSuffix(const std::string& block_id) {
    size_t dot = block_id.find('.');
    return dot == std::string::npos ? block_id : block_id.substr(0, dot);
}

void CodeText::Clear() {
    text_.clear();
    lines_.clear();
    line_offsets_.clear();
    blocks_.clear();
    measured_font_ = nullptr;
    max_width_ = 0;
}

void CodeText::SetText(const std::string& text, int tab_width) {
    Clear();
    text_ = text;
    size_t pos = 0;
    bool in_block = false;
    std::vector<size_t> open_stack;
    while (pos <= text_.size()) {
        size_t nl = text_.find('\n', pos);
        size_t end = nl == std::string::npos ? text_.size() : nl;
        line_offsets_.push_back(pos);
        CodeLine ln;
        ln.text.reserve(end - pos);
        int col = 0;
        for (size_t i = pos; i < end; ++i) {
            char ch = text_[i];
            if (ch == '\r') continue;
            if (ch == '\t') {
                int pad = tab_width - (col % tab_width);
                ln.text.append((size_t)pad, ' ');
                col += pad;
            } else {
                ln.text.push_back(ch);
                ++col;
            }
        }
        TokenizeLine(ln, in_block);
        int line_index = (int)lines_.size();
        if (ln.marker) {
            std::string kind, id;
            ParseMarker(ln.text, kind, id);
            if (kind == "begin" || kind == "user" || kind == "locked") {
                CodeBlock b;
                b.kind = kind;
                b.id = id;
                b.line_begin = b.line_end = line_index;
                b.depth = (int)open_stack.size();
                open_stack.push_back(blocks_.size());
                blocks_.push_back(std::move(b));
            } else if (kind == "end") {
                for (size_t k = open_stack.size(); k-- > 0;) {
                    if (blocks_[open_stack[k]].id == id) {
                        blocks_[open_stack[k]].line_end = line_index;
                        open_stack.erase(open_stack.begin() + (long)k);
                        break;
                    }
                }
            }
        }
        lines_.push_back(std::move(ln));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    if (!lines_.empty() && lines_.back().text.empty() && text_.size() > 0 && text_.back() == '\n') {
        lines_.pop_back();
        line_offsets_.pop_back();
    }
    for (size_t idx : open_stack) blocks_[idx].line_end = (int)lines_.size() - 1;
}

std::string CodeText::SourceLines(int begin, int end) const {
    if (lines_.empty()) return std::string();
    begin = std::max(0, begin);
    end = std::min(end, (int)lines_.size() - 1);
    if (end < begin) return std::string();
    size_t b = line_offsets_[(size_t)begin];
    size_t e = (size_t)end + 1 < line_offsets_.size() ? line_offsets_[(size_t)end + 1] : text_.size();
    return text_.substr(b, e - b);
}

const CodeBlock* CodeText::BlockAtLine(int line) const {
    const CodeBlock* best = nullptr;
    for (const CodeBlock& b : blocks_) {
        if (line < b.line_begin || line > b.line_end) continue;
        if (!best || b.depth >= best->depth) best = &b;
    }
    return best;
}

const CodeBlock* CodeText::FindBlock(const std::string& id) const {
    const CodeBlock* fallback = nullptr;
    for (const CodeBlock& b : blocks_) {
        if (b.id != id) continue;
        if (b.kind == "begin") return &b;
        if (!fallback) fallback = &b;
    }
    return fallback;
}

bool CodeText::FindNext(const std::string& needle, bool case_sensitive, const CodeMatch& from, bool backward,
                        CodeMatch& out) const {
    if (needle.empty() || lines_.empty()) return false;
    const int n = (int)lines_.size();
    if (!backward) {
        int line = from.line < 0 ? 0 : from.line;
        int col = from.line < 0 ? 0 : from.col + 1;
        for (int step = 0; step <= n; ++step) {
            int li = (line + step) % n;
            int start = step == 0 ? col : 0;
            int p = FindInLine(lines_[(size_t)li].text, needle, case_sensitive, start);
            if (p >= 0) {
                out = {li, p, (int)needle.size()};
                return true;
            }
        }
        return false;
    }
    int line = from.line < 0 ? n - 1 : from.line;
    int col = from.line < 0 ? (int)lines_[(size_t)line].text.size() : from.col;
    for (int step = 0; step <= n; ++step) {
        int li = ((line - step) % n + n) % n;
        int before = step == 0 ? col : (int)lines_[(size_t)li].text.size();
        int p = FindLastInLine(lines_[(size_t)li].text, needle, case_sensitive, before);
        if (p >= 0) {
            out = {li, p, (int)needle.size()};
            return true;
        }
    }
    return false;
}

int CodeText::CountMatches(const std::string& needle, bool case_sensitive) const {
    if (needle.empty()) return 0;
    int count = 0;
    for (const CodeLine& ln : lines_) {
        int p = 0;
        while ((p = FindInLine(ln.text, needle, case_sensitive, p)) >= 0) {
            ++count;
            p += (int)needle.size();
        }
    }
    return count;
}

CodeTextDrawResult CodeText::Draw(const char* str_id, const ImVec2& size, const CodeTextDrawOptions& o) {
    CodeTextDrawResult res;
    const CodeTextColors col = DefaultCodeColors();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Colors().bg_frame);
    bool visible = ImGui::BeginChild(str_id, size, IMFORGE_CHILD_BORDER,
                                     ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!visible) {
        ImGui::EndChild();
        return res;
    }
    if (o.font) ImGui::PushFont(o.font);
    const float line_h = ImGui::GetTextLineHeight();
    const float char_w = ImGui::CalcTextSize("M").x;
    const int line_count = LineCount();
    if (measured_font_ != ImGui::GetFont() || measured_size_ != ImGui::GetFontSize()) {
        measured_font_ = ImGui::GetFont();
        measured_size_ = ImGui::GetFontSize();
        max_width_ = 0;
        for (const CodeLine& ln : lines_)
            max_width_ = std::max(max_width_, ImGui::CalcTextSize(ln.text.c_str(), ln.text.c_str() + ln.text.size()).x);
    }
    int digits = 1;
    for (int v = std::max(line_count, 1); v >= 10; v /= 10) ++digits;
    digits = std::min(std::max(digits, 3), 10);
    const float gutter_w = o.line_numbers ? char_w * (float)(digits + 2) : char_w;
    const float content_w = gutter_w + max_width_ + char_w * 2.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win_pos = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();
    const bool win_hovered = ImGui::IsWindowHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    res.hovered = win_hovered;
    if (o.scroll_to_line >= 0) ImGui::SetScrollY(std::max(0.0f, (float)o.scroll_to_line * line_h - win_size.y * 0.35f));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const std::string needle = o.find ? o.find : "";

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushClipRect(ImVec2(win_pos.x + (o.line_numbers ? gutter_w : 0.0f), win_pos.y),
                        ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y), true);
    ImGuiListClipper clipper;
    clipper.Begin(line_count, line_h);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const CodeLine& ln = lines_[(size_t)i];
            const float row_x0 = win_pos.x, row_x1 = win_pos.x + win_size.x;
            if (i >= o.highlight_begin && i <= o.highlight_end)
                dl->AddRectFilled(ImVec2(row_x0, p.y), ImVec2(row_x1, p.y + line_h), col.highlight_bg);
            bool row_hover = win_hovered && mouse.y >= p.y && mouse.y < p.y + line_h && mouse.x >= row_x0 && mouse.x < row_x1;
            if (row_hover) {
                res.hovered_line = i;
                if (o.clickable) {
                    dl->AddRectFilled(ImVec2(row_x0, p.y), ImVec2(row_x1, p.y + line_h), col.hover_bg);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) res.clicked_line = i;
                }
            }
            const float text_x = p.x + gutter_w;
            if (!needle.empty()) {
                int q = 0;
                while ((q = FindInLine(ln.text, needle, o.find_case_sensitive, q)) >= 0) {
                    float x0 = text_x + ImGui::CalcTextSize(ln.text.c_str(), ln.text.c_str() + q).x;
                    float x1 = x0 + ImGui::CalcTextSize(ln.text.c_str() + q, ln.text.c_str() + q + needle.size()).x;
                    bool cur = o.find_current.line == i && o.find_current.col == q;
                    dl->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + line_h), cur ? col.find_current_bg : col.find_bg, 2.0f);
                    q += (int)needle.size();
                }
            }
            float x = text_x;
            for (const CodeSpan& s : ln.spans) {
                const char* b = ln.text.data() + s.begin;
                const char* e = ln.text.data() + s.end;
                ImU32 c = col.plain;
                switch (s.kind) {
                case CodeTok::Keyword: c = col.keyword; break;
                case CodeTok::Type: c = col.type; break;
                case CodeTok::Function: c = col.function; break;
                case CodeTok::String: c = col.string; break;
                case CodeTok::Number: c = col.number; break;
                case CodeTok::Comment: c = col.comment; break;
                case CodeTok::Marker: c = col.marker; break;
                case CodeTok::Preproc: c = col.preproc; break;
                default: break;
                }
                if (ln.marker) c = col.marker;
                dl->AddText(ImVec2(x, p.y), c, b, e);
                x += ImGui::CalcTextSize(b, e).x;
            }
            ImGui::Dummy(ImVec2(content_w, line_h));
        }
    }
    ImGui::PopClipRect();
    ImGui::PopStyleVar();

    if (o.line_numbers && line_count > 0) {
        const float scroll_y = ImGui::GetScrollY();
        int first = std::max(0, (int)(scroll_y / line_h));
        int last = std::min(line_count - 1, first + (int)(win_size.y / line_h) + 1);
        dl->AddRectFilled(ImVec2(win_pos.x, win_pos.y), ImVec2(win_pos.x + gutter_w, win_pos.y + win_size.y), col.gutter_bg);
        char buf[16];
        for (int i = first; i <= last; ++i) {
            float y = origin.y + (float)i * line_h;
            if (i >= o.highlight_begin && i <= o.highlight_end)
                dl->AddRectFilled(ImVec2(win_pos.x, y), ImVec2(win_pos.x + 3.0f, y + line_h), col.highlight_bar);
            std::snprintf(buf, sizeof(buf), "%*d", digits, i + 1);
            dl->AddText(ImVec2(win_pos.x + char_w, y), col.line_number, buf);
        }
    }
    if (o.font) ImGui::PopFont();
    ImGui::EndChild();
    return res;
}

bool DrawCodeFindBar(const char* str_id, CodeFindState& st, const CodeText& text) {
    if (!st.open) return false;
    ImGui::PushID(str_id);
    bool jump = false;
    auto step = [&](bool backward) {
        CodeMatch m;
        if (text.FindNext(st.query, st.case_sensitive, st.current, backward, m)) {
            st.current = m;
            jump = true;
        } else {
            st.current = CodeMatch();
        }
    };
    if (st.focus) {
        ImGui::SetKeyboardFocusHere();
        st.focus = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    bool enter = InputTextWithHintStd("##find", "Find  (Enter next, Shift+Enter previous, Esc close)", st.query,
                                      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    bool input_active = ImGui::IsItemActive();
    if (ImGui::IsItemEdited()) {
        st.current = CodeMatch();
        st.match_count = text.CountMatches(st.query, st.case_sensitive);
        step(false);
    }
    if (enter) {
        step(ImGui::GetIO().KeyShift);
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Aa", &st.case_sensitive)) {
        st.current = CodeMatch();
        st.match_count = text.CountMatches(st.query, st.case_sensitive);
        step(false);
    }
    Tooltip("Match case");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##prev", ImGuiDir_Up)) step(true);
    Tooltip("Previous match");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Down)) step(false);
    Tooltip("Next match");
    ImGui::SameLine();
    if (st.query.empty()) TextDim("type to search");
    else if (st.match_count == 0) TextDim("no matches");
    else TextDim("%d match%s", st.match_count, st.match_count == 1 ? "" : "es");
    ImGui::SameLine();
    if (ImGui::SmallButton("x##close_find")) st.open = false;
    if (input_active && ImGui::IsKeyPressed(ImGuiKey_Escape)) st.open = false;
    ImGui::PopID();
    return jump;
}

}  // namespace imforge::builder::ui
