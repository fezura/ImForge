#version 450
layout(push_constant) uniform PC {
    vec4 rect;      // min.x, min.y, max.x, max.y in ImGui coordinates
    vec4 display;   // DisplayPos.x, DisplayPos.y, DisplaySize.x, DisplaySize.y
    vec4 tint;
    vec4 p0;        // rounding, lod, saturation, brightness
    vec4 p1;        // noise, unused, unused, unused
} pc;
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec2 v_pos;
void main() {
    int corner = gl_VertexIndex < 3 ? gl_VertexIndex : (gl_VertexIndex == 3 ? 0 : gl_VertexIndex - 2);
    vec2 t = vec2((corner == 1 || corner == 2) ? 1.0 : 0.0, (corner == 2 || corner == 3) ? 1.0 : 0.0);
    vec2 p = mix(pc.rect.xy, pc.rect.zw, t);
    v_pos = p;
    v_uv = (p - pc.display.xy) / pc.display.zw;
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
