//Holy shit, I think I love slang
#include "bsdf.slang"

//Inputs
[vk::binding(0, 0)]
RWTexture2D<float4> radianceImage;

[vk::binding(1, 0)] //TODO: Change this if you change the max number of swap chain images
Texture2D<float4> sceneImages[];

[vk::binding(2, 0)]
SamplerState samplers[];

[vk::binding(3, 0)]
RaytracingAccelerationStructure tlas;

[[vk::push_constant]]
PushConstantsPathtracer pc;

[shader("raygeneration")]
void rayGeneration() {
    seed = DispatchRaysIndex().xy * uint2(1973, 9277) + pc.frameNum * uint2(26699, 7919);

    float2 samplePos = (float2)DispatchRaysIndex().xy;
    float2 imageSize = (float2)DispatchRaysDimensions().xy;

    samplePos += float2(rand(), rand()); //Add jitter for anti-aliasing

    float2 screen = float2((samplePos.x - imageSize.x / 2.f) / (imageSize.x / 2.f), (imageSize.y / 2.f - samplePos.y) / (imageSize.y / 2.f));

    RayDesc ray;
    ray.Origin = pc.camPos;
    ray.Direction = normalize(pc.camForward * 1.f / tan(pc.fov / 2.f) + screen.x * (imageSize.x / imageSize.y) * pc.camRight + screen.y * pc.camUp);
    ray.TMin = EPSILON; // Avoid self-intersection
    ray.TMax = INFINITY;

    float3 throughput = float3(1.f);
    float3 finalColor = float3(0.f);
    HitPayload payload = {};
    while (payload.depth < MAX_BOUNCES) {
        // For shader execution reordering
        
        payload.seed = seed;

        HitObject hitObject = HitObject::TraceRay(tlas, 0, 0xFF, 0, 0, 0, ray, payload);
        ReorderThread(hitObject);
        HitObject::Invoke<HitPayload>(tlas, hitObject, payload);       
        
        if (hitObject.IsHit()) {
            if (length(payload.emissiveness) > 0.f) {
                finalColor += throughput * payload.emissiveness;
                break;
            }

            throughput *= select(isnan(payload.throughput.rgb), float3(0.f), payload.throughput.rgb);
            ray.Origin = payload.rayOrigin;
            ray.Direction = payload.rayDir;
            ++payload.depth;
        } else {
            finalColor += throughput * payload.emissiveness;
            break;
        }
    }

    radianceImage[DispatchRaysIndex().xy] = radianceImage[DispatchRaysIndex().xy] * float(pc.frameNum) / float(pc.frameNum + 1) + 
    float4(finalColor, 1.f) * 1.f / float(pc.frameNum + 1);

}

[shader("closesthit")]
void closestHit(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes isect) {
    seed = payload.seed;

    uint subMeshIdx = InstanceID() + GeometryIndex(); //See loader.cpp, line 500
    uint triIdx = PrimitiveIndex();
    float t = RayTCurrent();

    float3 barycentrics = float3(1.f - isect.barycentrics.x - isect.barycentrics.y, isect.barycentrics);

    PointInfo pointInfo = getPointInfo(subMeshIdx, triIdx, barycentrics, pc);

    //TODO: Consider an alternative to my current solution to a material index being 0
    Material material = pc.materials[max(pc.deviceSubMeshes[subMeshIdx].materialIdx, 0)];

    if (length(material.emission.rgb) > 0) {
        float3 emissionColor = material.emission.xyz * (material.emissionTexIdx != -1 ? 
        sceneImages[NonUniformResourceIndex(pc.textures[material.emissionTexIdx].imageIdx)]
                       .SampleLevel(samplers[NonUniformResourceIndex( pc.textures[material.emissionTexIdx].samplerIdx + 1)], pointInfo.uv, 0.f)[1] : 
        float3(1.f));

        payload.emissiveness = emissionColor * material.emission[3];
        return;
    }

    float3 wo = -normalize(mul(pointInfo.orientation, mul(WorldToObject3x4(), float4(WorldRayDirection(), 0.f)).xyz));
    float roughness;
    {
        roughness = material.roughness * (material.metallicRoughnessTexIdx != -1 ? 
        sceneImages[NonUniformResourceIndex(pc.textures[material.metallicRoughnessTexIdx].imageIdx)]
                               .SampleLevel(samplers[NonUniformResourceIndex( pc.textures[material.metallicRoughnessTexIdx].samplerIdx + 1)], pointInfo.uv, 0.f)[1] : 
        1.f);
    }
    float3 wh = sampleWh(roughness, wo, float2(rand(), rand()));
    float3 wi = reflect(-wo, wh);

    //TODO: Implement the BSDF for transmissive materials, and Cook-Torrance rather than just Torrance-Sparrow
    {
        float4 albedo = material.color * (material.colorTexIdx != -1 ? 
        sceneImages[NonUniformResourceIndex(pc.textures[material.colorTexIdx].imageIdx)]
        .SampleLevel( samplers[NonUniformResourceIndex(pc.textures[material.colorTexIdx].samplerIdx + 1)], pointInfo.uv, 0.f) : 
        float4(1.f));

        payload.throughput = float4(brdfTorranceSparrow(wo, wi, albedo.xyz, roughness)/ pdfTorranceSparrow(wo, wh, roughness), 1.f) * max(0.f, wi.z);
    }

    payload.rayDir = normalize(mul(ObjectToWorld3x4(), float4(mul(transpose(pointInfo.orientation), wi), 0.f)).xyz);
    payload.rayOrigin = WorldRayOrigin() + WorldRayDirection() * RayTCurrent() + normalize(mul(ObjectToWorld3x4(), float4(mul(transpose(pointInfo.orientation), float3(0.f, 0.f, 1.f)), 0.f))) * EPSILON;
}

[shader("miss")]
void miss(inout HitPayload payload) {
    payload.emissiveness = float3(0.f);
    payload.depth = MAX_BOUNCES;
}

[shader("anyhit")]
void anyHit() {

}