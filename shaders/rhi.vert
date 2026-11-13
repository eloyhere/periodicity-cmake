// Periodicity — RHI device: the vertex stage shared by every built-in program.
//
// The same shader serves Mesh2D, Mesh3D and the textured-image quad, exactly as
// the OpenGL device's single vertex program does. `uParams.z` selects the path:
//
//   0  A Mesh2D / image vertex whose position is already in target pixels.
//      It is mapped to OpenGL-style NDC (top-left origin) and corrected for the
//      backend by uClip.
//   1  A Mesh3D vertex in model space; the command's mvp produces the same
//      OpenGL-style clip space the software rasterizer uses, then uClip maps it
//      to the backend's clip space (Y direction, depth range).
//
// uClip is QRhi::clipSpaceCorrMatrix(). Keeping it out of the vertex data is
// what lets one qsb file serve D3D11, Vulkan and OpenGL without a rebuild.

#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aUv;

layout(std140, binding = 0) uniform RhiUniforms
{
    mat4 uClip;            // QRhi::clipSpaceCorrMatrix()
    mat4 uMvp;             // gfx uniform "mvp"
    vec4 uColour;          // gfx uniform "colour"
    vec4 uBaseColour;      // gfx uniform "baseColour"
    vec4 uTargetSize;      // xy = target pixels
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

    if (ubo.uParams.z != 0.0) {
        gl_Position = ubo.uClip * (ubo.uMvp * vec4(aPosition, 1.0));
    } else {
        vec2 ndc = vec2(2.0 * aPosition.x / ubo.uTargetSize.x - 1.0,
                        1.0 - 2.0 * aPosition.y / ubo.uTargetSize.y);
        gl_Position = ubo.uClip * vec4(ndc, 0.0, 1.0);
    }
}
