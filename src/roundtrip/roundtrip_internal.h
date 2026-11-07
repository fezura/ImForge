#pragma once

#include "roundtrip.h"

#include <string>
#include <vector>

namespace imforge {
namespace roundtrip {
namespace detail {

struct Marker {
    enum class Kind { None, File, Begin, End, User, Locked };
    Kind kind = Kind::None;
    std::string id, hash, panel;
    int schema = 0;
};

std::vector<std::string> SplitLines(const std::string& text);
std::string JoinLines(const std::vector<std::string>& lines);
std::string Trim(const std::string& s);
bool StartsWith(const std::string& s, const char* prefix);

// ParseMarker: false with empty err means "not a marker", false with err means malformed
bool IsMarkerLine(const std::string& line);
bool NextCommentState(const std::string& line, bool in_comment);
bool ParseMarker(const std::string& line, Marker& out, std::string* err);
std::string SetMarkerHash(const std::string& line, const std::string& hash);
std::string SetMarkerKind(const std::string& line, const char* kind);

bool ChildPlaceholder(const std::string& line, std::string* id_out);
std::string ChildPlaceholderLine(const std::string& like_line, const std::string& id);

// line_begin/line_end are the 1-based marker lines, they stay
void ReplaceBody(std::vector<std::string>& lines, int line_begin, int line_end,
                 const std::vector<std::string>& body);

bool HasUserOrLockedAncestor(const ParsedFile& file, int block_index);

}  // namespace detail
}  // namespace roundtrip
}  // namespace imforge
