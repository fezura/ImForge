#pragma once

#include "imgui.h"

#include <cstdint>

namespace imforge {
namespace test {

struct HeadlessImGui {
    ImGuiContext* ctx = nullptr;
    int frames = 0;

    explicit HeadlessImGui(float width = 1280.0f, float height = 720.0f) {
        IMGUI_CHECKVERSION();
        ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(width, height);
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
#if IMGUI_VERSION_NUM < 19200
        io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
#endif
#else
#if IMGUI_VERSION_NUM >= 19200
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
#else
        io.Fonts->Build();
#endif
#endif
    }
    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;
    ~HeadlessImGui() {
        if (ctx) ImGui::DestroyContext(ctx);
    }

    void BeginFrame() {
        ImGui::SetCurrentContext(ctx);
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
    }
    ImDrawData* EndFrame() {
        ImGui::Render();
        ++frames;
        return ImGui::GetDrawData();
    }
    template <class F> ImDrawData* Frame(F&& body) {
        BeginFrame();
        body();
        return EndFrame();
    }
    template <class F> ImDrawData* Frames(int count, F&& body) {
        ImDrawData* dd = nullptr;
        for (int i = 0; i < count; ++i) dd = Frame(body);
        return dd;
    }
};

}  // namespace test
}  // namespace imforge
