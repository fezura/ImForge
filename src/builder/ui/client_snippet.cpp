#include "client_snippet.h"

#include <cctype>
#include <vector>

namespace imforge::builder {

namespace {

std::string Identifier(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) out.push_back((char)std::tolower(c));
        else if (!out.empty() && out.back() != '_') out.push_back('_');
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "data";
    if (std::isdigit((unsigned char)out[0])) out.insert(out.begin(), '_');
    return out;
}

std::string BaseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string StripExt(const std::string& name) {
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

struct Root {
    std::string param, type, var;
};

std::vector<Root> BindingRoots(const Project& p, std::string& panel_name) {
    std::vector<Root> roots;
    if (!p.panels.empty()) {
        const Panel& panel = p.panels.front();
        panel_name = panel.name;
        for (const Param& prm : panel.params) roots.push_back({prm.name, prm.type, "g_" + Identifier(prm.name)});
    } else {
        panel_name = p.panel_files.empty() ? "Main" : StripExt(StripExt(BaseName(p.panel_files.front())));
    }
    if (roots.empty())
        for (const std::string& t : p.binding_types) roots.push_back({Identifier(t), t, "g_" + Identifier(t)});
    return roots;
}

std::string ImForgeDir() {
#ifdef IMFORGE_SOURCE_DIR
    return IMFORGE_SOURCE_DIR;
#else
    return "path/to/ImForge";
#endif
}

}  // namespace

std::string LiveUrl(const Project& project) {
    const LiveSettings& l = project.live;
    if (l.transport == "pipe") return "pipe://" + l.pipe_name;
    if (l.transport == "shm") return "shm://" + l.shm_name;
    return "tcp://127.0.0.1:" + std::to_string(l.tcp_port);
}

std::string ShaderFxHeader(const std::string& renderer) {
    if (renderer == "dx9" || renderer == "dx11" || renderer == "dx12" || renderer == "gl3" || renderer == "vulkan")
        return "imforge/fx/imb_fx_" + renderer + ".h";
    return std::string();
}

std::string MakeClientSnippet(const Project& p) {
    std::string panel_name;
    std::vector<Root> roots = BindingRoots(p, panel_name);
    const std::string out_dir = p.codegen.output_dir.empty() ? "generated" : p.codegen.output_dir;
    std::string s;
    s += "// ImForge live attach for \"" + (p.name.empty() ? std::string("MyProject") : p.name) + "\" (development builds only).\n";
    s += "// Without IMFORGE_LIVE every imforge::live call compiles to an empty inline no-op.\n";
    if (!p.live.enabled) s += "// NOTE: live attach is currently disabled in Project Settings > Live; the builder will not listen.\n";
    s += "#define IMFORGE_LIVE\n";
    s += "#include \"imforge_client.h\"               // or the amalgamated dist/imforge_client_single.h\n";
    if (!roots.empty()) {
        s += "#include \"" + out_dir + "/imforge_reflect.h\"   // IMFORGE_REFLECT(...) for the bound types\n";
    } else {
        s += "// IMFORGE_REFLECT(MyStruct, field_a, field_b);   // describe each struct you bind\n";
    }
    s += "\n";
    if (!roots.empty()) {
        s += "// bound data, one root per panel parameter\n";
        for (const Root& r : roots) s += r.type + " " + r.var + ";\n";
        s += "\n";
    }
    s += "void InitUi() {\n";
    s += "    // once, after ImGui::CreateContext() and your ImGui_Impl*_Init()\n";
    s += "    imforge::live::Connect(\"" + LiveUrl(p) + "\");\n";
    s += "    #include \"" + out_dir + "/imforge_dev.h\"   // optional: registers event handlers and locked blocks\n";
    s += "}\n\n";
    s += "void DrawUi() {\n";
    s += "    // every frame, after ImGui::NewFrame()\n";
    s += "    imforge::live::NewFrame();\n";
    s += "    imforge::live::Draw(\"" + panel_name + "\", {";
    if (roots.empty()) {
        s += "});\n";
    } else {
        for (size_t i = 0; i < roots.size(); ++i) {
            if (i) s += ",";
            s += "\n        imforge::live::Bind(\"" + roots[i].param + "\", " + roots[i].var + ")";
        }
        s += " });\n";
    }
    s += "}\n\n";
    std::string ns = p.codegen.nmspace.empty() ? std::string() : p.codegen.nmspace + "::";
    s += "// Release: drop the #define (or guard it with #ifdef _DEBUG) and call the generated\n";
    s += "// " + ns + "Draw" + panel_name + "(...) from \"" + out_dir + "/\" instead of imforge::live::Draw.\n";
    return s;
}

std::string MakeCMakeSnippet(const Project& p) {
    const std::string out_dir = p.codegen.output_dir.empty() ? "generated" : p.codegen.output_dir;
    const std::string proj_dir = p.dir.empty() ? "path/to/" + (p.name.empty() ? std::string("MyProject") : p.name) : p.dir;
    std::string s;
    s += "# ImForge: add to the CMakeLists.txt of your application (target name: app)\n";
    s += "set(IMFORGE_DIR \"" + ImForgeDir() + "\")\n";
    s += "target_include_directories(app PRIVATE\n";
    s += "    \"${IMFORGE_DIR}/include\"              # imforge_client.h, imb_runtime.h, fx/ (header-only)\n";
    for (const std::string& lib : p.libraries) {
        std::string name = StripExt(BaseName(lib));
        s += "    \"${IMFORGE_DIR}/packs/" + name + "\"      # component library \"" + name + "\"\n";
    }
    s += "    \"" + proj_dir + "/" + out_dir + "\")   # generated UI code\n";
    s += "target_compile_features(app PRIVATE cxx_std_" + std::to_string(p.codegen.cpp_standard) + ")\n";
    s += "target_compile_definitions(app PRIVATE\n";
    s += "    $<$<CONFIG:Debug>:IMFORGE_LIVE>        # live attach in debug builds only\n";
    s += "    IMGUI_DEFINE_MATH_OPERATORS)\n";
    if (p.Feature("shaderFx", false)) {
        std::string hdr = ShaderFxHeader(p.renderer);
        s += "\n# Shader fx for " + p.renderer + ": #include \"" + hdr + "\" in one .cpp, call imb::fx::Init(...) once\n";
        s += "# and imb::fx::PrepareFrame(...) each frame before ImGui_Impl*_RenderDrawData.\n";
        if (p.renderer == "dx9") s += "target_link_libraries(app PRIVATE d3d9 d3dcompiler)\n";
        else if (p.renderer == "dx11") s += "target_link_libraries(app PRIVATE d3d11 dxgi d3dcompiler)\n";
        else if (p.renderer == "dx12") s += "target_link_libraries(app PRIVATE d3d12 dxgi d3dcompiler)\n";
        else if (p.renderer == "vulkan") s += "find_package(Vulkan REQUIRED)\ntarget_link_libraries(app PRIVATE Vulkan::Vulkan)\n";
        else s += "# gl3 uses the same GL loader as imgui_impl_opengl3; nothing extra to link.\n";
    }
    if (p.Feature("iconFonts", false))
        s += "\n# Icon fonts: ship fa-solid-900.ttf next to the executable and merge it in LoadFonts (see generated code).\n";
    return s;
}

}  // namespace imforge::builder
