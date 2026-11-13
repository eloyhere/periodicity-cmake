// Periodicity — RHI device: the vertex stage for Mesh2D and DrawImage.
//
// Two-dimensional vertices carry no normal. The input layout for these draws
// declares position, colour and uv only, so no attribute can stand in for the
// colour, and the shader declares exactly those three inputs.
//
// Positions arrive already in OpenGL-style normalised device coordinates: the
// device bakes the pixel-to-NDC transform while recording the draw, so the only
// transform left is the clip-space correction (for D3D11 that is a z remap).

#version 450

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 3) in vec2 aUv;

layout(std140, binding = 0) uniform RhiUniforms
{
    mat4 uClip;            // QRhi::clipSpaceCorrMatrix()
    mat4 uMvp;             // unused for 2D; declared to keep the block identical
    vec4 uColour;
    vec4 uBaseColour;
    vec4 uTargetSize;
    vec4 uLightDirection;
    vec4 uParams;
} ubo;

layout(location = 1) out vec4 vColor;
layout(location = 2) out vec2 vUv;

void main()
{
    vColor = aColor;
    vUv = aUv;

    gl_Position = ubo.uClip * vec4(aPosition, 0.0, 1.0);
}
