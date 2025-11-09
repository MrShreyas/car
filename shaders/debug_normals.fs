#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 FragPos;
in vec3 Normal;
in vec3 Tangent;
in vec3 Bitangent;

uniform bool hasNormalMap;
uniform sampler2D texture_normal1;
uniform vec4 texture_normal1_uv;
uniform float texture_normal1_rot;

vec2 applyUV(vec2 uv, vec4 uvparams, float rot)
{
    vec2 offset = uvparams.xy;
    vec2 scale = uvparams.zw;
    float c = cos(rot);
    float s = sin(rot);
    mat2 R = mat2(c, -s, s, c);
    return offset + (R * (uv * scale));
}

vec3 getNormalFromMap(vec3 n, vec3 t, vec3 b, sampler2D normalMap, vec2 uv)
{
    vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(t), normalize(b), normalize(n));
    return normalize(TBN * tangentNormal);
}

void main()
{
    vec3 N = normalize(Normal);
    if (hasNormalMap) {
        vec2 uvn = applyUV(TexCoords, texture_normal1_uv, texture_normal1_rot);
        N = getNormalFromMap(N, Tangent, Bitangent, texture_normal1, uvn);
    }
    // encode normal into 0..1 for visualization
    vec3 encoded = N * 0.5 + 0.5;
    FragColor = vec4(encoded, 1.0);
}
