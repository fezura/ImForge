#pragma once

// more integration cases, #included at the end of integration_tests.cpp to keep one executable

#include <chrono>
#include <thread>

#ifdef ITEST_HAVE_CLIENT
#include "live_test_builder.h"
#endif

// live loop
#ifdef ITEST_HAVE_CLIENT
namespace {

using BuilderSide = imforge::test::LiveTestBuilder;

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

TEST_CASE("live: builder/app loop over tcp loopback with the CheatMenu project") {
    LoadedProject& lp = Loaded("CheatMenu");
    const LoadedFile* main_menu = nullptr;
    for (const LoadedFile& f : lp.panel_files)
        if (f.json["name"].AsString() == "MainMenu") main_menu = &f;
    REQUIRE(main_menu);

    BuilderSide builder;
    REQUIRE(builder.Listen());
    test::HeadlessImGui gui;
    ExampleData d;
    int frames = 0;
    auto frame = [&] {
        gui.BeginFrame();
        live::NewFrame();
        live::Draw("MainMenu", {live::Bind("player", d.player), live::Bind("cfg", d.cfg)});
        gui.EndFrame();
        builder.Pump();
        ++frames;
    };
    auto until = [&](const std::function<bool()>& pred, uint64_t timeout_ms) {
        uint64_t deadline = NowMs() + timeout_ms;
        for (;;) {
            frame();
            if (pred()) return true;
            if (NowMs() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    live::SetAppName("IntegrationTest");
    REQUIRE(live::Connect(builder.Url().c_str()));
    REQUIRE_MESSAGE(until([&] { return live::IsConnected() && builder.t->IsConnected(); }, 5000), "connect timeout: " << live::Status());
    REQUIRE_MESSAGE(until([&] { return builder.First(msg::kHello) != nullptr; }, 5000), "no hello within 5 s");
    HelloMsg hello;
    REQUIRE(ParseHello(*builder.First(msg::kHello), hello));
    CHECK(hello.pairing);
    CHECK(hello.app.empty());
    CHECK(builder.pair.Established());
    CHECK(live::IsPaired());
    REQUIRE_MESSAGE(until([&] { return builder.First(msg::kClientInfo) != nullptr; }, 5000), "no client_info within 5 s");
    ClientInfoMsg info;
    REQUIRE(ParseClientInfo(*builder.First(msg::kClientInfo), info));
    CHECK(info.app == "IntegrationTest");
    CHECK(info.imgui_version == IMGUI_VERSION_NUM);
    CHECK(info.pid > 0);
    CHECK(info.backend == "headless");
    CHECK(builder.pair.SecurityEvents() == 0);
    CHECK(live::DebugPanelCount() == 0);
    CHECK(live::DebugItemCount() == 0);

    builder.Send(MsgProject(lp.project_file.json));
    std::vector<Json> comps;
    for (const LoadedFile& f : lp.component_files) comps.push_back(f.json);
    builder.Send(MsgComponents(comps));
    for (const LoadedFile& f : lp.theme_files) builder.Send(MsgTheme(f.json));
    builder.Send(MsgPanel(main_menu->json));
    REQUIRE_MESSAGE(until([&] { return live::DebugPanelCount() >= 1 && live::DebugItemCount() > 0; }, 5000),
                    "panel never drawn: panels=" << live::DebugPanelCount() << " items=" << live::DebugItemCount());
    frame();
    frame();
    size_t items_before = live::DebugItemCount();
    ImVec2 mn, mx;
    CHECK(live::DebugItemRect("MainMenu", "btn_panic", &mn, &mx));
    CHECK(live::DebugItemRect("MainMenu", "aim_enabled", &mn, &mx));
    CHECK_FALSE(live::DebugItemRect("MainMenu", "live_added_text", &mn, &mx));
    MESSAGE("live: " << items_before << " items drawn from the MainMenu definition");

    Json updated = main_menu->json;
    Json root = updated["root"];
    Json children = Json::Array();
    Json added = Json::Object();
    added.Set("id", Json("live_added_text"));
    added.Set("type", Json("Text"));
    Json props = Json::Object();
    props.Set("text", Json("LIVE UPDATE MARKER {player.name}"));
    added.Set("props", props);
    children.Push(added);
    for (size_t i = 0; i < root["children"].Size(); ++i) children.Push(root["children"][i]);
    root.Set("children", children);
    updated.Set("root", root);
    builder.Send(MsgPanel(updated));
    REQUIRE_MESSAGE(until([&] { return live::DebugItemRect("MainMenu", "live_added_text", &mn, &mx); }, 5000),
                    "panel update not applied within 5 s");
    frame();
    CHECK(live::DebugItemCount() >= items_before + 1);
    CHECK(live::DebugItemRect("MainMenu", "btn_panic", &mn, &mx));

    CHECK_MESSAGE(until([&] { return builder.Count(msg::kStats) > 0; }, 3000), "no stats message within 3 s");
    if (const Json* s = builder.First(msg::kStats)) {
        StatsMsg st;
        REQUIRE(ParseStats(*s, st));
        bool has_main = false;
        for (const std::string& p : st.panels) has_main = has_main || p == "MainMenu";
        CHECK(has_main);
    }
    size_t logs = builder.Count(msg::kLog);
    for (const Json& m : builder.received)
        if (IsMsg(m, msg::kLog)) MESSAGE("app log: " << DumpJson(m, 0));
    MESSAGE("live: " << frames << " frames, " << builder.received.size() << " messages received (" << logs << " log)");

    builder.Send(MsgRemovePanel("MainMenu"));
    CHECK(until([&] { return live::DebugPanelCount() == 0; }, 3000));
    live::Disconnect();
    CHECK_FALSE(live::IsConnected());
}
#else
TEST_CASE("live: SKIPPED (imforge_client.h not found)") { MESSAGE("SKIP: include/imforge/imforge_client.h missing"); }
#endif

// amalgamation
TEST_CASE("amalgamation: tools/amalgamate.py output compiles with and without IMFORGE_LIVE") {
    fs::path script = RepoRoot() / "tools" / "amalgamate.py";
    std::error_code ec;
    if (!fs::is_regular_file(script, ec)) {
        MESSAGE("SKIP: " << script.string() << " not present yet");
        return;
    }
    if (!HavePython()) {
        MESSAGE("SKIP: python not available");
        return;
    }
    fs::path out_dir = FreshDir("amalgamation");
    std::string out;
    int rc = Run(Python() + " -I \"" + script.string() + "\" --out-dir \"" + out_dir.string() + "\"", out, RepoRoot());
    CHECK_MESSAGE(rc == 0, "amalgamate.py failed (" << rc << "):\n" << FirstLines(out, 30));
    fs::path single = out_dir / "imforge_client_single.h";
    REQUIRE_MESSAGE(fs::is_regular_file(single, ec), "amalgamate.py did not write imforge_client_single.h");
    std::string text;
    REQUIRE(ReadFile(single, text));
    CHECK(text.size() > 100000);
    CHECK_MESSAGE(text.find("#include \"imforge_") == std::string::npos, "repository include left in the amalgamation");
    CHECK_MESSAGE(text.find("#include \"imforge/") == std::string::npos, "repository include left in the amalgamation");
    CHECK_MESSAGE(text.find("#include \"imb_") == std::string::npos, "repository include left in the amalgamation");
    std::string dist_text;
    if (ReadFile(RepoRoot() / "dist" / "imforge_client_single.h", dist_text) && dist_text != text)
        MESSAGE("note: dist/imforge_client_single.h is stale relative to the current headers (" << DiffReport(text, dist_text) << ")");
    if (!HaveCompiler()) {
        MESSAGE("SKIP compile: compiler not available");
        return;
    }
    int checks = 0;
    for (const std::string tag : {"v1.85", "v1.91.9b"}) {
        fs::path imgui = ImGuiCheckout(tag.c_str());
        if (imgui.empty()) {
            MESSAGE("SKIP: imgui " << tag << " not found under " << DepsRoot().string());
            continue;
        }
        for (int live_on = 0; live_on < 2; ++live_on) {
            std::vector<std::string> defs;
            if (live_on) defs.push_back("IMFORGE_LIVE");
            CompileResult c = SyntaxCheck(single, {imgui}, defs, true);
            ++checks;
            CHECK_MESSAGE((c.rc == 0 && c.errors == 0), "imforge_client_single.h @ " << tag << (live_on ? " +IMFORGE_LIVE" : "") << " failed:\n$ "
                                                                                     << c.command << "\n" << FirstLines(c.output, 40));
            CHECK_MESSAGE(c.warnings == 0, "imforge_client_single.h @ " << tag << (live_on ? " +IMFORGE_LIVE" : "") << ": " << c.warnings
                                                                        << " warnings\n" << FirstLines(c.output, 40));
        }
    }
    MESSAGE("amalgamation: " << checks << " compile checks run");
}

// version matrix
TEST_CASE("version matrix: tools/check_version_matrix.py exits 0") {
    fs::path script = RepoRoot() / "tools" / "check_version_matrix.py";
    std::error_code ec;
    if (!fs::is_regular_file(script, ec)) {
        MESSAGE("SKIP: " << script.string() << " not present");
        return;
    }
    if (!HavePython() || !HaveCompiler()) {
        MESSAGE("SKIP: python or compiler not available");
        return;
    }
    std::string dirs;
    for (const char* tag : {"v1.85", "v1.87", "v1.89.9", "v1.90.9", "v1.91.9b", "v1.91.9b-docking"}) {
        fs::path p = ImGuiCheckout(tag);
        if (!p.empty()) dirs += " \"" + p.string() + "\"";
    }
    if (dirs.empty()) {
        MESSAGE("SKIP: no pinned imgui checkouts under " << DepsRoot().string());
        return;
    }
    std::string out;
    int rc = Run(Python() + " -I \"" + script.string() + "\" --compiler " + Compiler() + " --imgui-dirs" + dirs, out, RepoRoot());
    std::vector<std::string> lines;
    std::istringstream ss(out);
    for (std::string line; std::getline(ss, line);) lines.push_back(line);
    std::string tail;
    for (size_t i = lines.size() > 60 ? lines.size() - 60 : 0; i < lines.size(); ++i) tail += lines[i] + "\n";
    MESSAGE("check_version_matrix.py output (tail):\n" << tail);
    CHECK_MESSAGE(rc == 0, "check_version_matrix.py exited with " << rc);
}
