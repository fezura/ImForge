#pragma once

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge {
namespace codegen {

// types.json loader. Owns every StructInfo/EnumInfo/CType it hands out.
struct TypesDb {
    TypeRegistry registry;
    std::deque<CType> elems;
    std::vector<std::pair<std::string, std::string>> headers;
    bool loaded = false;

    bool Load(const Json& types_json, Error* err = nullptr);
    bool LoadFile(const std::string& path, Error* err = nullptr);

    const StructInfo* FindStruct(const std::string& name) const;
    const EnumInfo* FindEnum(const std::string& name) const;
    std::string HeaderOf(const std::string& struct_name) const;
    // Walks `.field` / `[index]` segments from a root type. `rest` starts after the root name
    // (may be empty). A trailing `.size` on a vector/array yields I32 and sets *is_size.
    bool Resolve(const CType& root, const std::string& rest, CType& out, bool* is_size = nullptr,
                 CType* container = nullptr) const;
    // Dependency-ordered struct names (fields' struct types first) for the given roots.
    std::vector<std::string> OrderedStructs(const std::vector<std::string>& roots) const;
    std::vector<std::string> EnumsUsedBy(const std::vector<std::string>& structs) const;

    CType ParseType(const std::string& spelling, const std::string& ctype_name, const Json& field);
    static CType ScalarType(const std::string& state_type);
    static CType FromSpelling(const std::string& spelling);
};

}  // namespace codegen
}  // namespace imforge
