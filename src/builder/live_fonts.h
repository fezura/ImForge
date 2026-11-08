#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imforge/imforge_core.h"
#include "imforge/imforge_protocol.h"

namespace imforge::builder {

// project fonts for the live link: only files under the allowed roots (project dir, builder fonts dir)
// are read, each one checked and hashed once per change on disk
struct LiveFontFile {
    std::string path, hash;
    std::vector<unsigned char> data;
    uint64_t stamp = 0;
};

struct LiveFontPlan {
    std::vector<FontSpecMsg> specs;
    std::vector<const LiveFontFile*> files;
    std::vector<std::string> warnings;
    // icon name table for {icon:...} text, only when a font merges icons
    std::vector<std::pair<std::string, unsigned>> icons;
};

using LiveFontResolver = std::function<std::string(const std::string&)>;

bool FontPathAllowed(const std::string& path, const std::vector<std::string>& roots);
bool IconCodepoints(const std::string& merge_icons, unsigned& lo, unsigned& hi);

class LiveFontCache {
public:
    // resolve_file maps a FontSpec::file to a path, resolve_icons a mergeIcons name to the icon font path
    LiveFontPlan Plan(const Project& project, const std::vector<std::string>& roots, const LiveFontResolver& resolve_file,
                      const LiveFontResolver& resolve_icons);
    const LiveFontFile* Load(const std::string& path, const std::vector<std::string>& roots, std::string& err);
    size_t CachedFiles() const { return files_.size(); }

private:
    std::map<std::string, LiveFontFile> files_;
};

inline std::vector<Json> FontDataMessages(const LiveFontFile& f) {
    std::vector<Json> out;
    for (size_t off = 0; off < f.data.size(); off += kProtocolFontChunk) {
        size_t n = f.data.size() - off < kProtocolFontChunk ? f.data.size() - off : kProtocolFontChunk;
        out.push_back(MsgFontData(f.hash, f.data.size(), off, f.data.data() + off, n));
    }
    return out;
}

}  // namespace imforge::builder
