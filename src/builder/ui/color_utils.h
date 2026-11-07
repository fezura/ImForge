#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "imgui.h"

namespace imforge {
namespace builder {

inline int ColorByte(float f) {
    int v = (int)(f * 255.0f + 0.5f);
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

inline std::string ColorToHex(const ImVec4& c, bool with_alpha = true) {
    char buf[16];
    if (with_alpha)
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", ColorByte(c.x), ColorByte(c.y), ColorByte(c.z),
                      ColorByte(c.w));
    else
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", ColorByte(c.x), ColorByte(c.y), ColorByte(c.z));
    return buf;
}

inline int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline bool HexToColor(const std::string& text, ImVec4& out) {
    size_t start = 0;
    if (!text.empty() && text[0] == '#') start = 1;
    else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) start = 2;
    std::string hex = text.substr(start);
    for (char c : hex)
        if (HexNibble(c) < 0) return false;
    int v[4] = {0, 0, 0, 255};
    if (hex.size() == 3 || hex.size() == 4) {
        for (size_t i = 0; i < hex.size(); ++i) v[i] = HexNibble(hex[i]) * 17;
    } else if (hex.size() == 6 || hex.size() == 8) {
        for (size_t i = 0; i + 1 < hex.size(); i += 2) v[i / 2] = HexNibble(hex[i]) * 16 + HexNibble(hex[i + 1]);
    } else {
        return false;
    }
    out = ImVec4(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f);
    return true;
}

inline ImVec4 RgbToHsv(const ImVec4& c) {
    ImVec4 r;
    ImGui::ColorConvertRGBtoHSV(c.x, c.y, c.z, r.x, r.y, r.z);
    r.w = c.w;
    return r;
}

inline ImVec4 HsvToRgb(const ImVec4& hsv) {
    ImVec4 r;
    ImGui::ColorConvertHSVtoRGB(hsv.x, hsv.y, hsv.z, r.x, r.y, r.z);
    r.w = hsv.w;
    return r;
}

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline ImVec4 AdjustHsv(const ImVec4& c, float dh, float ds, float dv) {
    ImVec4 hsv = RgbToHsv(c);
    hsv.x = std::fmod(hsv.x + dh + 1.0f, 1.0f);
    hsv.y = Clamp01(hsv.y + ds);
    hsv.z = Clamp01(hsv.z + dv);
    return HsvToRgb(hsv);
}

inline float SrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float Luminance(const ImVec4& c) {
    return 0.2126f * SrgbToLinear(c.x) + 0.7152f * SrgbToLinear(c.y) + 0.0722f * SrgbToLinear(c.z);
}

inline float ContrastRatio(const ImVec4& a, const ImVec4& b) {
    float la = Luminance(a) + 0.05f, lb = Luminance(b) + 0.05f;
    return la > lb ? la / lb : lb / la;
}

inline ImVec4 ContrastTextColor(const ImVec4& bg) {
    return Luminance(bg) > 0.179f ? ImVec4(0, 0, 0, 1) : ImVec4(1, 1, 1, 1);
}

inline ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

inline ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

inline ImU32 ColorToU32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

inline ImVec4 U32ToColor(ImU32 v) { return ImGui::ColorConvertU32ToFloat4(v); }

inline std::string ColorToImVec4Code(const ImVec4& c) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "ImVec4(%.3ff, %.3ff, %.3ff, %.3ff)", c.x, c.y, c.z, c.w);
    return buf;
}

inline bool ColorsEqual(const ImVec4& a, const ImVec4& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

}  // namespace builder
}  // namespace imforge
