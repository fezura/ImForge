#include "codegen.h"
#include "roundtrip_internal.h"

#include <map>

namespace imforge {
namespace roundtrip {

namespace {

enum class Decision { Keep, ReplaceOwn, ReplaceWhole };

struct BlockDecision {
    Decision decision = Decision::Keep;
    const Block* existing = nullptr;
};

using Decisions = std::map<std::string, BlockDecision>;

Diagnostic MakeDiag(Diagnostic::Level level, const std::string& panel, const std::string& node_id,
                    const std::string& message) {
    Diagnostic d;
    d.level = level;
    d.message = message;
    d.panel = panel;
    d.node_id = node_id;
    return d;
}

bool TokensEqual(const std::vector<Token>& a, const std::vector<Token>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].text != b[i].text) return false;
    return true;
}

bool IsBlank(const std::string& s) {
    for (char c : s)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    return true;
}

std::string FileName(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

const char* KindName(Block::Kind k) {
    switch (k) {
        case Block::Kind::User: return "user";
        case Block::Kind::Locked: return "locked";
        case Block::Kind::File: return "file";
        default: return "begin";
    }
}

std::string JoinIds(const std::vector<std::string>& ids) {
    std::string s;
    for (const std::string& id : ids) s += (s.empty() ? "" : ", ") + id;
    return s.empty() ? std::string("none") : s;
}

struct Renderer {
    const ParsedFile& fresh;
    const std::vector<std::string>& lines;
    const Decisions& decisions;
    std::vector<std::string> out;

    void RenderBlock(const Block& fb) {
        out.push_back(lines[(size_t)fb.line_begin - 1]);
        auto it = decisions.find(fb.id);
        const BlockDecision* d = it == decisions.end() ? nullptr : &it->second;
        if (d && d->decision == Decision::ReplaceWhole) {
            for (const std::string& l : detail::SplitLines(d->existing->body)) out.push_back(l);
        } else {
            const std::string& own =
                d && d->decision == Decision::ReplaceOwn ? d->existing->own_body : fb.own_body;
            for (const std::string& l : detail::SplitLines(own)) {
                std::string cid;
                if (!detail::ChildPlaceholder(l, &cid)) {
                    out.push_back(l);
                    continue;
                }
                if (const Block* c = fresh.Find(Block::Kind::Begin, cid)) RenderBlock(*c);
            }
        }
        out.push_back(lines[(size_t)fb.line_end - 1]);
    }

    void RenderFile() {
        std::map<int, const Block*> tops;
        for (const Block& b : fresh.blocks)
            if (b.kind == Block::Kind::Begin && b.parent < 0) tops[b.line_begin] = &b;
        size_t i = 0;
        while (i < lines.size()) {
            auto it = tops.find((int)i + 1);
            if (it == tops.end()) {
                out.push_back(lines[i]);
                ++i;
                continue;
            }
            RenderBlock(*it->second);
            i = (size_t)it->second->line_end;
        }
    }
};

bool TryLiteralMerge(Panel& panel, const Block& eb, const Block& fb, const std::vector<Token>& ut,
                     const std::vector<Token>& ft, const codegen::Result& fresh,
                     const std::string& path, std::string& reason) {
    if (eb.children != fb.children) {
        reason = "child blocks differ (user: " + JoinIds(eb.children) +
                 "; generated: " + JoinIds(fb.children) + ")";
        return false;
    }
    Node* node = FindNode(panel.root, eb.id);
    if (!node) {
        reason = "node '" + eb.id + "' does not exist in the panel";
        return false;
    }
    std::vector<codegen::LiteralRef> refs;
    for (const codegen::LiteralRef& r : fresh.literals) {
        if (r.node_id != eb.id) continue;
        if (!r.file.empty() && r.file != path && FileName(r.file) != FileName(path)) continue;
        refs.push_back(r);
    }
    std::vector<LiteralEdit> edits;
    if (!ExtractLiteralEdits(node, ut, ft, refs, edits, &reason)) return false;
    if (edits.empty()) {
        reason = "code differs from the generated block but no literal changed";
        return false;
    }
    return ApplyLiteralEdits(*node, edits, &reason);
}

// conflict blocks keep their old hash so the next merge still sees them as edited, until
// ApplyResolution settles them
std::string RestoreConflictHashes(const std::string& text, const std::map<std::string, std::string>& hashes) {
    if (hashes.empty()) return text;
    ParsedFile pf;
    if (!ParseGenerated(text, pf, nullptr)) return text;
    std::vector<std::string> lines = detail::SplitLines(text);
    for (const Block& b : pf.blocks) {
        if (b.kind != Block::Kind::Begin) continue;
        auto it = hashes.find(b.id);
        if (it == hashes.end()) continue;
        std::string h = it->second;
        if (h.empty() || h == BlockHash(b)) h = BlockHash(b) == "00000000" ? "ffffffff" : "00000000";
        std::string& marker = lines[(size_t)b.line_begin - 1];
        marker = detail::SetMarkerHash(marker, h);
    }
    return detail::JoinLines(lines);
}

void Append(MergeReport& into, const MergeReport& part) {
    into.updated_nodes.insert(into.updated_nodes.end(), part.updated_nodes.begin(),
                              part.updated_nodes.end());
    into.conflicts.insert(into.conflicts.end(), part.conflicts.begin(), part.conflicts.end());
    into.diagnostics.insert(into.diagnostics.end(), part.diagnostics.begin(),
                            part.diagnostics.end());
}

}  // namespace

int FindFreshFile(const ParsedFile& existing, const codegen::Result& fresh,
                  const std::string& panel_name) {
    int best = -1, best_score = -1;
    bool existing_header = existing.full_text.find("#pragma once") != std::string::npos;
    for (size_t k = 0; k < fresh.files.size(); ++k) {
        ParsedFile pf;
        if (!ParseGenerated(fresh.files[k].content, pf, nullptr)) continue;
        if (pf.panel != panel_name) continue;
        int score = 0;
        for (const Block& b : existing.blocks)
            if (b.kind != Block::Kind::File && pf.Find(b.kind, b.id)) score += 2;
        bool header = fresh.files[k].content.find("#pragma once") != std::string::npos;
        if (header == existing_header) score += 1;
        if (score > best_score) {
            best_score = score;
            best = (int)k;
        }
    }
    return best;
}

MergeReport Merge(Panel& panel, const std::string& existing_source, const codegen::Result& fresh,
                  std::string& merged_source_out) {
    ParsedFile ex;
    Error e;
    if (!ParseGenerated(existing_source, ex, &e)) {
        MergeReport rep;
        rep.diagnostics.push_back(MakeDiag(Diagnostic::Level::Error, panel.name, "",
                                           "existing generated file does not parse: " + e.message +
                                               " (line " + std::to_string(e.line) + ")"));
        merged_source_out = existing_source;
        return rep;
    }
    int idx = FindFreshFile(ex, fresh, panel.name);
    if (idx < 0) {
        MergeReport rep;
        rep.diagnostics.push_back(MakeDiag(Diagnostic::Level::Error, panel.name, "",
                                           "no generated file for panel '" + panel.name + "'"));
        merged_source_out = existing_source;
        return rep;
    }
    return Merge(panel, existing_source, fresh, (size_t)idx, merged_source_out);
}

MergeReport Merge(Panel& panel, const std::string& existing_source, const codegen::Result& fresh,
                  size_t fresh_file_index, std::string& merged_source_out) {
    MergeReport rep;
    merged_source_out = existing_source;
    ParsedFile ex;
    Error e;
    if (!ParseGenerated(existing_source, ex, &e)) {
        rep.diagnostics.push_back(MakeDiag(Diagnostic::Level::Error, panel.name, "",
                                           "existing generated file does not parse: " + e.message +
                                               " (line " + std::to_string(e.line) + ")"));
        return rep;
    }
    if (fresh_file_index >= fresh.files.size()) {
        rep.diagnostics.push_back(MakeDiag(Diagnostic::Level::Error, panel.name, "",
                                           "fresh file index out of range"));
        return rep;
    }
    const codegen::FileOut& file = fresh.files[fresh_file_index];
    std::vector<std::string> carried;
    std::string text = CarryUserRegions(file.content, ex, &carried);
    ParsedFile fr;
    if (!ParseGenerated(text, fr, &e)) {
        rep.diagnostics.push_back(MakeDiag(Diagnostic::Level::Error, panel.name, "",
                                           "generated file '" + file.path +
                                               "' does not parse: " + e.message + " (line " +
                                               std::to_string(e.line) + ")"));
        merged_source_out = file.content;
        return rep;
    }
    for (const Block& eb : ex.blocks) {
        if (eb.kind != Block::Kind::User && eb.kind != Block::Kind::Locked) continue;
        if (IsBlank(eb.body) || fr.Find(eb.kind, eb.id)) continue;
        rep.diagnostics.push_back(MakeDiag(
            Diagnostic::Level::Warning, panel.name, "",
            std::string("user code in ") + KindName(eb.kind) + " block '" + eb.id +
                "' has no counterpart in the generated output and was dropped"));
    }
    Decisions decisions;
    std::map<std::string, std::string> conflict_hashes;
    for (size_t bi = 0; bi < ex.blocks.size(); ++bi) {
        const Block& eb = ex.blocks[bi];
        if (eb.kind != Block::Kind::Begin || detail::HasUserOrLockedAncestor(ex, (int)bi)) continue;
        if (BlockHash(eb) == eb.hash) continue;
        const Block* fb = fr.Find(Block::Kind::Begin, eb.id);
        if (!fb) {
            rep.diagnostics.push_back(MakeDiag(Diagnostic::Level::Warning, panel.name, eb.id,
                                               "edited block '" + eb.id +
                                                   "' no longer exists in the generated output; "
                                                   "the edit was dropped"));
            continue;
        }
        std::vector<Token> ut = Tokenize(eb.own_body), ft = Tokenize(fb->own_body);
        if (TokensEqual(ut, ft)) continue;
        std::string reason;
        BlockDecision d;
        d.existing = &eb;
        if (TryLiteralMerge(panel, eb, *fb, ut, ft, fresh, file.path, reason)) {
            rep.updated_nodes.push_back(eb.id);
            d.decision = Decision::ReplaceOwn;
        } else {
            rep.conflicts.push_back(Conflict{eb.id, reason, eb.body, fb->body});
            rep.diagnostics.push_back(MakeDiag(Diagnostic::Level::Warning, panel.name, eb.id,
                                               "round-trip conflict in block '" + eb.id +
                                                   "': " + reason));
            d.decision = Decision::ReplaceWhole;
            conflict_hashes[eb.id] = eb.hash;
        }
        decisions[eb.id] = d;
    }
    std::vector<std::string> lines = detail::SplitLines(text);
    Renderer r{fr, lines, decisions, {}};
    r.RenderFile();
    merged_source_out = RestoreConflictHashes(RehashBlocks(detail::JoinLines(r.out)), conflict_hashes);
    if (existing_source.compare(0, 3, "\xEF\xBB\xBF") == 0) merged_source_out.insert(0, existing_source, 0, 3);
    return rep;
}

MergeReport MergeFiles(Panel& panel,
                       const std::vector<std::pair<std::string, std::string>>& existing_files,
                       const codegen::Result& fresh,
                       std::vector<std::pair<std::string, std::string>>& merged_files) {
    MergeReport rep;
    for (size_t k = 0; k < fresh.files.size(); ++k) {
        const codegen::FileOut& f = fresh.files[k];
        ParsedFile pf;
        if (!ParseGenerated(f.content, pf, nullptr) || pf.panel != panel.name) continue;
        const std::string* existing = nullptr;
        for (const auto& ef : existing_files)
            if (ef.first == f.path || FileName(ef.first) == FileName(f.path)) existing = &ef.second;
        std::string merged = f.content;
        if (existing) Append(rep, Merge(panel, *existing, fresh, k, merged));
        merged_files.emplace_back(f.path, merged);
    }
    return rep;
}

}  // namespace roundtrip
}  // namespace imforge
