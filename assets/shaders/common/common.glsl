// common.glsl
// Shared math constants and utility functions.
// Included by pbr.glsl and any shader that needs these utilities.
//
// Does NOT declare any uniforms or I/O — pure functions only.

#ifndef SA_COMMON_GLSL
#define SA_COMMON_GLSL

// ── Constants ─────────────────────────────────────────────────────────────────

const float PI      = 3.14159265359;
const float INV_PI  = 0.31830988618;
const float EPSILON = 1e-7;

// ── Depth utilities ───────────────────────────────────────────────────────────

// Convert a non-linear depth buffer value to a linear eye-space depth.
// Matches the standard Vulkan/OpenGL reversed-Z convention when near/far are
// passed from the projection matrix.
float LinearizeDepth(float depth, float near, float far) {
    return (2.0 * near * far) / (far + near - (depth * 2.0 - 1.0) * (far - near));
}

// Reconstruct world-space position from a depth sample and the inverse
// view-projection matrix.  uv is in [0,1]; depth in [0,1] (Vulkan NDC).
vec3 ReconstructWorldPos(vec2 uv, float depth, mat4 invViewProj) {
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = invViewProj * ndc;
    return world.xyz / world.w;
}

// ── Octahedral normal encoding ────────────────────────────────────────────────
// Encodes a unit-length vec3 normal into two floats in [-1, 1].
// Used for the G-Buffer RT1 (RG channels).
// Reference: Cigolle et al. "Survey of Efficient Representations for
// Independent Unit Vectors", JCGT 2014.
//
// IMPORTANT: uses signNotZero instead of sign() to avoid sign(0)=0,
// which would corrupt normals whose x or y component is exactly zero
// in the southern hemisphere fold (e.g. poles of a sphere).

float signNotZero(float v) { return v >= 0.0 ? 1.0 : -1.0; }

vec2 OctEncode(vec3 n) {
    // Project onto octahedron surface
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    // Fold lower hemisphere into upper square
    if (n.z < 0.0) {
        float ox = n.x, oy = n.y;
        n.x = (1.0 - abs(oy)) * signNotZero(ox);
        n.y = (1.0 - abs(ox)) * signNotZero(oy);
    }
    return n.xy;
}

vec3 OctDecode(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        float ox = n.x, oy = n.y;
        n.x = (1.0 - abs(oy)) * signNotZero(ox);
        n.y = (1.0 - abs(ox)) * signNotZero(oy);
    }
    return normalize(n);
}

#endif // SA_COMMON_GLSL
