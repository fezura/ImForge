#pragma once

#include <memory>

#include "imgui.h"

namespace imforge::builder {

struct EditorContext;

struct EditorPanel {
    virtual ~EditorPanel() {}
    virtual const char* Name() const = 0;
    virtual void Draw(EditorContext&) = 0;
    virtual const char* DefaultDock() const { return nullptr; }
    virtual const char* Icon() const { return nullptr; }
    virtual bool BeginWindow(EditorContext&) { return ImGui::Begin(Name(), &open, flags); }
    virtual void EndWindow(EditorContext&) { ImGui::End(); }
    virtual void OnProjectChanged(EditorContext&) {}
    bool open = true;
    bool show_in_view_menu = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
};

using PanelFactoryFn = std::unique_ptr<EditorPanel> (*)();

struct PanelFactory {
    int priority;
    const char* type_name;
    PanelFactoryFn create;
};

bool RegisterPanelFactory(int priority, const char* type_name, PanelFactoryFn create);

}  // namespace imforge::builder

#define IMFORGE_PANEL_CONCAT2(a, b) a##b
#define IMFORGE_PANEL_CONCAT(a, b) IMFORGE_PANEL_CONCAT2(a, b)

#define IMFORGE_REGISTER_PANEL(Type, Priority)                                                        \
    static const bool IMFORGE_PANEL_CONCAT(imforge_panel_registered_, __COUNTER__) =                    \
        ::imforge::builder::RegisterPanelFactory((Priority), #Type, []() -> std::unique_ptr<::imforge::builder::EditorPanel> { \
            return std::unique_ptr<::imforge::builder::EditorPanel>(new Type());                       \
        })
