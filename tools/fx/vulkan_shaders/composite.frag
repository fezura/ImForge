#version 450
layout(set = 0, binding = 0) uniform sampler2D u_blur;
layout(push_constant) uniform PC {
    vec4 rect;
    vec4 display;
    vec4 tint;
    vec4 p0;        // rounding, lod, saturation, brightness
    vec4 p1;        // noise, unused, unused, unused
} pc;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_pos;
layout(location = 0) out vec4 o_color;
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
void main() {
    vec2 half_size = (pc.rect.zw - pc.rect.xy) * 0.5;
    vec2 center = (pc.rect.xy + pc.rect.zw) * 0.5;
    float r = clamp(pc.p0.x, 0.0, min(half_size.x, half_size.y));
    vec2 d = abs(v_pos - center) - half_size + vec2(r);
    float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
    float mask = clamp(0.5 - dist / max(fwidth(dist), 1e-5), 0.0, 1.0);
    vec3 c = textureLod(u_blur, v_uv, pc.p0.y).rgb;
    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, pc.p0.z) * pc.p0.w;
    c += (hash12(gl_FragCoord.xy) - 0.5) * pc.p1.x * 0.0625;
    c *= pc.tint.rgb;
    o_color = vec4(c, pc.tint.a * mask);
}
