#pragma once

#include <string>

#include "imforge/imforge_core.h"

namespace imforge::builder {

std::string LiveUrl(const Project& project);
std::string ShaderFxHeader(const std::string& renderer);
std::string MakeClientSnippet(const Project& project);
std::string MakeCMakeSnippet(const Project& project);

}  // namespace imforge::builder
