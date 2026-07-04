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
// LUT is 64×64; selfshadow fit — uv = (roughness, sqrt(1 - NdotV)) in [0,1].
const vec2 LTC_LUT_SIZE = vec2(64.0);

// Sample the LTC inverse-M matrix LUT.  The four texels (t.x..t.w) map to the
// isotropic Minv exactly as in the selfshadow reference (M[1][1] = 1):
//     | t.x  0   t.z |
//     | 0    1   0   |
//     | t.y  0   t.w |
mat3 LtcMatrix(vec2 uv) {
    vec4 t = texture(t_LtcMat, uv);
    return mat3(
        vec3(t.x, 0.0, t.y),   // column 0
        vec3(0.0, 1.0, 0.0),   // column 1
        vec3(t.z, 0.0, t.w)    // column 2
    );
}

// Edge integral of the clamped-cosine polygon form factor (Heitz 2016).
// The rational approximation folds in the 1/(2*PI) normalisation, so the
// returned vector's z-component is directly the (unclipped) form factor E.
vec3 IntegrateEdgeVec(vec3 v1, vec3 v2) {
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float thetaSinTheta = (x > 0.0)
        ? v
        : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2) * thetaSinTheta;
}

float IntegrateEdge(vec3 v1, vec3 v2) {
    return IntegrateEdgeVec(v1, v2).z;
}

// Clip a quad (in cosine space) against the horizon plane z = 0, producing a
// polygon of n = 0..5 vertices.  This is the accurate variant (Heitz 2016); it
// stays correct when the light straddles / intersects the shading plane, unlike
// the clipless sphere-approximation which shows dark-lens artefacts there.
void ClipQuadToHorizon(inout vec3 L[5], out int n) {
    // detect clipping config
    int config = 0;
    if (L[0].z > 0.0) config += 1;
    if (L[1].z > 0.0) config += 2;
    if (L[2].z > 0.0) config += 4;
    if (L[3].z > 0.0) config += 8;

    n = 0;

    if (config == 0) {
        // clip all
    } else if (config == 1) {
        n = 3;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[3].z * L[0] + L[0].z * L[3];
    } else if (config == 2) {
        n = 3;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    } else if (config == 3) {
        n = 4;
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
        L[3] = -L[3].z * L[0] + L[0].z * L[3];
    } else if (config == 4) {
        n = 3;
        L[0] = -L[3].z * L[2] + L[2].z * L[3];
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
    } else if (config == 5) {
        n = 0; // impossible
    } else if (config == 6) {
        n = 4;
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    } else if (config == 7) {
        n = 5;
        L[4] = -L[3].z * L[0] + L[0].z * L[3];
        L[3] = -L[3].z * L[2] + L[2].z * L[3];
    } else if (config == 8) {
        n = 3;
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
        L[1] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] =  L[3];
    } else if (config == 9) {
        n = 4;
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
        L[2] = -L[2].z * L[3] + L[3].z * L[2];
    } else if (config == 10) {
        n = 0; // impossible
    } else if (config == 11) {
        n = 5;
        L[4] = L[3];
        L[3] = -L[2].z * L[3] + L[3].z * L[2];
        L[2] = -L[2].z * L[1] + L[1].z * L[2];
    } else if (config == 12) {
        n = 4;
        L[1] = -L[1].z * L[2] + L[2].z * L[1];
        L[0] = -L[0].z * L[3] + L[3].z * L[0];
    } else if (config == 13) {
        n = 5;
        L[4] = L[3];
        L[3] = L[2];
        L[2] = -L[1].z * L[2] + L[2].z * L[1];
        L[1] = -L[1].z * L[0] + L[0].z * L[1];
    } else if (config == 14) {
        n = 5;
        L[4] = -L[0].z * L[3] + L[3].z * L[0];
        L[0] = -L[0].z * L[1] + L[1].z * L[0];
    } else if (config == 15) {
        n = 4;
    }

    if (n == 3)
        L[3] = L[0];
    if (n == 4)
        L[4] = L[0];
}

// Integrate the clamped cosine over a planar polygon (4 vertices) transformed
// into the LTC-warped tangent space.  Returns the form factor (already
// normalised via IntegrateEdge — do NOT scale by 1/(2*PI) again downstream).
float LTC_Evaluate(vec3 N, vec3 V, vec3 P, mat3 Minv, vec3 points[4], bool twoSided) {
    // Orthonormal basis around N
    vec3 T1 = normalize(V - N * dot(V, N));
    vec3 T2 = cross(N, T1);
    mat3 basis = transpose(mat3(T1, T2, N));

    // Warp polygon vertices into cosine space (unnormalised — clipping needs .z)
    vec3 L[5];
    L[0] = Minv * (basis * (points[0] - P));
    L[1] = Minv * (basis * (points[1] - P));
    L[2] = Minv * (basis * (points[2] - P));
    L[3] = Minv * (basis * (points[3] - P));
    L[4] = L[0];   // safe init; overwritten by clipping when needed

    // Clip against the horizon (accurate, artefact-free at plane intersections)
    int n;
    ClipQuadToHorizon(L, n);
    if (n == 0)
        return 0.0;

    // Project onto the unit sphere
    L[0] = normalize(L[0]);
    L[1] = normalize(L[1]);
    L[2] = normalize(L[2]);
    L[3] = normalize(L[3]);
    L[4] = normalize(L[4]);

    // Sum the edge integrals around the clipped boundary
    float sum = IntegrateEdge(L[0], L[1])
              + IntegrateEdge(L[1], L[2])
              + IntegrateEdge(L[2], L[3]);
    if (n >= 4) sum += IntegrateEdge(L[3], L[4]);
    if (n == 5) sum += IntegrateEdge(L[4], L[0]);

    return twoSided ? abs(sum) : max(0.0, sum);
}

// Evaluate one area light (LightEntry.type == 3) and return its contribution.
vec3 EvaluateAreaLight(LightEntry light, vec3 P, vec3 N, vec3 V,
                       float roughness, vec3 F0, vec3 albedo, float metallic) {
    float NdotV = clamp(dot(N, V), 0.0, 1.0);

    // LUT coordinate (selfshadow fit): x = roughness, y = sqrt(1 - NdotV).
    // Bias/scale to sample texel centres and avoid edge bleeding.
    vec2 uv = vec2(roughness, sqrt(1.0 - NdotV));
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

    bool  twoSided = light.twoSided > 0.5;
    float specIrr = LTC_Evaluate(N, V, P, Minv,       corners, twoSided) * ggxNorm;
    float diffIrr = LTC_Evaluate(N, V, P, mat3(1.0),  corners, twoSided);

    vec3 F  = mix(F0, vec3(1.0), fresnel);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    vec3 radiance = light.color * light.intensity;
    return (kD * albedo * INV_PI * diffIrr + F * specIrr) * radiance;
}

#endif // SA_PBR_GLSL
