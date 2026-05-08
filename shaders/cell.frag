#version 450
// Sprite fragment shader.
//
// style == 0  → solid bevel block (top/left = highlight, bottom/right = shadow)
// style == 1  → hollow ghost outline
// style == 2  → empty grid cell (very dark with subtle centre marker)

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    vec2 pos;
    vec2 size;
    vec4 mid;     // w = style flag
    vec4 hi;
    vec4 lo;
} pc;

void main() {
    float style = pc.mid.w;
    vec3  midC  = pc.mid.xyz;
    vec3  hiC   = pc.hi.xyz;
    vec3  loC   = pc.lo.xyz;

    // Empty grid cell ─ very dark with a soft centre dot
    if (style > 1.5) {
        float d   = length(vUV - 0.5) * 2.0;
        float dot = 1.0 - smoothstep(0.10, 0.20, d);
        outColor  = vec4(midC + hiC * dot * 0.06, 1.0);
        return;
    }

    // Ghost piece ─ thin hollow outline
    if (style > 0.5) {
        float b = 0.13;
        if (vUV.x > b && vUV.x < 1.0 - b &&
            vUV.y > b && vUV.y < 1.0 - b)
            discard;
        outColor = vec4(hiC, 1.0);
        return;
    }

    // Solid block ─ classic NES-style left-lit bevel
    float bevel = 0.16;
    vec3  col   = midC;
    if (vUV.y < bevel)        col = hiC;
    if (vUV.x < bevel)        col = hiC;
    if (vUV.y > 1.0 - bevel)  col = loC;
    if (vUV.x > 1.0 - bevel)  col = loC;

    // Soft inner border to suggest a slight inset on the centre face
    float insetX = step(bevel + 0.02, vUV.x) * step(vUV.x, 1.0 - bevel - 0.02);
    float insetY = step(bevel + 0.02, vUV.y) * step(vUV.y, 1.0 - bevel - 0.02);
    if (insetX * insetY > 0.5) {
        // soften the centre slightly toward highlight for a glassy feel
        col = mix(col, hiC, 0.05);
    }

    outColor = vec4(col, 1.0);
}
