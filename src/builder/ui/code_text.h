#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#if IMGUI_VERSION_NUM >= 19110
#define IMFORGE_CHILD_BORDER ImGuiChildFlags_Borders
#elif IMGUI_VERSION_NUM >= 19000
#define IMFORGE_CHILD_BORDER ImGuiChildFlags_Border
#else
#define IMFORGE_CHILD_BORDER true
#endif

namespace imforge::builder::ui {

enum class CodeTok : unsigned char { Plain, Keyword, Type, Function, String, Number, Comment, Marker, Preproc };

struct CodeSpan {
    int begin = 0, end = 0;
    CodeTok kind = CodeTok::Plain;
};

struct CodeLine {
    std::string text;
    std::vector<CodeSpan> spans;
    bool marker = false;
};

struct CodeBlock {
    std::string kind, id;
    int line_begin = 0, line_end = 0, depth = 0;
};

struct CodeTextColors {
    ImU32 plain, keyword, type, function, string, number, comment, marker, preproc;
    ImU32 line_number, gutter_bg, highlight_bg, highlight_bar, find_bg, find_current_bg, hover_bg;
};
CodeTextColors DefaultCodeColors();

struct CodeMatch {
    int line = -1, col = -1, len = 0;
};

struct CodeTextDrawOptions {
    ImFont* font = nullptr;
    bool line_numbers = true;
    bool clickable = true;
    int highlight_begin = -1, highlight_end = -1;
    const char* find = nullptr;
    bool find_case_sensitive = false;
    CodeMatch find_current;
    int scroll_to_line = -1;
};

struct CodeTextDrawResult {
    int clicked_line = -1, hovered_line = -1;
    bool hovered = false;
};

class CodeText {
public:
    void SetText(const std::string& text, int tab_width = 4);
    void Clear();
    const std::string& Text() const { return text_; }
    bool Empty() const { return lines_.empty(); }
    int LineCount() const { return (int)lines_.size(); }
    const CodeLine& Line(int i) const { return lines_[(size_t)i]; }
    std::string SourceLines(int begin, int end) const;
    const std::vector<CodeBlock>& Blocks() const { return blocks_; }
    const CodeBlock* BlockAtLine(int line) const;
    const CodeBlock* FindBlock(const std::string& id) const;
    bool FindNext(const std::string& needle, bool case_sensitive, const CodeMatch& from, bool backward,
                  CodeMatch& out) const;
    int CountMatches(const std::string& needle, bool case_sensitive) const;
    CodeTextDrawResult Draw(const char* str_id, const ImVec2& size, const CodeTextDrawOptions& opts);

private:
    std::string text_;
    std::vector<CodeLine> lines_;
    std::vector<size_t> line_offsets_;
    std::vector<CodeBlock> blocks_;
    ImFont* measured_font_ = nullptr;
    float measured_size_ = 0, max_width_ = 0;
};

struct CodeFindState {
    bool open = false, focus = false, case_sensitive = false;
    std::string query;
    CodeMatch current;
    int match_count = 0;
};

bool DrawCodeFindBar(const char* str_id, CodeFindState& st, const CodeText& text);
std::string StripEventSuffix(const std::string& block_id);

}  // namespace imforge::builder::ui
