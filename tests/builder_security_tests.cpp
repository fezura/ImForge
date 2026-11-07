#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../src/builder/project_io.cpp"
#include "../src/builder/panels/panels_state.h"

namespace imforge::builder {
std::string ExeDir() { return std::filesystem::current_path().generic_string(); }
}  // namespace imforge::builder

using namespace imforge;
using namespace imforge::builder;

namespace {

namespace fs = std::filesystem;

struct TempDir {
    std::string path;
    explicit TempDir(const char* tag) {
        static int counter = 0;
        fs::path p = fs::temp_directory_path() / ("imforge_builder_sec_" + std::string(tag) + "_" + std::to_string(++counter));
        std::error_code ec;
        fs::remove_all(p, ec);
        fs::create_directories(p, ec);
        path = p.generic_string();
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string Sub(const char* rel) const { return path + "/" + rel; }
};

void Spit(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << text;
}

bool Exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

const char* kPanelJson = R"({"schemaVersion":1,"kind":"panel","name":"Main","root":{"id":"window","type":"Window"}})";

std::string ProjectJsonWith(const std::string& panels, const std::string& output_dir = "generated") {
    return R"({"schemaVersion":1,"kind":"project","name":"Evil","panels":[)" + panels +
           R"(],"codegen":{"outputDir":")" + output_dir + R"("}})";
}

}  // namespace

TEST_CASE("IsSafeRelativePath rejects traversal, absolute, drive and backslash paths") {
    CHECK(IsSafeRelativePath("panels/Main.imb.json"));
    CHECK(IsSafeRelativePath("panels/Settings Page.imb.json"));
    CHECK(IsSafeRelativePath("a..b/c.json"));
    CHECK_FALSE(IsSafeRelativePath(""));
    CHECK_FALSE(IsSafeRelativePath("../x.json"));
    CHECK_FALSE(IsSafeRelativePath("a/../../x.json"));
    CHECK_FALSE(IsSafeRelativePath("a/.."));
    CHECK_FALSE(IsSafeRelativePath("/etc/passwd"));
    CHECK_FALSE(IsSafeRelativePath("C:/Windows/x.json"));
    CHECK_FALSE(IsSafeRelativePath("C:x.json"));
    CHECK_FALSE(IsSafeRelativePath("a\\b.json"));
    CHECK_FALSE(IsSafeRelativePath("a//b.json"));
    CHECK_FALSE(IsSafeRelativePath("a/b/"));
    CHECK_FALSE(IsSafeRelativePath(std::string("a\0b", 3)));
    CHECK_FALSE(IsSafeRelativePath("a\nb"));
    CHECK_FALSE(IsSafeRelativePath("file.txt:stream"));
}

TEST_CASE("names derived from user JSON become inert file stems") {
    CHECK(SanitizeFileStem("../../evil") == ".._.._evil");
    CHECK(SanitizeFileStem("..") == "_..");
    CHECK(SanitizeFileStem("") == "_");
    CHECK(SanitizeFileStem("a/b\\c:d") == "a_b_c_d");
    CHECK(DefaultPanelFile("../../evil") == "panels/.._.._evil.imb.json");
    CHECK(IsSafeRelativePath(DefaultPanelFile("../../evil")));
    CHECK(IsSafeRelativePath(DefaultComponentFile("/abs/path")));
    CHECK(IsSafeRelativePath(DefaultThemeFile("C:\\x")));
    CHECK(DefaultPanelFile("Main") == "panels/Main.imb.json");
}

TEST_CASE("SafeJoin stays inside the base directory") {
    TempDir tmp("safejoin");
    CHECK_FALSE(SafeJoin(tmp.path, "a/b.json").empty());
    CHECK(SafeJoin(tmp.path, "../x.json").empty());
    CHECK(SafeJoin(tmp.path, "/tmp/x.json").empty());
    CHECK(SafeJoin(tmp.path, "a/../../x.json").empty());
    CHECK(PathIsInside(tmp.path, tmp.path + "/a/b"));
    CHECK_FALSE(PathIsInside(tmp.path, tmp.path + "/../other"));
    CHECK(PathIsInside(tmp.path, tmp.path));

    TempDir outside("safejoin_out");
    std::error_code ec;
    fs::create_directory_symlink(outside.path, tmp.Sub("link"), ec);
    if (!ec) {
        CHECK(SafeJoin(tmp.path, "link/x.json").empty());
    }
}

TEST_CASE("LoadProject refuses project files that point outside the project") {
    for (const char* bad : {"../../evil.imb.json", "/tmp/evil.imb.json", "C:/evil.imb.json", "panels\\\\..\\\\evil.imb.json"}) {
        TempDir tmp("load");
        Spit(tmp.Sub("imforge.json"), ProjectJsonWith(std::string("\"") + bad + "\""));
        Project p;
        Error e;
        CHECK_FALSE(LoadProject(tmp.path, p, &e));
        CHECK(e.message.find("unsafe file path") != std::string::npos);
    }
}

TEST_CASE("SaveProject never writes outside the project directory") {
    TempDir parent("save");
    std::string proj = parent.Sub("proj");
    fs::create_directories(proj);
    Project p;
    p.dir = proj;
    p.name = "Evil";
    Panel panel;
    panel.name = "Main";
    panel.file = "../escaped.imb.json";
    panel.root.id = "window";
    panel.root.type = "Window";
    p.panels.push_back(panel);
    Error e;
    CHECK_FALSE(SaveProject(p, &e));
    CHECK(e.message.find("outside the project") != std::string::npos);
    CHECK_FALSE(Exists(parent.Sub("escaped.imb.json")));

    p.panels[0].file = parent.path + "/abs_escaped.imb.json";
    CHECK_FALSE(SaveProject(p, &e));
    CHECK_FALSE(Exists(parent.Sub("abs_escaped.imb.json")));

    p.panels[0].file.clear();
    p.panels[0].name = "../../traversal";
    REQUIRE_MESSAGE(SaveProject(p, &e), e.message);
    CHECK(Exists(proj + "/panels/.._.._traversal.imb.json"));
    CHECK_FALSE(Exists(parent.Sub("traversal.imb.json")));
}

TEST_CASE("WriteTextFile refuses to write through a symlink") {
    TempDir tmp("symlink");
    std::string victim = tmp.Sub("victim.txt");
    Spit(victim, "precious");
    std::error_code ec;
    fs::create_symlink(victim, tmp.Sub("link.json"), ec);
    if (ec) return;
    Error e;
    CHECK_FALSE(WriteTextFile(tmp.Sub("link.json"), "overwritten", &e));
    std::string content;
    REQUIRE(ReadTextFile(victim, content));
    CHECK(content == "precious");
}

TEST_CASE("project-supplied output directories outside the project need confirmation") {
    TempDir parent("outdir");
    std::string proj = parent.Sub("proj");
    fs::create_directories(proj);
    Spit(proj + "/imforge.json", ProjectJsonWith("\"panels/Main.imb.json\"", "../outside_src"));
    Spit(proj + "/panels/Main.imb.json", kPanelJson);

    Project p;
    Error e;
    REQUIRE_MESSAGE(LoadProject(proj, p, &e), e.message);
    std::string out = NormalizePath(JoinPath(p.dir, p.codegen.output_dir));
    CHECK_FALSE(OutputDirAllowed(p, out));

    CHECK(OutputDirAllowed(p, p.dir + "/generated"));

    ApproveExternalOutputDir(out);
    CHECK(OutputDirAllowed(p, out));

    Project q;
    REQUIRE_MESSAGE(LoadProject(proj, q, &e), e.message);
    std::string other = NormalizePath(JoinPath(q.dir, "../another"));
    CHECK_FALSE(OutputDirAllowed(q, other));
    q.codegen.output_dir = other;
    CHECK(OutputDirAllowed(q, other));
}

TEST_CASE("FlushPendingDeletes only removes panel files inside the project") {
    TempDir parent("flush");
    std::string proj = parent.Sub("proj");
    fs::create_directories(proj + "/panels");
    Spit(proj + "/panels/old.imb.json", "{}");
    Spit(parent.Sub("victim.imb.json"), "{}");
    Project p;
    p.dir = proj;
    QueuePanelFileDelete(proj, "../victim.imb.json");
    QueuePanelFileDelete(proj, parent.Sub("victim.imb.json"));
    QueuePanelFileDelete(proj, "panels/old.imb.json");
    FlushPendingDeletes(p);
    CHECK(Exists(parent.Sub("victim.imb.json")));
    CHECK_FALSE(Exists(proj + "/panels/old.imb.json"));
    ClearPanelFileOps();
}

TEST_CASE("NewProjectFromTemplate keeps a traversal project name inside the destination") {
    TempDir parent("tpl");
    std::string tpl = parent.Sub("tpl");
    Spit(tpl + "/{{PROJECT_NAME}}/note.txt", "hello {{PROJECT_NAME}}");
    std::string dest = parent.Sub("dest");
    Project settings;
    Error e;
    REQUIRE_MESSAGE(NewProjectFromTemplate(tpl, dest, "../../pwn", settings, &e), e.message);
    CHECK(Exists(dest + "/.._.._pwn/note.txt"));
    CHECK_FALSE(Exists(parent.Sub("pwn")));
    std::string text;
    REQUIRE(ReadTextFile(dest + "/.._.._pwn/note.txt", text));
    CHECK(text == "hello ../../pwn");
}
