#pragma once

#include <string>

namespace imforge::builder {

bool OpenExternal(const std::string& url_or_path);
bool NativePickFolder(std::string& out, const char* title, const std::string& initial_dir = std::string());
bool NativePickFile(std::string& out, const char* title, const char* extension, const std::string& initial_dir = std::string());
bool NativeDialogsAvailable();
std::string HomeDir();

}  // namespace imforge::builder
