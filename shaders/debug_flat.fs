#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 FragPos;
in vec3 Normal;
in vec3 Tangent;
in vec3 Bitangent;

uniform bool hasBaseColor;
uniform sampler2D texture_diffuse1;
uniform vec4 baseColorFactor;
uniform vec4 texture_diffuse1_uv;
uniform float texture_diffuse1_rot;

vec2 applyUV(vec2 uv, vec4 uvparams, float rot)
{
    vec2 offset = uvparams.xy;
    vec2 scale = uvparams.zw;
    float c = cos(rot);
    float s = sin(rot);
    mat2 R = mat2(c, -s, s, c);
    return offset + (R * (uv * scale));
}

void main()
{
    vec2 uv = TexCoords;
    if (hasBaseColor) uv = applyUV(TexCoords, texture_diffuse1_uv, texture_diffuse1_rot);
    vec4 base = hasBaseColor ? texture(texture_diffuse1, uv) : vec4(baseColorFactor.rgb, baseColorFactor.a);
    // simple gamma-corrected output for visibility
    vec3 outc = pow(base.rgb, vec3(1.0/2.2));
    FragColor = vec4(outc, base.a);
}
