// Periodicity — RHI device: the vertex stage shared by every built-in program.
//
// The same shader serves Mesh2D, Mesh3D and the textured-image quad. There is
// exactly one transform path:
//
//     gl_Position = uClip * (uMvp * vec4(aPosition, 1.0))
//
//   * Mesh2D and DrawImage vertices arrive with x/y already in OpenGL-style
//     normalised device coordinates (-1..1, y up). The RHI device bakes the
//     pixel-to-NDC transform when it records the draw, and passes an identity
//     uMvp, so these draws depend on no per-draw size uniform at all.
//   * Mesh3D vertices are model space and uMvp (the command's "mvp", identity
//     by default) produces the same OpenGL-style clip space the software
//     rasterizer uses.
//
// uClip is QRhi::clipSpaceCorrMatrix(). It maps that OpenGL-style clip space to
// the backend's (for D3D11 it only remaps z from -1..1 to 0..1), which is what
// lets one qsb file serve D3D11, Vulkan and OpenGL without a rebuild.

#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec2 aUv;

layout(std140, binding = 0) uniform RhiUniforms
{
    mat4 uClip;            // QRhi::clipSpaceCorrMatrix()
    mat4 uMvp;             // gfx uniform "mvp"; identity for 2D and images
    vec4 uColour;          // gfx uniform "colour"
    vec4 uBaseColour;      // gfx uniform "baseColour"
    vec4 uTargetSize;      // xy = target pixels (fragment stages only)
    vec4 uLightDirection;  // xyz = gfx uniform "lightDirection"
    vec4 uParams;          // x = ambient, y = hasTexture, z = is3D
} ubo;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec2 vUv;

void main()
{
    vNormal = aNormal;
    vColor = aColor;
    vUv = aUv;

    gl_Position = ubo.uClip * (ubo.uMvp * vec4(aPosition, 1.0));
}
