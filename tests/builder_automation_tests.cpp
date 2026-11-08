#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "imforge/imforge_transport.h"
#include "codegen.h"
#include "roundtrip.h"

#include "../src/builder/prefs.cpp"
#include "../src/builder/project_io.cpp"
#include "../src/builder/live_server.cpp"
#include "../src/builder/imblib.cpp"
#include "../src/builder/editor_context.cpp"
#include "../src/builder/editor_commands.cpp"
#include "../src/builder/ui/tree_ops.cpp"
#include "../src/builder/panels/components_ops.cpp"
#include "../src/builder/panels/components_actions.cpp"
#include "../src/builder/bundled_packs.cpp"
#define Fail ExportFail
#include "../src/builder/export.cpp"
#undef Fail
#define BaseName SnippetBaseName
#include "../src/builder/ui/client_snippet.cpp"
#undef BaseName
#include "../src/builder/live_bridge.cpp"
#include "../src/builder/automation.cpp"

namespace imforge::builder {
std::string ExeDir() { return std::filesystem::current_path().generic_string(); }
void RequestOverwriteConfirm(EditorContext&, const std::vector<std::string>&) {}
bool ShowLibraryTab(EditorContext&, const std::string&) { return false; }
void ScrollLibrary(float) {}
}  // namespace imforge::builder

using namespace imforge;
using namespace imforge::builder;

namespace {

namespace fs = std::filesystem;

std::string TempPath(const std::string& tag) {
#ifdef IMFORGE_BINARY_DIR
    fs::path p = fs::path(IMFORGE_BINARY_DIR) / "test_tmp" / ("builder_automation_" + tag);
#else
    fs::path p = fs::temp_directory_path() / ("imforge_builder_automation_" + tag);
#endif
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p.generic_string();
}

void SetEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// no window: the first capture hands back a 2x2 white frame, later ones never finish, so a
// shot that should have been skipped can't land anywhere
struct FakePlatform : Platform {
    int captures = 0;
    bool Init(const char*, int, int) override { return true; }
    bool PollEvents(bool&) override { return true; }
    void BeginFrame() override {}
    void EndFrame(const ImVec4&) override {}
    void Shutdown() override {}
    float DpiScale() override { return 1.0f; }
    ImTextureID CreateTextureRGBA(const unsigned char*, int, int) override { return ImTextureID(); }
    void DestroyTexture(ImTextureID) override {}
    void* GetProcAddress(const char*) override { return nullptr; }
    const char* Name() const override { return "fake"; }
    bool RequestCapture() override {
        ++captures;
        return true;
    }
    bool FetchCapture(int& w, int& h, std::vector<unsigned char>& px) override {
        if (captures > 1) return false;
        w = h = 2;
        px.assign(16, 255);
        return true;
    }
};

}  // namespace

TEST_CASE("screenshot with an unset ${VAR} in the path is skipped") {
#ifdef _WIN32
    SetEnv("APPDATA", TempPath("config"));
#else
    SetEnv("XDG_CONFIG_HOME", TempPath("config"));
#endif
    std::string dir = TempPath("shots");
    UnsetEnv("IMFORGE_TEST_UNSET_DIR");
    SetEnv("IMFORGE_TEST_SHOT_DIR", dir);
    std::string script = dir + "/script.txt";
    {
        std::ofstream out(script, std::ios::binary);
        out << "# both lines look the same, only one variable is set\n"
               "screenshot ${IMFORGE_TEST_SHOT_DIR}/taken.png\n"
               "screenshot ${IMFORGE_TEST_UNSET_DIR}/imforge_skipped.png\n"
               "quit 0\n";
    }
    EditorContext ctx;
    Automation a;
    std::string err;
    REQUIRE_MESSAGE(a.Load(script, &err), err);
    FakePlatform platform;
    for (int i = 0; i < 200 && !a.Done(); ++i) a.Step(ctx, platform);
    CHECK(a.Done());
    CHECK(a.ExitCode() == 0);
    CHECK(platform.captures == 1);
    CHECK(fs::is_regular_file(dir + "/taken.png"));
    CHECK_FALSE(fs::exists(fs::absolute("/imforge_skipped.png")));
}
