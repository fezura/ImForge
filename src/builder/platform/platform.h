#pragma once

#include <memory>
#include <vector>

#include "imgui.h"

namespace imforge::builder {

struct Platform {
    virtual ~Platform() {}
    virtual bool Init(const char* title, int w, int h) = 0;
    virtual bool PollEvents(bool& quit) = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame(const ImVec4& clear) = 0;
    virtual void Shutdown() = 0;
    virtual float DpiScale() = 0;
    virtual ImTextureID CreateTextureRGBA(const unsigned char* pixels, int w, int h) = 0;
    virtual void DestroyTexture(ImTextureID tex) = 0;
    virtual void* GetProcAddress(const char* name) = 0;
    virtual const char* Name() const = 0;
    virtual void SetTitle(const char*) {}
    virtual void RebuildFontAtlas() {}
    virtual void GetWindowSize(int& w, int& h) { w = h = 0; }
    virtual bool IsMaximized() { return false; }
    virtual void SetMaximized(bool) {}
    virtual bool RequestCapture() { return false; }
    virtual bool FetchCapture(int&, int&, std::vector<unsigned char>&) { return false; }
};

std::unique_ptr<Platform> CreatePlatform();

}  // namespace imforge::builder
