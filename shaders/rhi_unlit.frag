// Periodicity — RHI device: the "unlit" fragment stage, also used for
// DrawImage.
//
// The texture is always sampled; uParams.y==0 selects a white texel, which is
// why untextured unlit draws bind a 1x1 white dummy instead of branching around
// the sample (a branch around an implicit-derivative sample is undefined on
// some backends).

#version 450

layout(location = 1) in vec4 vColor;
layout(location = 2) in vec2 vUv;

layout(binding = 1) uniform sampler2D uTexture;

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
    vec4 texel = texture(uTexture, vUv);
    vec4 base = vColor * mix(vec4(1.0), texel, ubo.uParams.y);
    fragColor = vec4(base.rgb * base.a, base.a);
}
