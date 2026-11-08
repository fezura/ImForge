#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <utility>

#include "emit_ctx.h"

namespace imforge {
namespace codegen {

namespace {

const char kMarkStart = '\x01';
const char kMarkEnd = '\x02';

bool IsIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsIdentChar(char c) { return IsIdentStart(c) || IsDigit(c); }

bool PrevAllowsUnaryMinus(const std::vector<Token>& toks) {
    if (toks.empty()) return true;
    const Token& t = toks.back();
    if (t.kind == Token::Kind::Ident || t.kind == Token::Kind::Number || t.kind == Token::Kind::String)
        return false;
    return !(t.text == ")" || t.text == "]");
}

}  // namespace

std::string Fnv1a32Hex(const std::string& text) {
    unsigned h = 2166136261u;
    for (unsigned char c : text) {
        h ^= c;
        h *= 16777619u;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return buf;
}

std::string NormalizeForHash(const std::string& body) {
    std::string out;
    out.reserve(body.size());
    bool at_line_start = true;
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '\r') continue;
        if (at_line_start && (c == ' ' || c == '\t')) continue;
        out.push_back(c);
        at_line_start = c == '\n';
    }
    return out;
}

std::vector<Token> Tokenize(const std::string& s) {
    std::vector<Token> toks;
    size_t i = 0, n = s.size();
    int line = 1;
    bool line_start = true;
    while (i < n) {
        char c = s[i];
        if (c == '\n') {
            ++line;
            line_start = true;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) {
                if (s[i] == '\n') ++line;
                ++i;
            }
            i = i + 2 <= n ? i + 2 : n;
            continue;
        }
        Token t;
        t.offset = i;
        t.line = line;
        if (c == '#' && line_start) {
            size_t e = i;
            while (e < n && s[e] != '\n') ++e;
            t.kind = Token::Kind::Directive;
            t.text = s.substr(i, e - i);
            while (!t.text.empty() && (t.text.back() == ' ' || t.text.back() == '\r')) t.text.pop_back();
            i = e;
        } else if (c == '@' && s.compare(i, 7, "@child:") == 0) {
            size_t e = i + 7;
            while (e < n && IsIdentChar(s[e])) ++e;
            t.kind = Token::Kind::ChildRef;
            t.text = s.substr(i, e - i);
            i = e;
        } else if (IsIdentStart(c)) {
            size_t e = i;
            while (e < n && IsIdentChar(s[e])) ++e;
            t.kind = Token::Kind::Ident;
            t.text = s.substr(i, e - i);
            i = e;
        } else if (IsDigit(c) || (c == '.' && i + 1 < n && IsDigit(s[i + 1])) ||
                   (c == '-' && i + 1 < n && (IsDigit(s[i + 1]) || (s[i + 1] == '.' && i + 2 < n && IsDigit(s[i + 2]))) &&
                    PrevAllowsUnaryMinus(toks))) {
            size_t e = i + 1;
            bool hex = c == '0' && e < n && (s[e] == 'x' || s[e] == 'X');
            if (hex) ++e;
            while (e < n) {
                char d = s[e];
                if (IsDigit(d) || d == '.' || d == '\'' || (hex && ((d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F')))) {
                    ++e;
                } else if (!hex && (d == 'e' || d == 'E') && e + 1 < n && (IsDigit(s[e + 1]) || s[e + 1] == '+' || s[e + 1] == '-')) {
                    e += 2;
                } else if (d == 'f' || d == 'F' || d == 'u' || d == 'U' || d == 'l' || d == 'L') {
                    ++e;
                } else {
                    break;
                }
            }
            t.kind = Token::Kind::Number;
            t.text = s.substr(i, e - i);
            i = e;
        } else if (c == '"' || c == '\'') {
            size_t e = i + 1;
            while (e < n && s[e] != c) {
                if (s[e] == '\\' && e + 1 < n) ++e;
                if (s[e] == '\n') ++line;
                ++e;
            }
            e = e < n ? e + 1 : n;
            t.kind = Token::Kind::String;
            t.text = s.substr(i, e - i);
            i = e;
        } else {
            static const char* const kMulti[] = {"<<=", ">>=", "->*", "...", "::", "->", "<<", ">>", "<=", ">=",
                                                 "==", "!=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=",
                                                 "%=", "&=", "|=", "^=", ".*"};
            size_t len = 1;
            for (const char* m : kMulti) {
                size_t ml = std::strlen(m);
                if (s.compare(i, ml, m) == 0) {
                    len = ml;
                    break;
                }
            }
            t.kind = Token::Kind::Punct;
            t.text = s.substr(i, len);
            i += len;
        }
        line_start = false;
        toks.push_back(std::move(t));
    }
    return toks;
}

std::string Writer::IndentString() const {
    if (indent <= 0) return std::string();
    if (opts && opts->settings.use_tabs) return std::string(static_cast<size_t>(indent), '\t');
    int width = opts ? opts->settings.indent : 4;
    if (width <= 0) width = 4;
    return std::string(static_cast<size_t>(indent * width), ' ');
}

void Writer::Raw(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        size_t m = text.find(kMarkStart, i);
        if (m == std::string::npos) {
            out.append(text, i, std::string::npos);
            break;
        }
        out.append(text, i, m - i);
        size_t e = text.find(kMarkEnd, m);
        if (e == std::string::npos) {
            i = m + 1;
            continue;
        }
        size_t idx = static_cast<size_t>(std::atoi(text.c_str() + m + 1));
        if (idx < marks.size()) marks[idx].offset = out.size();
        i = e + 1;
    }
}

void Writer::Line(const std::string& text) {
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        std::string part = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!part.empty()) {
            bool blank = part.find_first_not_of(" \t\r") == std::string::npos;
            if (!blank && part[0] != '#') out += IndentString();
            Raw(part);
        }
        out.push_back('\n');
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

void Writer::Blank() {
    if (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') return;
    out.push_back('\n');
}

int Writer::TokenIndex() const { return static_cast<int>(Tokenize(out).size()); }

bool Writer::MarkersOn() const { return (!opts || opts->settings.markers) && suppress_markers == 0; }

void Writer::BeginBlock(const std::string& id, const char* kind) {
    Block b;
    b.id = id;
    b.kind = kind;
    b.parent = open_blocks.empty() ? -1 : open_blocks.back();
    b.begin_off = out.size();
    if (MarkersOn()) {
        std::string line = std::string("// @imb:") + kind + " id=" + id + " h=";
        out += IndentString();
        out += line;
        b.hash_off = out.size();
        out += "00000000\n";
    } else {
        b.hash_off = std::string::npos;
    }
    b.body_off = out.size();
    int index = static_cast<int>(blocks.size());
    if (b.parent >= 0) blocks[static_cast<size_t>(b.parent)].children.push_back(index);
    blocks.push_back(std::move(b));
    open_blocks.push_back(index);
}

void Writer::EndBlock() {
    if (open_blocks.empty()) return;
    Block& b = blocks[static_cast<size_t>(open_blocks.back())];
    open_blocks.pop_back();
    b.end_off = out.size();
    if (b.hash_off != std::string::npos) {
        std::string body;
        size_t pos = b.body_off;
        for (int ci : b.children) {
            const Block& c = blocks[static_cast<size_t>(ci)];
            if (c.begin_off < pos) continue;
            body.append(out, pos, c.begin_off - pos);
            body += "@child:" + c.id + "\n";
            pos = c.close_off;
        }
        if (pos < b.end_off) body.append(out, pos, b.end_off - pos);
        out.replace(b.hash_off, 8, Fnv1a32Hex(NormalizeForHash(body)));
        out += IndentString();
        out += "// @imb:end id=" + b.id + "\n";
    }
    b.close_off = out.size();
    b.closed = true;
}

void Writer::UserSlot(const std::string& id) {
    if (!MarkersOn()) return;
    Line("// @imb:user id=" + id);
    Line("// @imb:end id=" + id);
}

std::string Writer::MarkToken(const std::string& node_id, const std::string& prop) {
    Mark m;
    m.node_id = node_id;
    m.prop = prop;
    marks.push_back(m);
    return std::string(1, kMarkStart) + std::to_string(marks.size() - 1) + std::string(1, kMarkEnd);
}

std::string Writer::StripMarks(const std::string& text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t m = text.find(kMarkStart, i);
        if (m == std::string::npos) {
            out.append(text, i, std::string::npos);
            break;
        }
        out.append(text, i, m - i);
        size_t e = text.find(kMarkEnd, m);
        i = e == std::string::npos ? m + 1 : e + 1;
    }
    return out;
}

void Writer::MarkNextLiteral(const std::string& node_id, const std::string& prop) {
    Mark m;
    m.node_id = node_id;
    m.prop = prop;
    m.offset = out.size();
    m.next_literal = true;
    marks.push_back(m);
}

void Writer::InsertAt(size_t offset, const std::string& text) {
    if (offset > out.size()) offset = out.size();
    out.insert(offset, text);
    size_t d = text.size();
    for (Mark& m : marks)
        if (m.offset != std::string::npos && m.offset >= offset) m.offset += d;
    for (Block& b : blocks) {
        if (b.begin_off >= offset) b.begin_off += d;
        if (b.body_off >= offset) b.body_off += d;
        if (b.hash_off != std::string::npos && b.hash_off >= offset) b.hash_off += d;
        if (b.closed) {
            if (b.end_off >= offset) b.end_off += d;
            if (b.close_off >= offset) b.close_off += d;
        }
    }
}

void Writer::Prepend(const std::string& text) { InsertAt(0, text); }

void Writer::Finalize(const std::string& file, std::vector<LiteralRef>& result) const {
    std::vector<Token> toks = Tokenize(out);
    auto token_at = [&](size_t offset, bool inline_mark) -> int {
        size_t lo = 0, hi = toks.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (toks[mid].offset + toks[mid].text.size() <= offset && toks[mid].offset < offset) lo = mid + 1;
            else hi = mid;
        }
        for (size_t i = lo; i < toks.size(); ++i) {
            const Token& t = toks[i];
            // a bare enum name from a widget manifest (Online, not ImGuiDir_Left) is the literal itself,
            // don't run on and claim the next string or number for this prop
            if (inline_mark && i == lo && t.offset == offset && t.kind == Token::Kind::Ident &&
                (i + 1 >= toks.size() || (toks[i + 1].text != "(" && toks[i + 1].text != "::")))
                return static_cast<int>(i);
            bool is_flag = t.kind == Token::Kind::Ident &&
                           (t.text.compare(0, 5, "ImGui") == 0 || t.text == "true" || t.text == "false");
            if (t.kind == Token::Kind::String || t.kind == Token::Kind::Number || is_flag) return static_cast<int>(i);
            if (t.kind == Token::Kind::Punct && t.text == ";") return -1;
        }
        return -1;
    };
    auto block_relative = [&](int tok) -> int {
        const Token& t = toks[static_cast<size_t>(tok)];
        int best = -1;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const Block& b = blocks[i];
            if (!b.closed || b.hash_off == std::string::npos) continue;
            if (t.offset >= b.body_off && t.offset < b.end_off) best = static_cast<int>(i);
        }
        if (best < 0) return -1;
        const Block& b = blocks[static_cast<size_t>(best)];
        int rel = 0;
        size_t first = 0, last = static_cast<size_t>(tok);
        while (first < last) {
            size_t mid = (first + last) / 2;
            if (toks[mid].offset < b.body_off) first = mid + 1;
            else last = mid;
        }
        for (size_t i = first; i < static_cast<size_t>(tok); ++i) {
            const Token& x = toks[i];
            bool in_child = false;
            for (int ci : b.children) {
                const Block& c = blocks[static_cast<size_t>(ci)];
                if (x.offset >= c.begin_off && x.offset < c.close_off) {
                    in_child = true;
                    break;
                }
            }
            if (!in_child) ++rel;
        }
        for (int ci : b.children) {
            const Block& c = blocks[static_cast<size_t>(ci)];
            if (c.close_off <= t.offset) ++rel;
        }
        return rel;
    };
    std::set<std::pair<std::string, std::string>> seen;
    for (const LiteralRef& r : result)
        if (r.file == file) seen.emplace(r.node_id, r.prop);
    for (const Mark& m : marks) {
        if (m.offset == std::string::npos) continue;
        int tok = token_at(m.offset, !m.next_literal);
        if (tok < 0) continue;
        if (!seen.emplace(m.node_id, m.prop).second) continue;
        LiteralRef ref;
        ref.node_id = m.node_id;
        ref.prop = m.prop;
        ref.token_index = tok;
        ref.file = file;
        ref.block_token_index = block_relative(tok);
        result.push_back(ref);
    }
}

std::string TextFmt::CallArgs() const {
    std::string r = fmt;
    for (const std::string& a : args) r += ", " + a;
    return r;
}

}  // namespace codegen
}  // namespace imforge
