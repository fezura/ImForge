#version 450
layout(location = 0) in vec2 v_local;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 o_color;
void main() {
    float edge = 1.0 - smoothstep(0.85, 1.0, max(abs(v_local.x), abs(v_local.y)));
    o_color = vec4(v_color.rgb * (0.75 + 0.25 * v_local.y), v_color.a * edge);
}
