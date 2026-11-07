#version 450
layout(set = 0, binding = 0) uniform sampler2D u_src;
layout(push_constant) uniform PC { vec2 dir; vec2 pad; } pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() {
    vec3 c = texture(u_src, v_uv).rgb * 0.2270270270;
    c += texture(u_src, v_uv + pc.dir * 1.3846153846).rgb * 0.3162162162;
    c += texture(u_src, v_uv - pc.dir * 1.3846153846).rgb * 0.3162162162;
    c += texture(u_src, v_uv + pc.dir * 3.2307692308).rgb * 0.0702702703;
    c += texture(u_src, v_uv - pc.dir * 3.2307692308).rgb * 0.0702702703;
    o_color = vec4(c, 1.0);
}
