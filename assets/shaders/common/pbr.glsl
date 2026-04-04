// pbr.glsl
// Shared PBR microfacet BRDF, IBL irradiance, and LTC area-light functions.
//
// Dependencies (must be included before this file, or via transitive include):
//   frame_uniforms.glsl — u_Frame (irrSH), t_LtcMat, t_LtcAmp, LightEntry
//   common.glsl         — PI, EPSILON
//
// Does NOT declare any vertex/fragment I/O.
// Include this header in any shader that computes lighting.

#ifndef SA_PBR_GLSL
#define SA_PBR_GLSL

#include "frame_uniforms.glsl"
#include "common.glsl"

// ── Fresnel ───────────────────────────────────────────────────────────────────

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Roughness-aware Fresnel for IBL (Lagarde & de Rousiers 2012).
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0)
              * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ── Microfacet BRDF terms ─────────────────────────────────────────────────────

// GGX/Trowbridge-Reitz normal distribution function.
// Accepts precomputed NdotH to avoid recomputing the dot product.
float DistributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + EPSILON);
}

// Schlick-GGX geometry function (single direction).
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + EPSILON);
}

// Smith's method: product of view and light geometry terms.
float GeometrySmith(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX(NdotV, roughness)
         * GeometrySchlickGGX(NdotL, roughness);
}

// ── IBL diffuse irradiance (Spherical Harmonics) ──────────────────────────────
// Evaluates the pre-convolved L0+L1+L2 SH stored in u_Frame.irrSH[9].
// Coefficients are already multiplied by the Lambertian convolution kernel
// (Ramamoorthi & Hanrahan 2001), so evaluation is a simple linear combination.
vec3 EvaluateSHIrradiance(vec3 N) {
    vec3 irr = vec3(0.0);
    irr += u_Frame.irrSH[0].rgb *  0.282095;
    irr += u_Frame.irrSH[1].rgb * (0.488603 * N.y);
    irr += u_Frame.irrSH[2].rgb * (0.488603 * N.z);
    irr += u_Frame.irrSH[3].rgb * (0.488603 * N.x);
    irr += u_Frame.irrSH[4].rgb * (1.092548 * N.x * N.y);
    irr += u_Frame.irrSH[5].rgb * (1.092548 * N.y * N.z);
    irr += u_Frame.irrSH[6].rgb * (0.315392 * (3.0 * N.z * N.z - 1.0));
    irr += u_Frame.irrSH[7].rgb * (1.092548 * N.x * N.z);
    irr += u_Frame.irrSH[8].rgb * (0.546274 * (N.x * N.x - N.y * N.y));
    return max(irr, vec3(0.0));
}

// ── LTC area light (Heitz et al. 2016) ───────────────────────────────────────
// LUT is 64×64; uv = (NdotV, roughness) in [0,1].
const vec2 LTC_LUT_SIZE = vec2(64.0);

// Sample the LTC matrix LUT.  Packed storage: (m00, m02, m11, m20).
mat3 LtcMatrix(vec2 uv) {
    vec4 t = texture(t_LtcMat, uv);
    return mat3(
        t.x,  0.0, t.y,
        0.0,  t.z, 0.0,
        t.w,  0.0, 1.0
    );
}

// Integrate the clamped cosine over a planar polygon (4 vertices) transformed
// into the LTC-warped tangent space.  Returns unshadowed irradiance.
float LTC_Evaluate(vec3 N, vec3 V, vec3 P, mat3 Minv, vec3 points[4]) {
    // Orthonormal basis around N
    vec3 T1 = normalize(V - N * dot(V, N));
    vec3 T2 = cross(N, T1);
    mat3 basis = transpose(mat3(T1, T2, N));

    // Transform and warp polygon vertices
    vec3 L[4];
    for (int j = 0; j < 4; ++j)
        L[j] = normalize(Minv * (basis * (points[j] - P)));

    // Solid-angle form: sum cross-product z-components around boundary
    float sum = 0.0;
    for (int j = 0; j < 4; ++j) {
        vec3 a = L[j];
        vec3 b = L[(j + 1) & 3];
        float theta = acos(clamp(dot(a, b), -1.0, 1.0));
        sum += theta * cross(a, b).z;
    }
    return max(0.0, sum) * INV_PI * 0.5;
}

// Evaluate one area light (LightEntry.type == 3) and return its contribution.
vec3 EvaluateAreaLight(LightEntry light, vec3 P, vec3 N, vec3 V,
                       float roughness, vec3 F0, vec3 albedo, float metallic) {
    float NdotV = max(dot(N, V), 0.0);

    // LUT coordinate — bilinear clamp to avoid edge artefacts
    vec2 uv = clamp(vec2(NdotV, roughness), vec2(0.0), vec2(1.0));
    uv = uv * (LTC_LUT_SIZE - 1.0) / LTC_LUT_SIZE + 0.5 / LTC_LUT_SIZE;

    mat3  Minv    = LtcMatrix(uv);
    vec4  ltcAmp  = texture(t_LtcAmp, uv);
    float ggxNorm = ltcAmp.x;   // GGX specular lobe normalisation
    float fresnel = ltcAmp.y;   // Fresnel term at (NdotV, roughness)

    // Rectangle corners from position, tangents, and half-sizes
    float hw = light.innerAngle * 0.5;   // half width  (innerAngle slot reused)
    float hh = light.outerAngle * 0.5;   // half height (outerAngle slot reused)
    vec3  tu = light.tangentU;
    vec3  tv = light.tangentV;
    vec3  lp = light.position;

    vec3 corners[4];
    corners[0] = lp - tu * hw - tv * hh;
    corners[1] = lp + tu * hw - tv * hh;
    corners[2] = lp + tu * hw + tv * hh;
    corners[3] = lp - tu * hw + tv * hh;

    float specIrr = LTC_Evaluate(N, V, P, Minv,       corners) * ggxNorm;
    float diffIrr = LTC_Evaluate(N, V, P, mat3(1.0),  corners);

    vec3 F  = mix(F0, vec3(1.0), fresnel);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    vec3 radiance = light.color * light.intensity;
    return (kD * albedo * INV_PI * diffIrr + F * specIrr) * radiance;
}

#endif // SA_PBR_GLSL
