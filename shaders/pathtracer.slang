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

[vk::binding(4, 0)]
Texture2D<float4> hdrImage;

[[vk::push_constant]]
PushConstantsPathtracer pc;

//Tests if the ray hit anything in the scene before it would reach whatever is tMax away in the direction
//Having hit nothing means that we can see the light
inline bool shadowRayCast(float3 origin, float3 dir, float tMax) {
    RayDesc r;
    r.Origin = origin;
    r.Direction = dir;
    r.TMin = EPSILON;
    r.TMax = tMax;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, r);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_NOTHING;
}

[shader("raygeneration")]
void rayGeneration() {
    seed = DispatchRaysIndex().xy * uint2(1973, 9277) + pc.frameNum * uint2(26699, 7919);

    float2 samplePos = (float2)DispatchRaysIndex().xy;
    float2 imageSize = (float2)DispatchRaysDimensions().xy;

    samplePos += float2(rand(), rand()); //Add jitter for anti-aliasing

    RayDesc ray;
    {
        float aspect = imageSize.x / imageSize.y;
        float2 ndc = float2((samplePos.x - imageSize.x / 2.f) / (imageSize.x / 2.f), (imageSize.y / 2.f - samplePos.y) / (imageSize.y / 2.f));
        float3 dir = normalize(pc.camForward + pc.camRight * tan(pc.fov / 2.f) * ndc.x * aspect + pc.camUp * tan(pc.fov / 2.f) * ndc.y);

        float2 lensSample = toDiskConcentric(float2(rand(), rand()));
        //Get the direction of the ray by finding the point on the focus plane, subtracting the point on the lens we're sampling from, and normalizing
        ray.Origin = pc.camPos + pc.camUp * lensSample.x * pc.lensRadius + pc.camRight * lensSample.y * pc.lensRadius;
        ray.Direction = normalize(pc.camPos + dir * pc.focusDist / dot(dir, pc.camForward) - ray.Origin);
    }
    ray.TMin = EPSILON; // Avoid self-intersection
    ray.TMax = INFINITY;

    float3 throughput = float3(1.f);
    float3 finalColor = float3(0.f);
    HitPayload payload = {};
    while (payload.depth < pc.maxBounces) {
        payload.seed = seed;

        //For SER
        HitObject hitObject = HitObject::TraceRay(tlas, 0, 0xFF, 0, 0, 0, ray, payload);
        ReorderThread(hitObject);
        HitObject::Invoke<HitPayload>(tlas, hitObject, payload);       
        
        if (hitObject.IsHit()) {
            if (payload.throughput.a > 0.f) {
                finalColor += throughput * payload.throughput.rgb;
                break;
            }

            throughput *= select(isnan(payload.throughput.rgb), float3(0.f), payload.throughput.rgb);
            ray.Origin = payload.rayOrigin;
            ray.Direction = payload.rayDir;
            ++payload.depth;
        } else {
            finalColor += throughput * payload.throughput.rgb;
            break;
        }

        //TODO: MIS

        seed = payload.seed;
    }

    radianceImage[DispatchRaysIndex().xy] = radianceImage[DispatchRaysIndex().xy] * float(pc.frameNum) / float(pc.frameNum + 1) + 
    float4(finalColor, 1.f) * 1.f / float(pc.frameNum + 1);
}

[shader("closesthit")]
void closestHit(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes isect) {
    seed = payload.seed;

    PointInfo pointInfo;
    bool entering;
    {
        float3 gNor;
        float3 barycentrics = float3(1.f - isect.barycentrics.x - isect.barycentrics.y, isect.barycentrics);
        pointInfo = getPointInfo(InstanceID() + GeometryIndex(), PrimitiveIndex(), barycentrics, pc, gNor);

        entering = dot(ObjectRayDirection(), gNor) < 0.f;
        if (dot(ObjectRayDirection(), gNor) > 0.f) gNor *= -1.f;
        if (dot(gNor, pointInfo.nor) < 0.f) {
            pointInfo.nor *= -1.f;
            pointInfo.tan *= -1.f;
        }
    }

    // TODO: Consider an alternative to my current solution to a material index being 0
    Material material = pc.sceneData->materials[max(pc.sceneData->deviceSubMeshes[InstanceID() + GeometryIndex()].materialIdx, 0)];

    // Emissive
    //We terminate early on emissive materials
    if (length(material.emission.rgb) > 0.f) {
        float3 emissionColor = material.emission.rgb * (material.emissionTexIdx != -1 ? 
            sceneImages[NonUniformResourceIndex(pc.sceneData->textures[material.emissionTexIdx].imageIdx)]
            .SampleLevel(samplers[NonUniformResourceIndex( pc.sceneData->textures[material.emissionTexIdx].samplerIdx + 1)], pointInfo.uv, 0.f) .rgb : 
            float3(1.f));

        payload.throughput = float4(emissionColor * material.emission[3], 1.f);
        return;
    }

    //APPLY MATERIAL MAPS
    // Normal map
    if (material.norTexIdx >= 0) {
        Texture tex = pc.sceneData->textures[material.norTexIdx];
        float3 mNor = sceneImages[NonUniformResourceIndex(tex.imageIdx)]
                          .SampleLevel(samplers[NonUniformResourceIndex(tex.samplerIdx + 1)], pointInfo.uv, 0.f) .rgb;

        applyNormalMap(pointInfo, mNor);
    }
    float3 wo = -normalize(mulNor(pointInfo, mul(WorldToObject3x4(), float4(WorldRayDirection(), 0.f)).xyz));

    //Roughness
    material.roughness *= (material.metallicRoughnessTexIdx != -1 ? 
        sceneImages[NonUniformResourceIndex(pc.sceneData->textures[material.metallicRoughnessTexIdx].imageIdx)]
        .SampleLevel(samplers[NonUniformResourceIndex( pc.sceneData->textures[material.metallicRoughnessTexIdx].samplerIdx + 1)], pointInfo.uv, 0.f)[1] : 
        1.f);
        material.roughness = max(material.roughness, .00001f);

    //Metallic
    material.metallic = material.metallic * (material.metallicRoughnessTexIdx != -1 ? 
        sceneImages[NonUniformResourceIndex( pc.sceneData->textures[material.metallicRoughnessTexIdx].imageIdx)]
        .SampleLevel(samplers[NonUniformResourceIndex( pc.sceneData->textures[material.metallicRoughnessTexIdx].samplerIdx + 1)], pointInfo.uv, 0.f)[2] : 
        1.f);

    // Albedo
    material.color *= (material.colorTexIdx != -1 ? 
        sceneImages[NonUniformResourceIndex(pc.sceneData->textures[material.colorTexIdx].imageIdx)]
        .SampleLevel( samplers[NonUniformResourceIndex(pc.sceneData->textures[material.colorTexIdx].samplerIdx + 1)], pointInfo.uv, 0.f) : 
        float4(1.f));

    float3 wi;
    {
        // TODO: Handle alpha better
        if (material.color.a < .2f) {
            payload.throughput = float4(float3(1.f), 0.f);
            payload.rayDir = WorldRayDirection();
            payload.rayOrigin = WorldRayOrigin() + (RayTCurrent() + EPSILON) * WorldRayDirection();
            payload.seed = seed;
            return;
        }

        float3 bsdf;
        float pdf;
        {
            float2 xi = float2(rand(), rand());
            float rand = rand();
            float3 wh;
            float3 f;
            float etaA = entering ? 1.f : material.ior;
            float etaB = entering ? material.ior : 1.f;
            wi = sampleWiCookTorrance(material.metallic, material.roughness, material.transmissiveness, rand, wo, wh, etaA, etaB, f, xi);
            payload.throughput = float4(bsdfCookTorrance(wo, wh, wi, material.color.rgb, material.metallic, material.roughness, f, material.transmissiveness, etaA, etaB, rand), 0.f);
            payload.throughput /= pdfCookTorrance(wo, wh, wi, material.color.rgb, material.metallic, material.roughness, f, material.transmissiveness, etaA, etaB, rand);
        }
    }

    payload.rayDir = normalize(mul(ObjectToWorld3x4(), float4(mulNorTranspose(pointInfo, wi), 0.f)).xyz);
    payload.rayOrigin = WorldRayOrigin() + WorldRayDirection() * RayTCurrent() + normalize(mul(ObjectToWorld3x4(), float4(mulNorTranspose(pointInfo, float3(0.f, 0.f, 1.f)), 0.f))) * sign(wi.z) * EPSILON;
    payload.seed = seed;
}

[shader("miss")]
void miss(inout HitPayload payload) {
    float2 uv = float2(atan2(WorldRayDirection().z, WorldRayDirection().x), asin(WorldRayDirection().y));
    uv *= float2(1.f / (2.f * PI), -1.f / PI); //v is flipped
    uv += float2(.5f);
    payload.throughput = float4(hdrImage.SampleLevel(samplers[0], uv, 0.f).rgb, 1.f);
    payload.depth = MAX_BOUNCES;
}

[shader("anyhit")]
void anyHit() {

}