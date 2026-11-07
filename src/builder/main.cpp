#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "app.h"
#include "automation.h"
#include "export.h"
#include "imforge/imforge_version.h"
#include "platform/platform.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void PrintUsage() {
    std::printf("ImForge builder %s\n"
                "usage: imforge_builder [options] [project_dir]\n"
                "  project_dir      folder containing imforge.json\n"
                "  --fonts <dir>    override the fonts directory (also IMFORGE_FONTS_DIR)\n"
                "  --log <file>     mirror the Log panel to a file (also IMFORGE_LOG_FILE)\n"
                "  --self-check-export <project_dir> <out_dir>\n"
                "                   headless export / hand-edit / import / re-export round trip; writes\n"
                "                   <out_dir>/imforge_selfcheck.txt and exits 0 on success\n"
                "  --script <file>  run an automation script (one command per line, see tools/demo/live_demo.txt)\n"
                "                   and exit with its exit code\n"
                "  --no-wizard      do not show the wizard when no project is open\n"
                "  --version        print the version and exit\n"
                "  -h, --help       show this help\n",
                IMFORGE_VERSION);
}

}  // namespace

int main(int argc, char** argv) {
    std::string project_dir, script_path;
    bool no_wizard = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            PrintUsage();
            return 0;
        }
        if (std::strcmp(a, "--version") == 0) {
            std::printf("%s\n", IMFORGE_VERSION);
            return 0;
        }
        if (std::strcmp(a, "--no-wizard") == 0) {
            no_wizard = true;
            continue;
        }
        if (std::strcmp(a, "--self-check-export") == 0 && i + 2 < argc) {
            std::string report;
            bool ok = imforge::builder::ExportSelfCheck(argv[i + 1], argv[i + 2], report);
            std::printf("%s", report.c_str());
            return ok ? 0 : 3;
        }
        if (std::strcmp(a, "--script") == 0 && i + 1 < argc) {
            script_path = argv[++i];
            continue;
        }
        if ((std::strcmp(a, "--fonts") == 0 || std::strcmp(a, "--log") == 0) && i + 1 < argc) {
            const char* var = std::strcmp(a, "--fonts") == 0 ? "IMFORGE_FONTS_DIR" : "IMFORGE_LOG_FILE";
#ifdef _WIN32
            _putenv_s(var, argv[++i]);
#else
            setenv(var, argv[++i], 1);
#endif
            continue;
        }
        if (a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a);
            PrintUsage();
            return 2;
        }
        project_dir = a;
    }

    imforge::builder::Automation automation;
    if (!script_path.empty()) {
        imforge::builder::AutomationAttachConsole();
        std::string err;
        if (!automation.Load(script_path, &err)) {
            std::fprintf(stderr, "ImForge: %s\n", err.c_str());
            return 2;
        }
    }

    std::unique_ptr<imforge::builder::Platform> platform = imforge::builder::CreatePlatform();
    if (!platform) {
        std::fprintf(stderr, "ImForge: no platform backend compiled in\n");
        return 1;
    }
    imforge::builder::App app(*platform);
    if (no_wizard) app.Ctx().prefs.show_wizard_on_start = false;
    if (!app.Init(project_dir.empty() ? nullptr : project_dir.c_str())) return 1;
    if (no_wizard) app.Ctx().prefs.show_wizard_on_start = false;

    bool quit = false;
    while (!quit) {
        bool want_quit = false;
        platform->PollEvents(want_quit);
        app.Frame(want_quit);
        quit = want_quit;
        if (script_path.empty()) continue;
        automation.Step(app.Ctx(), *platform);
        if (automation.Done()) break;
    }
    app.Shutdown();
    if (!script_path.empty()) return automation.Done() ? automation.ExitCode() : 4;
    return 0;
}

#if defined(_WIN32) && defined(IMFORGE_BUILDER_WINMAIN)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(__argc, __argv); }
#endif
