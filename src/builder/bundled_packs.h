#pragma once

#include <set>
#include <string>
#include <vector>

#include "imforge/imforge_core.h"

namespace imforge {
struct Interpreter;
}

namespace imforge::builder {

struct EditorContext;

// a widget pack shipped with the builder (packs/<name>/manifest.json)
struct BundledPack {
    std::string name, title, dir, version, description;
    std::vector<std::string> headers;
    std::vector<std::string> widgets;
    std::vector<std::string> includes;
    bool has_adapters = false;
};

std::string FindBundledPacksDir();
std::vector<BundledPack> ScanBundledPacks(const std::string& packs_dir, WidgetCatalog* catalog,
                                          std::vector<std::string>* warnings);
const std::vector<BundledPack>& BundledPacks();
const BundledPack* BundledPackForWidget(const std::string& type);
const BundledPack* BundledPackByName(const std::string& name);
bool HasBundledAdapters(const std::string& pack);

// registers the adapters of every pack compiled into this build, returns how many packs
int RegisterBundledAdapters(Interpreter& interp);

// startup: locate packs, add their widget manifests to ctx.catalog, register adapters on ctx.interp
void InitBundledPacks(EditorContext& ctx);

std::set<std::string> UsedWidgetTypes(const Project& project);

}  // namespace imforge::builder
