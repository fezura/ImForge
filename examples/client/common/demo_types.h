#pragma once
// Sample game state used by every example app. Copy this file as a template for your own types:
// the IMFORGE_REFLECT lines are what the builder needs to bind widgets to your variables.
#include "imgui.h"
#include "imforge/imforge_client.h"

#include <cmath>
#include <cstdio>

#ifndef IMFORGE_REFLECT
#define IMFORGE_REFLECT(...)
#endif
#ifndef IMFORGE_REFLECT_ENUM
#define IMFORGE_REFLECT_ENUM(...)
#endif

enum class Mode { Off = 0, Legit = 1, Rage = 2 };

struct Player {
    char name[32];
    float health, max_hp;
    ImVec2 pos;
    int ammo;
    Mode mode;
};

struct EspSettings {
    bool enabled;
    ImVec4 color;
    int box_type;
    float max_distance;
};

struct AimSettings {
    bool enabled;
    float fov, smooth;
    int bone, key;
};

struct MiscSettings {
    bool bhop;
    float speed;
    bool fov_changer;
    float fov;
    bool watermark;
    bool crosshair;
    bool show_fov;
    int menu_key;
};

struct Settings {
    EspSettings esp;
    AimSettings aim;
    MiscSettings misc;
};

IMFORGE_REFLECT_ENUM(Mode, Off, Legit, Rage);
IMFORGE_REFLECT(Player, name, health, max_hp, pos, ammo, mode);
IMFORGE_REFLECT(EspSettings, enabled, color, box_type, max_distance);
IMFORGE_REFLECT(AimSettings, enabled, fov, smooth, bone, key);
IMFORGE_REFLECT(MiscSettings, bhop, speed, fov_changer, fov, watermark, crosshair, show_fov, menu_key);
IMFORGE_REFLECT(Settings, esp, aim, misc);

inline Player MakeDemoPlayer() {
    Player p = {};
    std::snprintf(p.name, sizeof(p.name), "%s", "Forge");
    p.health = 100.0f;
    p.max_hp = 100.0f;
    p.pos = ImVec2(640.0f, 360.0f);
    p.ammo = 30;
    p.mode = Mode::Off;
    return p;
}

inline Settings MakeDemoSettings() {
    Settings s = {};
    s.esp.enabled = true;
    s.esp.color = ImVec4(1.0f, 0.35f, 0.2f, 1.0f);
    s.esp.box_type = 1;
    s.esp.max_distance = 250.0f;
    s.aim.enabled = false;
    s.aim.fov = 5.0f;
    s.aim.smooth = 3.0f;
    s.aim.bone = 0;
    s.aim.key = 1;
    s.misc.bhop = false;
    s.misc.speed = 1.5f;
    s.misc.fov_changer = false;
    s.misc.fov = 90.0f;
    s.misc.watermark = true;
    s.misc.crosshair = true;
    s.misc.show_fov = true;
    s.misc.menu_key = 0;
    return s;
}

inline void SimulateGame(Player& p, float dt) {
    static float time = 0.0f;
    static float shot_timer = 0.0f;
    time += dt;
    shot_timer += dt;
    p.pos = ImVec2(640.0f + std::cos(time * 0.7f) * 220.0f, 360.0f + std::sin(time * 0.7f) * 130.0f);
    p.health = p.max_hp * (0.55f + 0.45f * std::sin(time * 0.9f));
    if (shot_timer > 0.25f) {
        shot_timer = 0.0f;
        p.ammo = p.ammo > 0 ? p.ammo - 1 : 30;
    }
    p.mode = static_cast<Mode>(static_cast<int>(time / 4.0f) % 3);
}
