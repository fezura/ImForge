#include "codegen.h"
#include "roundtrip_internal.h"

#include <cstdlib>
#include <cstring>

namespace imforge {
namespace roundtrip {

namespace detail {

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        size_t end = nl == std::string::npos ? text.size() : nl;
        size_t len = end - start;
        if (len > 0 && text[end - 1] == '\r') --len;
        lines.push_back(text.substr(start, len));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string JoinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) {
        out += l;
        out += '\n';
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

bool StartsWith(const std::string& s, const char* prefix) {
    size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

namespace {

const char kMarkerTag[] = "@imb:";

size_t MarkerTagOffset(const std::string& line) {
    size_t p = 0;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
    if (line.compare(p, 2, "//") != 0) return std::string::npos;
    p += 2;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
    if (line.compare(p, 5, kMarkerTag) != 0) return std::string::npos;
    return p;
}

std::string ReadWord(const std::string& s, size_t& p) {
    size_t b = p;
    while (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\r' && s[p] != '=') ++p;
    return s.substr(b, p - b);
}

// skip string/char literals so a "/*" inside one doesn't open a comment
bool UpdateCommentState(const std::string& line, bool in_comment) {
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_comment) {
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                in_comment = false;
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return false;
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            in_comment = true;
            ++i;
        } else if (c == '"' || c == '\'') {
            for (++i; i < line.size() && line[i] != c; ++i)
                if (line[i] == '\\') ++i;
        }
    }
    return in_comment;
}

void SkipSpaces(const std::string& s, size_t& p) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r')) ++p;
}

std::string ReadValue(const std::string& s, size_t& p) {
    size_t b = p;
    while (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\r') ++p;
    return s.substr(b, p - b);
}

}  // namespace

bool NextCommentState(const std::string& line, bool in_comment) { return UpdateCommentState(line, in_comment); }

bool IsMarkerLine(const std::string& line) {
    return MarkerTagOffset(line) != std::string::npos;
}

bool ParseMarker(const std::string& line, Marker& out, std::string* err) {
    if (err) err->clear();
    size_t p = MarkerTagOffset(line);
    if (p == std::string::npos) return false;
    p += 5;
    std::string kind = ReadWord(line, p);
    Marker m;
    if (kind == "file") m.kind = Marker::Kind::File;
    else if (kind == "begin") m.kind = Marker::Kind::Begin;
    else if (kind == "end") m.kind = Marker::Kind::End;
    else if (kind == "user") m.kind = Marker::Kind::User;
    else if (kind == "locked") m.kind = Marker::Kind::Locked;
    else {
        if (err) *err = "unknown marker '@imb:" + kind + "'";
        return false;
    }
    while (true) {
        SkipSpaces(line, p);
        if (p >= line.size()) break;
        std::string key = ReadWord(line, p);
        std::string value;
        if (p < line.size() && line[p] == '=') {
            ++p;
            value = ReadValue(line, p);
        }
        if (key == "id") m.id = value;
        else if (key == "h") m.hash = value;
        else if (key == "panel") m.panel = value;
        else if (key == "schema") m.schema = std::atoi(value.c_str());
    }
    if (m.kind == Marker::Kind::File) {
        if (m.panel.empty()) {
            if (err) *err = "@imb:file marker without panel=";
            return false;
        }
    } else if (m.id.empty()) {
        if (err) *err = "@imb:" + kind + " marker without id=";
        return false;
    }
    out = m;
    return true;
}

std::string SetMarkerHash(const std::string& line, const std::string& hash) {
    size_t tag = MarkerTagOffset(line);
    if (tag == std::string::npos) return line;
    size_t h = line.find(" h=", tag);
    if (h == std::string::npos) h = line.find("\th=", tag);
    if (h == std::string::npos) {
        size_t e = line.size();
        while (e > 0 && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) --e;
        return line.substr(0, e) + " h=" + hash;
    }
    size_t vb = h + 3, ve = vb;
    while (ve < line.size() && line[ve] != ' ' && line[ve] != '\t' && line[ve] != '\r') ++ve;
    return line.substr(0, vb) + hash + line.substr(ve);
}

std::string SetMarkerKind(const std::string& line, const char* kind) {
    size_t tag = MarkerTagOffset(line);
    if (tag == std::string::npos) return line;
    size_t kb = tag + 5, ke = kb;
    while (ke < line.size() && line[ke] != ' ' && line[ke] != '\t' && line[ke] != '\r') ++ke;
    return line.substr(0, kb) + kind + line.substr(ke);
}

bool ChildPlaceholder(const std::string& line, std::string* id_out) {
    std::string t = Trim(line);
    if (!StartsWith(t, "@child:")) return false;
    if (id_out) *id_out = Trim(t.substr(7));
    return true;
}

std::string ChildPlaceholderLine(const std::string& like_line, const std::string& id) {
    size_t p = 0;
    while (p < like_line.size() && (like_line[p] == ' ' || like_line[p] == '\t')) ++p;
    return like_line.substr(0, p) + "@child:" + id;
}

void ReplaceBody(std::vector<std::string>& lines, int line_begin, int line_end,
                 const std::vector<std::string>& body) {
    if (line_begin < 1 || line_end <= line_begin || line_end > (int)lines.size()) return;
    lines.erase(lines.begin() + line_begin, lines.begin() + (line_end - 1));
    lines.insert(lines.begin() + line_begin, body.begin(), body.end());
}

bool HasUserOrLockedAncestor(const ParsedFile& file, int block_index) {
    int p = file.blocks[(size_t)block_index].parent;
    while (p >= 0) {
        Block::Kind k = file.blocks[(size_t)p].kind;
        if (k == Block::Kind::User || k == Block::Kind::Locked) return true;
        p = file.blocks[(size_t)p].parent;
    }
    return false;
}

}  // namespace detail

const Block* ParsedFile::Find(Block::Kind kind, const std::string& id) const {
    if (indexed_ > blocks.size()) {
        index_.clear();
        indexed_ = 0;
    }
    for (; indexed_ < blocks.size(); ++indexed_)
        index_.emplace(std::make_pair((int)blocks[indexed_].kind, blocks[indexed_].id), indexed_);
    auto it = index_.find(std::make_pair((int)kind, id));
    return it == index_.end() ? nullptr : &blocks[it->second];
}

std::string NormalizeForHash(const std::string& own_body) {
    return codegen::NormalizeForHash(own_body);
}

std::string Fnv1a32Hex(const std::string& normalized_body) {
    return codegen::Fnv1a32Hex(normalized_body);
}

std::string BlockHash(const Block& block) { return Fnv1a32Hex(NormalizeForHash(block.own_body)); }

namespace {

void SetError(Error* err, const std::string& message, int line) {
    if (!err) return;
    err->message = message;
    err->line = line;
    err->col = 0;
}

void FinishBlock(ParsedFile& out, const std::vector<std::string>& lines, size_t index) {
    Block& b = out.blocks[index];
    std::vector<size_t> kids;
    for (size_t k = index + 1; k < out.blocks.size(); ++k)
        if (out.blocks[k].parent == (int)index && out.blocks[k].kind != Block::Kind::User)
            kids.push_back(k);
    std::string body, own;
    size_t kid = 0;
    for (int ln = b.line_begin + 1; ln < b.line_end; ++ln) {
        const std::string& text = lines[(size_t)ln - 1];
        body += text;
        body += '\n';
        if (kid >= kids.size() || out.blocks[kids[kid]].line_begin != ln) {
            own += text;
            own += '\n';
            continue;
        }
        const Block& child = out.blocks[kids[kid]];
        own += detail::ChildPlaceholderLine(text, child.id);
        own += '\n';
        for (int k = ln + 1; k <= child.line_end; ++k) {
            body += lines[(size_t)k - 1];
            body += '\n';
        }
        ln = child.line_end;
        ++kid;
    }
    b.body = body;
    b.own_body = own;
}

}  // namespace

bool ParseGenerated(const std::string& text, ParsedFile& out, Error* err) {
    out = ParsedFile();
    out.full_text = text;
    std::vector<std::string> lines = detail::SplitLines(text);
    std::vector<size_t> stack;
    bool in_comment = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        const bool inside_comment = in_comment;
        in_comment = detail::NextCommentState(line, in_comment);
        if (inside_comment || !detail::IsMarkerLine(line)) continue;
        int ln = (int)i + 1;
        detail::Marker m;
        std::string msg;
        if (!detail::ParseMarker(line, m, &msg)) {
            SetError(err, msg, ln);
            return false;
        }
        if (m.kind == detail::Marker::Kind::File) {
            if (out.panel.empty()) {
                out.panel = m.panel;
                out.schema = m.schema;
            }
            Block b;
            b.kind = Block::Kind::File;
            b.id = m.panel;
            b.line_begin = b.line_end = ln;
            b.parent = stack.empty() ? -1 : (int)stack.back();
            out.blocks.push_back(b);
            continue;
        }
        if (m.kind == detail::Marker::Kind::End) {
            if (stack.empty()) {
                SetError(err, "@imb:end id=" + m.id + " without an open block", ln);
                return false;
            }
            Block& open = out.blocks[stack.back()];
            if (open.id != m.id) {
                SetError(err,
                         "@imb:end id=" + m.id + " does not match open block '" + open.id +
                             "' (line " + std::to_string(open.line_begin) + ")",
                         ln);
                return false;
            }
            open.line_end = ln;
            FinishBlock(out, lines, stack.back());
            stack.pop_back();
            continue;
        }
        Block b;
        b.kind = m.kind == detail::Marker::Kind::Begin  ? Block::Kind::Begin
                 : m.kind == detail::Marker::Kind::User ? Block::Kind::User
                                                        : Block::Kind::Locked;
        if (const Block* dup = out.Find(b.kind, m.id)) {
            SetError(err,
                     "duplicate block id '" + m.id + "' (first at line " +
                         std::to_string(dup->line_begin) + ")",
                     ln);
            return false;
        }
        b.id = m.id;
        b.hash = m.hash;
        b.line_begin = ln;
        b.parent = stack.empty() ? -1 : (int)stack.back();
        if (b.parent >= 0 && b.kind != Block::Kind::User)
            out.blocks[(size_t)b.parent].children.push_back(b.id);
        out.blocks.push_back(b);
        stack.push_back(out.blocks.size() - 1);
    }
    if (!stack.empty()) {
        const Block& open = out.blocks[stack.back()];
        SetError(err, "block '" + open.id + "' is never closed", open.line_begin);
        return false;
    }
    return true;
}

}  // namespace roundtrip
}  // namespace imforge
