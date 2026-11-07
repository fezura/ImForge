#pragma once

#include <deque>
#include <string>
#include <vector>

#include "imforge/imforge_core.h"
#include "imforge/imforge_expr.h"
#include "type_database.h"

namespace imforge::builder {

// Resolves panel params (via the TypeDatabase), `state.<var>` paths and, while a component definition
// is open, its params: `binding<T>` resolves to T, scalars to their type, `enum:<A,B,C>` to a made-up enum.
struct EditorTypeEnv final : expr::TypeEnv {
    const binding::TypeDatabase* db = nullptr;
    std::vector<Param> params;
    std::vector<StateVar> state;
    std::vector<Param> component_params;
    std::string last_error;

    EditorTypeEnv() = default;
    EditorTypeEnv(const binding::TypeDatabase* database, const Panel& panel);
    EditorTypeEnv(const binding::TypeDatabase* database, const Component& component);
    void SetPanel(const Panel& panel);
    void SetComponent(const Component& component);
    void Clear();

    bool TypeOfPath(const std::string& path, CType& out) override;
    bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) override;

    bool TypeOfPathErr(const std::string& path, CType& out, std::string* err);
    std::vector<std::string> Complete(const std::string& prefix) const;
    std::vector<std::string> Roots() const;
    const EnumInfo* FindEnum(const std::string& enum_name) const;
    const Param* ComponentParam(const std::string& name) const;

    static bool StateCType(const std::string& type_name, CType& out);
    bool ComponentParamCType(const Param& p, CType& out);

private:
    std::deque<EnumInfo> enums_;

    bool StatePath(const std::string& path, CType& out, std::string* err) const;
    bool ComponentParamPath(const std::string& path, CType& out, std::string* err);
    bool Walk(const CType& root, const std::string& rest, CType& out, std::string* err) const;
    std::vector<std::string> MembersOf(const CType& t) const;
    void CompleteMembers(const std::string& prefix, std::vector<std::string>& out) const;
};

}  // namespace imforge::builder
