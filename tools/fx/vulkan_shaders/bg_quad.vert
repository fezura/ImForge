#version 450
layout(push_constant) uniform PC {
    vec4 center_size;   // center.x, center.y (NDC), half_size.x, half_size.y (NDC)
    vec4 color;
    vec4 angle;         // rotation in radians, aspect (w/h), unused, unused
} pc;
layout(location = 0) out vec2 v_local;
layout(location = 1) out vec4 v_color;
void main() {
    int corner = gl_VertexIndex < 3 ? gl_VertexIndex : (gl_VertexIndex == 3 ? 0 : gl_VertexIndex - 2);
    vec2 t = vec2((corner == 1 || corner == 2) ? 1.0 : -1.0, (corner == 2 || corner == 3) ? 1.0 : -1.0);
    float s = sin(pc.angle.x), c = cos(pc.angle.x);
    vec2 p = vec2(t.x * c - t.y * s, t.x * s + t.y * c) * pc.center_size.zw;
    p.x /= pc.angle.y;
    v_local = t;
    v_color = pc.color;
    gl_Position = vec4(pc.center_size.xy + p, 0.0, 1.0);
}
