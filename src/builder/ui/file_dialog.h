#pragma once

#include <string>
#include <vector>

namespace imforge {
namespace builder {

struct FileDialogOptions {
    std::string title = "Open file";
    std::vector<std::string> extensions;
    bool pick_directory = false;
    bool save = false;
    std::string default_name;
};

void OpenFileDialog(const char* key, const std::string& start_dir, const FileDialogOptions& options);
bool DrawFileDialog(const char* key, std::string& out_path);
bool FileDialogIsOpen(const char* key);
const std::vector<std::string>& FileDialogRecentDirs();
void FileDialogAddRecentDir(const std::string& dir);

}  // namespace builder
}  // namespace imforge
