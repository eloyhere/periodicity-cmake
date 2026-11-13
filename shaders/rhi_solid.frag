// Periodicity — RHI device: the "solid" fragment stage.
//
// One uniform colour for the whole primitive. The output is premultiplied
// because the offscreen target is ARGB32_Premultiplied and the blend state is
// the premultiplied one (One, OneMinusSrcAlpha), which is exactly what the
// OpenGL device and the software rasterizer's final write do.

#version 450

layout(std140, binding = 0) uniform RhiUniforms
{
    mat4 uClip;
    mat4 uMvp;
    vec4 uColour;
    vec4 uBaseColour;
    vec4 uTargetSize;
    vec4 uLightDirection;
    vec4 uParams;
} ubo;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = vec4(ubo.uColour.rgb * ubo.uColour.a, ubo.uColour.a);
}
