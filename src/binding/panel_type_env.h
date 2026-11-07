#pragma once

#include <string>
#include <vector>

#include "imforge/imforge_core.h"
#include "imforge/imforge_expr.h"
#include "type_database.h"

namespace imforge {
namespace binding {

// params resolve through the TypeDatabase, `state.<var>` through the panel's state decls
struct PanelTypeEnv : expr::TypeEnv {
    PanelTypeEnv(const TypeDatabase& db, std::vector<Param> params, std::vector<StateVar> state);

    bool TypeOfPath(const std::string& path, CType& out) override;
    bool EnumValue(const std::string& enum_name, const std::string& member, long long& out) override;
    std::vector<std::string> Complete(const std::string& prefix) const;

    static bool StateType(const std::string& type_name, CType& out);
    const EnumInfo* FindEnum(const std::string& enum_name) const;

    std::string last_error;

private:
    const TypeDatabase& db_;
    std::vector<Param> params_;
    std::vector<StateVar> state_;

    bool StatePath(const std::string& path, CType& out, std::string* err) const;
};

}  // namespace binding
}  // namespace imforge
