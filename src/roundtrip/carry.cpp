#include "roundtrip_internal.h"

#include <algorithm>
#include <cctype>

namespace imforge {
namespace roundtrip {

namespace {

size_t IndentOf(const std::string& line) {
    size_t k = 0;
    while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
    return k;
}

bool IsBlankLine(const std::string& line) { return IndentOf(line) == line.size(); }

// matches `static void Panel_id_Block(State& state) {` ... `}`, what codegen emits for locked nodes
std::string WrapperName(const std::vector<std::string>& body) {
    if (body.size() < 2) return std::string();
    std::string first = detail::Trim(body.front());
    if (detail::Trim(body.back()) != "}" || first.empty() || first.back() != '{') return "";
    size_t paren = first.find('(');
    if (paren == std::string::npos || paren == 0) return std::string();
    size_t b = paren;
    while (b > 0 && (std::isalnum((unsigned char)first[b - 1]) || first[b - 1] == '_')) --b;
    return b == paren ? std::string() : first.substr(b, paren - b);
}

// a block locked in place by ApplyResolution still holds the old inline code, so once codegen
// emits a wrapper that code has to move inside it
std::vector<std::string> LockedBody(const std::vector<std::string>& fresh_body,
                                    const std::vector<std::string>& user_body) {
    std::string name = WrapperName(fresh_body);
    if (name.empty()) return user_body;
    for (const std::string& l : user_body)
        if (l.find(name) != std::string::npos) return user_body;
    std::string target = "    ";
    for (size_t i = 1; i + 1 < fresh_body.size(); ++i)
        if (!IsBlankLine(fresh_body[i])) {
            target = fresh_body[i].substr(0, IndentOf(fresh_body[i]));
            break;
        }
    size_t strip = std::string::npos;
    for (const std::string& l : user_body)
        if (!IsBlankLine(l)) strip = std::min(strip, IndentOf(l));
    if (strip == std::string::npos) strip = 0;
    std::vector<std::string> out;
    out.push_back(fresh_body.front());
    for (const std::string& l : user_body)
        out.push_back(IsBlankLine(l) ? std::string() : target + l.substr(strip));
    out.push_back(fresh_body.back());
    return out;
}

std::string RehashLocked(const std::string& text) {
    ParsedFile pf;
    if (!ParseGenerated(text, pf, nullptr)) return text;
    std::vector<std::string> lines = detail::SplitLines(text);
    for (const Block& b : pf.blocks)
        if (b.kind == Block::Kind::Locked)
            lines[(size_t)b.line_begin - 1] =
                detail::SetMarkerHash(lines[(size_t)b.line_begin - 1], BlockHash(b));
    return detail::JoinLines(lines);
}

}  // namespace

std::string CarryUserRegions(const std::string& fresh, const ParsedFile& existing,
                             std::vector<std::string>* carried_ids) {
    ParsedFile pf;
    if (!ParseGenerated(fresh, pf, nullptr)) return fresh;
    std::vector<std::string> lines = detail::SplitLines(fresh);
    std::vector<size_t> targets;
    for (size_t i = 0; i < pf.blocks.size(); ++i) {
        const Block& b = pf.blocks[i];
        if (b.kind != Block::Kind::User && b.kind != Block::Kind::Locked) continue;
        if (detail::HasUserOrLockedAncestor(pf, (int)i)) continue;
        if (!existing.Find(b.kind, b.id)) continue;
        targets.push_back(i);
    }
    std::vector<std::string> rebuilt;
    rebuilt.reserve(lines.size());
    size_t cursor = 0;
    for (size_t k = 0; k < targets.size(); ++k) {
        const Block& b = pf.blocks[targets[k]];
        const Block* eb = existing.Find(b.kind, b.id);
        std::vector<std::string> body = detail::SplitLines(eb->body);
        if (b.kind == Block::Kind::Locked) body = LockedBody(detail::SplitLines(b.body), body);
        if (b.line_end <= b.line_begin || (size_t)b.line_end > lines.size() || (size_t)b.line_begin < cursor) continue;
        for (; cursor < (size_t)b.line_begin; ++cursor) rebuilt.push_back(std::move(lines[cursor]));
        for (std::string& l : body) rebuilt.push_back(std::move(l));
        cursor = (size_t)b.line_end - 1;
    }
    for (; cursor < lines.size(); ++cursor) rebuilt.push_back(std::move(lines[cursor]));
    lines.swap(rebuilt);
    if (carried_ids)
        for (size_t i : targets) carried_ids->push_back(pf.blocks[i].id);
    return RehashLocked(detail::JoinLines(lines));
}

std::string RehashBlocks(const std::string& text) {
    ParsedFile pf;
    if (!ParseGenerated(text, pf, nullptr)) return text;
    std::vector<std::string> lines = detail::SplitLines(text);
    for (const Block& b : pf.blocks) {
        if (b.kind != Block::Kind::Begin && b.kind != Block::Kind::Locked) continue;
        std::string& marker = lines[(size_t)b.line_begin - 1];
        marker = detail::SetMarkerHash(marker, BlockHash(b));
    }
    return detail::JoinLines(lines);
}

std::string ApplyResolution(const std::string& merged_source, const Conflict& conflict,
                            bool keep_user_code) {
    ParsedFile pf;
    if (!ParseGenerated(merged_source, pf, nullptr)) return merged_source;
    const Block* b = pf.Find(Block::Kind::Begin, conflict.node_id);
    if (!b) return merged_source;
    std::vector<std::string> lines = detail::SplitLines(merged_source);
    const std::string& body = keep_user_code ? conflict.user_code : conflict.generated_code;
    detail::ReplaceBody(lines, b->line_begin, b->line_end, detail::SplitLines(body));
    if (keep_user_code) {
        std::string& marker = lines[(size_t)b->line_begin - 1];
        marker = detail::SetMarkerKind(marker, "locked");
    }
    std::string text = detail::JoinLines(lines);
    ParsedFile out;
    if (!ParseGenerated(text, out, nullptr)) return text;
    const Block* r = out.Find(keep_user_code ? Block::Kind::Locked : Block::Kind::Begin, conflict.node_id);
    if (!r) return text;
    for (const Block& nb : out.blocks) {
        if (nb.kind != Block::Kind::Begin && nb.kind != Block::Kind::Locked) continue;
        if (nb.line_begin < r->line_begin || nb.line_end > r->line_end) continue;
        std::string& marker = lines[(size_t)nb.line_begin - 1];
        marker = detail::SetMarkerHash(marker, BlockHash(nb));
    }
    return detail::JoinLines(lines);
}

}  // namespace roundtrip
}  // namespace imforge
