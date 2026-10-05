//Macros
#define MAX_BOUNCES 10
#define EPSILON 0.001f
#define PI 3.1415926535897932384626433832795028841971f
#define INFINITY 0x1.fffffep+127f

#define ENGINE_IMAGES 1
#define MAX_SWAPCHAIN_SIZE 4
#define HDR_IMAGES 1

//Struct definitions
struct Vertex {
	half2 uv;
	half2 nor;
    half2 tan;
    float w; //Handedness of the tangent
};

struct DeviceSubMesh {
    float3 *posBuffer;
    Vertex *vertexBuffer;
    uint32_t *indexBuffer;

    int materialIdx;
    int padding;
};

struct Texture {
    int imageIdx;
    int samplerIdx;
};

struct Material {
    float4 color;
    float4 emission; // Also store emissive strength in here?
    float metallic;
    float roughness;
    float transmissiveness;
    float ior;

    int colorTexIdx;
    int metallicRoughnessTexIdx;
    int norTexIdx;
    int emissionTexIdx;
};

struct PushConstantsPathtracer {
    float3 camPos;
    float3 camForward;
    float3 camRight;
    float3 camUp;

    DeviceSubMesh *deviceSubMeshes;
    Material *materials;
    Texture *textures;
    RaytracingAccelerationStructure *tlas;

    float fov;
    uint32_t frameNum;
    uint32_t maxBounces;

    float lensRadius = 0.f;
    float focusDist = 1.f;
};

struct HitPayload {
    float4 throughput = {};

    //TODO: Add weight for RR?
    float3 rayOrigin = {};
    float3 rayDir = {};

    float ior = 1.f; //ior of the previous material
    int depth = 0;

    uint2 seed = 0;
};

struct PointInfo {
    float3x3 tbn = {};
    float3 nor = {};
    float2 uv = {};
};

struct AliasTableEntry {
    float threshold;
    uint32_t alias;
};

//Helper functions
float3 fromOctahedral(half2 u) {
    if (u.x == -2.h && u.y == -2.h) return float3(0);

    float3 v = float3(u.x, u.y, 1.f - abs(u.x) - abs(u.y));

    float2 signNotZeroxy = sign(v.xy);
    signNotZeroxy.x = signNotZeroxy.x == 0.f ? 1.f : signNotZeroxy.x;
    signNotZeroxy.y = signNotZeroxy.y == 0.f ? 1.f : signNotZeroxy.y;

    if (v.z < 0) v.xy = (float2(1.f) - abs(v.yx)) * signNotZeroxy;

    return normalize(v);
}

//From ShaderToy https://www.shadertoy.com/view/4tXyWN
static uint2 seed;
float rand() {
    seed += uint2(1);
    uint2 q = 1103515245U * ((seed >> 1U) ^ (seed.yx));
    uint n = 1103515245U * ((q.x) ^ (q.y >> 3U));
    return float(n) * (1.0 / float(0xffffffffU));
}

inline PointInfo getPointInfo(uint subMeshIdx, uint primitiveIdx, float3 barycentrics, PushConstantsPathtracer pc) {
    float3 nor;
    {
        float3 *posBuffer = pc.deviceSubMeshes[subMeshIdx].posBuffer;
        float3 p0 = posBuffer[pc.deviceSubMeshes[subMeshIdx].indexBuffer[primitiveIdx * 3]];
        float3 p1 = posBuffer[pc.deviceSubMeshes[subMeshIdx].indexBuffer[primitiveIdx * 3 + 1]];
        float3 p2 = posBuffer[pc.deviceSubMeshes[subMeshIdx].indexBuffer[primitiveIdx * 3 + 2]];
        nor = normalize(cross(p1 - p0, p2 - p0));
    }

    Vertex *vertices = pc.deviceSubMeshes[subMeshIdx].vertexBuffer;

    float3 tangent = float3(0.f);

    PointInfo res = {};
    [ForceUnroll] //Loop unrolling, yay!
    for (int i = 0; i < 3; ++i) {
        Vertex v = vertices[pc.deviceSubMeshes[subMeshIdx].indexBuffer[primitiveIdx * 3 + i]];

        res.nor += barycentrics[i] * fromOctahedral(v.nor);
        res.uv += barycentrics[i] * float2(v.uv.x, v.uv.y);
        tangent += barycentrics[i] * fromOctahedral(v.tan);
    }

    res.nor = normalize(res.nor);

    if (length(tangent) == 0.f) { //When we have to compute the tangent
        tangent = abs(res.nor.x) > abs(res.nor.y) ? float3(-res.nor.z, 0.f, res.nor.x) / sqrt(res.nor.x * res.nor.x + res.nor.z * res.nor.z) :
            float3(0.f, res.nor.z, -res.nor.y) / sqrt(res.nor.y * res.nor.y + res.nor.z * res.nor.z);
    }
    tangent = normalize(tangent - res.nor * dot(res.nor, tangent));
    res.tbn = float3x3(tangent, cross(res.nor, tangent), res.nor); // TODO: might have to change this for left-handed spaces
    res.nor = nor;
    return res;
}

inline float2 toDiskUniform(float2 xi) {
    float r = sqrt(xi.x);
    float theta = 2.f * PI * xi.y;

    return float2(r * cos(theta), r * sin(theta));
}

inline float2 toDiskConcentric(float2 xi) {
    float2 uOffset = 2.f * xi - float2(1.f);

    if (uOffset.x == 0 && uOffset.y == 0) return float2(0.f);

    float r = (abs(uOffset.x) > abs(uOffset.y)) ? uOffset.x : uOffset.y;
    float theta = (abs(uOffset.x) > abs(uOffset.y)) ? PI * uOffset.y / (uOffset.x * 4.f) : PI / 2.f - PI * uOffset.x / (uOffset.y * 4.f);

    return float2(r * cos(theta), r * sin(theta));
}

inline float3 toHemisphereCosineWeighted(float2 xi) {
    float2 diskSample = toDiskConcentric(xi);

    return float3(diskSample.x, diskSample.y, sqrt(1.f - diskSample.x * diskSample.x - diskSample.y * diskSample.y));
}

inline float3 toHemisphereUniform(float2 xi) {
    return float3(cos(2.f * PI * xi.y) * sqrt(1.f - xi.x * xi.x), sin(2.f * PI * xi.y) * sqrt(1.f - xi.x * xi.x), xi.x);
}



inline float3x3 applyNormalMap(float3x3 tbn, float3 mNor) {
    mNor = normalize(mNor * 2.f - 1.f);

    float3 nNor = normalize(mul(mNor, tbn));
    float3 nTan = normalize(tbn[0] - nNor * dot(tbn[0], nNor));
    float3 nBit = cross(nTan, nNor);

    return float3x3(nTan, nBit, nNor);
}