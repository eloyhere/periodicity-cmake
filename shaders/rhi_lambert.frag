// Periodicity — RHI device: the "lambert" fragment stage.
//
// Mirrors the software and OpenGL devices: a normalised light direction (with a
// (0,0,1) fallback for a zero vector), a normalised vertex normal when it is
// non-zero, ambient + (1 - ambient) * max(dot(n, l), 0), albedo multiplied by
// the interpolated vertex colour.

#version 450

layout(location = 0) in vec3 vNormal;
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
    vec3 light = ubo.uLightDirection.xyz;
    if (dot(light, light) <= 0.0)
        light = vec3(0.0, 0.0, 1.0);
    else
        light = normalize(light);

    vec3 normal = vNormal;
    if (dot(normal, normal) > 0.0)
        normal = normalize(normal);

    float intensity = clamp(dot(normal, light), 0.0, 1.0);
    float ambient = clamp(ubo.uParams.x, 0.0, 1.0);
    float factor = ambient + (1.0 - ambient) * intensity;
    vec3 base = ubo.uBaseColour.rgb * vColor.rgb;
    float alpha = ubo.uBaseColour.a * vColor.a;
    vec3 colour = base * factor;
    fragColor = vec4(colour * alpha, alpha);
}
