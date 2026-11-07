#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imforge/imforge_core.h"
#include "imforge/imforge_json.h"
#include "imforge/imforge_value.h"

namespace imforge {
namespace binding {

struct FieldDesc {
    std::string name, type, ctype, elem;
    int count = 0;
    long long offset = -1;
};

struct StructDesc {
    std::string name, header;
    long long size = 0;
    std::vector<FieldDesc> fields;
};

struct EnumDesc {
    std::string name, header, underlying;
    bool scoped = false;
    std::vector<std::pair<std::string, long long>> values;
};

std::string NormalizeTypeName(const std::string& cpp_type);
bool CKindFromName(const std::string& name, CKind& out);
std::string SpellCType(const CType& t);
FieldDesc DescribeCType(const CType& t);

// Later loads replace same-named types. Struct()/Enum() pointers stay valid until Clear().
class TypeDatabase {
public:
    bool Load(const Json& types_json, Error* err = nullptr);
    Json Save() const;
    void Clear();
    bool Empty() const { return struct_src_.empty() && enum_src_.empty(); }

    void AddStruct(const StructDesc& d);
    void AddEnum(const EnumDesc& d);
    bool FromRegistry(const TypeRegistry& reg);

    const StructInfo* Struct(const std::string& name) const;
    const EnumInfo* Enum(const std::string& name) const;
    const StructDesc* StructSource(const std::string& name) const;
    const EnumDesc* EnumSource(const std::string& name) const;
    std::vector<std::string> StructNames() const;
    std::vector<std::string> EnumNames() const;

    bool ResolveTypeName(const std::string& cpp_type, CType& out) const;
    bool TypeOfPath(const std::vector<Param>& params, const std::string& path, CType& out,
                    std::string* err = nullptr) const;
    bool WalkPath(const CType& root, const std::string& rest, CType& out, std::string* err) const;
    std::vector<std::string> Complete(const std::vector<Param>& params,
                                      const std::string& prefix) const;
    std::vector<std::string> Members(const CType& type) const;

private:
    std::map<std::string, StructDesc> struct_src_;
    std::map<std::string, EnumDesc> enum_src_;
    std::map<std::string, StructInfo> structs_;
    std::map<std::string, EnumInfo> enums_;
    mutable std::map<std::string, CType> interned_;

    void Rebuild();
    const CType* Intern(const std::string& spelling) const;
    CType Compute(const std::string& spelling) const;
    CType FieldType(const FieldDesc& f) const;
    bool Member(const CType& cur, const std::string& name, CType& out, std::string* err) const;
    bool Element(const CType& cur, const std::string& index, CType& out, std::string* err) const;
};

}  // namespace binding
}  // namespace imforge
