#pragma once

#include "imforge/imforge_core.h"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace imforge {
namespace codegen {
struct Result;
struct LiteralRef;
}  // namespace codegen

namespace roundtrip {

// line_begin/line_end are the 1-based marker lines. own_body is body with each directly nested
// begin/locked block collapsed to one `@child:<id>` line, that's what h= hashes. Nested user
// slots stay in own_body. parent is -1 at file level.
struct Block {
    enum class Kind { Begin, User, Locked, File };
    Kind kind = Kind::Begin;
    std::string id;
    std::string hash;
    std::string body;
    std::string own_body;
    int line_begin = 0, line_end = 0;
    int parent = -1;
    std::vector<std::string> children;
};

struct ParsedFile {
    std::string panel;
    int schema = 0;
    std::vector<Block> blocks;
    std::string full_text;
    const Block* Find(Block::Kind kind, const std::string& id) const;

  private:
    mutable std::map<std::pair<int, std::string>, size_t> index_;
    mutable size_t indexed_ = 0;
};

// ids only have to be unique per kind: a locked function and its call-site begin block share one
bool ParseGenerated(const std::string& text, ParsedFile& out, Error* err);

// must match codegen exactly: leading whitespace stripped, nested blocks as `@child:<id>`,
// "\n" line ends, FNV-1a 32, 8 lowercase hex digits
std::string NormalizeForHash(const std::string& own_body);
std::string Fnv1a32Hex(const std::string& normalized_body);
std::string BlockHash(const Block& block);

// Same stream as codegen::Tokenize, so LiteralRef::block_token_index indexes
// Tokenize(own_body) directly. Comments (and so marker lines) are skipped, a preprocessor line or
// `@child:<id>` is one Punct token, and a minus right before a digit belongs to the number.
struct Token {
    enum Kind { Ident, Number, String, Char, Punct } kind;
    std::string text;
    int line;
    size_t offset;
};
std::vector<Token> Tokenize(const std::string& cpp);

// Copies user/locked bodies from `existing` into `fresh` by id. If the fresh locked block is a
// function wrapper but the old body is inline code (locked by ApplyResolution), the old lines get
// re-indented into the wrapper.
std::string CarryUserRegions(const std::string& fresh, const ParsedFile& existing,
                             std::vector<std::string>* carried_ids);

std::string RehashBlocks(const std::string& text);

// value is already in the prop's JSON shape. Flags are a delta so flags the generator adds by
// itself (MenuBar etc) don't get duplicated into the project.
struct LiteralEdit {
    std::string prop;
    PropType type = PropType::String;
    Json value;
    std::vector<std::string> flags_added, flags_removed;
};

// Walks user and fresh tokens in lockstep. At each ref the whole literal is eaten on both sides
// (string run, signed number, `|` chain, ImVec2/ImVec4/IM_COL32 group) and differences become
// edits, everything else has to match token for token. node may be null, then prop types are
// guessed from the fresh token.
bool ExtractLiteralEdits(const Node* node, const std::vector<Token>& user_tokens,
                         const std::vector<Token>& fresh_tokens,
                         const std::vector<codegen::LiteralRef>& refs,
                         std::vector<LiteralEdit>& edits_out, std::string* reason);

// all or nothing: if any target prop is bound to an expression/param the node is left alone
bool ApplyLiteralEdits(Node& node, const std::vector<LiteralEdit>& edits, std::string* reason);

struct Conflict {
    std::string node_id;
    std::string reason;
    std::string user_code;
    std::string generated_code;
};
struct MergeReport {
    std::vector<std::string> updated_nodes;
    std::vector<Conflict> conflicts;
    std::vector<Diagnostic> diagnostics;
};

// Merges one generated file. The first overload picks the fresh file for this panel that shares
// the most block ids. Edited blocks with literal-only changes go into `panel`, anything else is a
// Conflict and the user's body stays as is. All other hashes get recomputed, so regenerate
// before merging again or those edits count as untouched and the generator wins. Conflict
// blocks keep their old h= so they keep showing up until ApplyResolution. On error the output
// is just existing_source.
MergeReport Merge(Panel& panel, const std::string& existing_source, const codegen::Result& fresh,
                  std::string& merged_source_out);
MergeReport Merge(Panel& panel, const std::string& existing_source, const codegen::Result& fresh,
                  size_t fresh_file_index, std::string& merged_source_out);

// existing_files are (path, content) from disk, matched by full path or file name. Fresh files
// with no match pass through verbatim.
MergeReport MergeFiles(Panel& panel,
                       const std::vector<std::pair<std::string, std::string>>& existing_files,
                       const codegen::Result& fresh,
                       std::vector<std::pair<std::string, std::string>>& merged_files);

int FindFreshFile(const ParsedFile& existing, const codegen::Result& fresh,
                  const std::string& panel_name);

// keep_user_code turns the block into `@imb:locked` with the user's body. The caller also has to
// set node->locked so the next generate emits a locked function for CarryUserRegions to fill.
// Only the resolved block is rehashed so other conflicts in the file stay flagged.
std::string ApplyResolution(const std::string& merged_source, const Conflict& conflict,
                            bool keep_user_code);

}  // namespace roundtrip
}  // namespace imforge
