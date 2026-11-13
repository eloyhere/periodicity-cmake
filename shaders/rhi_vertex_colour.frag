// Periodicity — RHI device: the "vertexColour" fragment stage.
//
// The interpolated per-vertex colour, premultiplied on output.

#version 450

layout(location = 1) in vec4 vColor;

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
    fragColor = vec4(vColor.rgb * vColor.a, vColor.a);
}
