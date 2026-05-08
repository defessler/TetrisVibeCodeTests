#version 450
// Sprite quad vertex shader.
// Input is a unit quad (0,0)..(1,1).  Push constants supply the NDC
// position and size, so we just transform and pass the UV through.

layout(location = 0) in vec2 inPos;
layout(location = 0) out vec2 vUV;

layout(push_constant) uniform PC {
    vec2 pos;    //  8 b   NDC top-left   (Vulkan: y down)
    vec2 size;   //  8 b   NDC width/height
    vec4 mid;    // 16 b   xyz=main rgb,    w=style (0=solid,1=ghost,2=empty)
    vec4 hi;     // 16 b   xyz=highlight,   w=unused
    vec4 lo;     // 16 b   xyz=shadow,      w=unused
} pc;

void main() {
    vUV = inPos;
    vec2 ndc = pc.pos + inPos * pc.size;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
