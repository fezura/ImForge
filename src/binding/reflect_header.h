#pragma once

#include <string>
#include <vector>

#include "type_database.h"

namespace imforge {
namespace binding {

struct ReflectHeaderOptions {
    std::vector<std::string> types;
    std::string value_include = "imforge_value.h";
    bool include_type_headers = true;
};

// empty roots means everything. structs come out in dependency order, nested types first
bool ReflectOrder(const TypeDatabase& db, const std::vector<std::string>& roots,
                  std::vector<std::string>& enums_out, std::vector<std::string>& structs_out,
                  std::string* err);

bool GenerateReflectHeader(const TypeDatabase& db, const ReflectHeaderOptions& opt, std::string& out,
                           std::string* err, std::vector<std::string>* warnings = nullptr);

}  // namespace binding
}  // namespace imforge
