#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace imforge {
namespace builder {

struct IconEntry {
    std::string name;
    std::string utf8;
    unsigned codepoint = 0;
};

const IconEntry* IconTable(size_t& count);
const char* IconUtf8(const char* name);
const IconEntry* FindIcon(const char* name);

bool LoadIconTableFromHeader(const std::string& header_path);
void AddIconHeaderSearchDir(const std::string& dir);
const std::string& IconTableSource();
void IconCodepointRange(unsigned& min_out, unsigned& max_out);

std::string Utf8FromCodepoint(unsigned cp);
unsigned CodepointFromUtf8(const char* s);

}  // namespace builder
}  // namespace imforge
