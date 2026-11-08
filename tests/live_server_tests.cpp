#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#ifndef IMFORGE_LIVE
#define IMFORGE_LIVE
#endif
#define IMFORGE_BACKEND_NAME "headless"
#include "imforge/imforge_client.h"
#include "imgui_headless.h"
#include "live_test_builder.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

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

namespace imforge::builder {
std::string ExeDir() { return std::filesystem::current_path().generic_string(); }
void RequestOverwriteConfirm(EditorContext&, const std::vector<std::string>&) {}
}  // namespace imforge::builder

using namespace imforge;
using namespace imforge::builder;
namespace live = imforge::live;

namespace {

template <class Pred> bool PumpUntil(EditorContext& ctx, test::HeadlessImGui& ui, Pred pred, uint64_t timeout_ms = 5000) {
    uint64_t deadline = imforge::detail::NowMs() + timeout_ms;
    for (;;) {
        ctx.live.Poll(ctx);
        live::PreNewFrame();
        ui.BeginFrame();
        live::NewFrame();
        ui.EndFrame();
        ctx.live.Poll(ctx);
        if (pred()) return true;
        if (imforge::detail::NowMs() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void PumpFor(EditorContext& ctx, test::HeadlessImGui& ui, int frames) {
    for (int i = 0; i < frames; ++i) {
        PumpUntil(ctx, ui, [] { return false; }, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

}  // namespace

TEST_CASE("a second client_info on the same connection does not make the builder send everything again") {
    test::UseTestPairingDir();
    int port = 0;
    {
        std::unique_ptr<Transport> probe = CreateTransport("tcp://127.0.0.1:0");
        REQUIRE(probe);
        REQUIRE(probe->Listen("tcp://127.0.0.1:0"));
        port = probe->BoundPort();
        probe->Close();
    }
    test::HeadlessImGui ui;
    EditorContext ctx;
    ctx.has_project = true;
    FontSpec spec;
    spec.name = "Body";
    spec.file = "body.ttf";
    ctx.project.fonts.push_back(spec);
    int plans = 0;
    ctx.live.SetFontPlanner([&plans](const Project&) {
        ++plans;
        return LiveFontPlan();
    });
    LiveSettings settings;
    settings.transport = "tcp";
    settings.tcp_port = port;
    std::string err;
    REQUIRE_MESSAGE(ctx.live.Start(settings, "127.0.0.1", &err), err);
    REQUIRE(live::Connect(ctx.live.Url().c_str()));
    REQUIRE(PumpUntil(ctx, ui, [&] { return ctx.live.Client().connected && ctx.live.IsConnected(); }));
    PumpFor(ctx, ui, 20);
    CHECK(plans == 1);
    const std::string app = ctx.live.Client().app;

    live::client_detail::SendJson(live::client_detail::Get(), MsgClientInfo("someone else", 4242, 19191, "b", "p", {}, {}, {}));
    PumpFor(ctx, ui, 40);
    CHECK(plans == 1);
    CHECK(ctx.live.Client().app == app);

    live::Disconnect();
    ctx.live.Stop();
}
