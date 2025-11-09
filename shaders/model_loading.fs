#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 FragPos;
in vec3 Normal;
in vec3 Tangent;
in vec3 Bitangent;

uniform vec3 viewPos;

uniform vec4 baseColorFactor;

uniform bool hasBaseColor;
uniform bool hasNormalMap;
uniform bool hasMetallicRoughness;

uniform sampler2D texture_diffuse1;
uniform vec4 texture_diffuse1_uv; // offset.x, offset.y, scale.x, scale.y
uniform float texture_diffuse1_rot;

uniform sampler2D texture_normal1;
uniform vec4 texture_normal1_uv;
uniform float texture_normal1_rot;

uniform sampler2D texture_metallicRoughness1;
uniform vec4 texture_metallicRoughness1_uv;
uniform float texture_metallicRoughness1_rot;

// IBL
uniform samplerCube irradianceMap;
uniform samplerCube prefilteredMap;
uniform sampler2D brdfLUT;
uniform float prefilterMaxMip; // maximum mip level for prefiltered env map

// extra factors provided by CPU
uniform float metallicFactor;
uniform float roughnessFactor;

// apply a simple UV transform: offset + R(scale * uv)
vec2 applyUV(vec2 uv, vec4 uvparams, float rot)
{
    vec2 offset = uvparams.xy;
    vec2 scale = uvparams.zw;
    float c = cos(rot);
    float s = sin(rot);
    mat2 R = mat2(c, -s, s, c);
    return offset + (R * (uv * scale));
}

// helper: normal map unpack and TBN
vec3 getNormalFromMap(vec3 n, vec3 t, vec3 b, sampler2D normalMap, vec2 uv)
{
    vec3 tangentNormal = texture(normalMap, uv).xyz * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(t), normalize(b), normalize(n));
    return normalize(TBN * tangentNormal);
}

// GGX / Cook-Torrance functions
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265 * denom * denom;
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// sample prefiltered env map with roughness using LOD
vec3 PrefilteredEnvRadiance(vec3 R, float roughness)
{
    // sample using roughness * maxMip
    float lod = roughness * prefilterMaxMip;
    return textureLod(prefilteredMap, R, lod).rgb;
}

void main()
{
    vec3 N = normalize(Normal);
    vec3 V = normalize(viewPos - FragPos);

    // base color
    vec2 baseUV = TexCoords;
    if (hasBaseColor)
        baseUV = applyUV(TexCoords, texture_diffuse1_uv, texture_diffuse1_rot);
    vec4 baseSample = vec4(1.0);
    if (hasBaseColor)
        baseSample = texture(texture_diffuse1, baseUV);
    vec3 baseColor = baseSample.rgb * baseColorFactor.rgb;
    float alpha = baseSample.a * baseColorFactor.a;

    // normal map
    if (hasNormalMap)
    {
        vec2 nUV = applyUV(TexCoords, texture_normal1_uv, texture_normal1_rot);
        N = getNormalFromMap(N, Tangent, Bitangent, texture_normal1, nUV);
    }

    // metallic/roughness
    float metallic = metallicFactor;
    float roughness = roughnessFactor;
    if (hasMetallicRoughness)
    {
        vec2 mrUV = applyUV(TexCoords, texture_metallicRoughness1_uv, texture_metallicRoughness1_rot);
        vec4 mrSample = texture(texture_metallicRoughness1, mrUV);
        // glTF convention: R = occlusion or unspecified, G = roughness, B = metallic
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    roughness = clamp(roughness, 0.05, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    // reflectance at normal incidence for dielectrics is 0.04; for metals use baseColor
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, baseColor, metallic);

    // direct lighting: sample a single directional light for simple direct specular
    vec3 L = normalize(vec3(-0.2, -1.0, -0.3));
    vec3 H = normalize(V + L);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    vec3 numerator = NDF * G * F;
    float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.001;
    vec3 specular = numerator / denom;
    vec3 Lo = (kD * baseColor / 3.14159265 + specular) * NdotL;

    // IBL: diffuse irradiance + specular prefiltered
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * baseColor;
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = PrefilteredEnvRadiance(R, roughness);
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuseIBL + specularIBL);

    vec3 color = ambient + Lo;

    // tone mapping / gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    if (alpha < 0.01)
        discard;

    FragColor = vec4(color, alpha);
}
