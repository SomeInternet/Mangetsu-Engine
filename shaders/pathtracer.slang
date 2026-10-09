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
    for (uint i = 0; i < pc.maxBounces; ++i) {
        payload.seed = seed;

        //For SER
        HitObject hitObject = HitObject::TraceRay(tlas, 0, 0xFF, 0, 0, 0, ray, payload);
        ReorderThread(hitObject);
        HitObject::Invoke<HitPayload>(tlas, hitObject, payload);       
        
        if (hitObject.IsHit()) {
            if (payload.throughput.b < 0.f) {
                payload.throughput.b *= -1.f;
                finalColor += throughput * payload.throughput.rgb;
                break;
            }

            throughput *= select(isnan(payload.throughput.rgb), float3(0.f), payload.throughput.rgb);
            ray.Origin = payload.rayOrigin;
            ray.Direction = payload.rayDir;
        } else {
            finalColor += throughput * payload.throughput.rgb;
            finalColor.b *= -1.f;
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
    // TODO: Drop the image and sampler indices once I'm done with them?
    Material material = pc.sceneData->materials[max(pc.sceneData->deviceSubMeshes[InstanceID() + GeometryIndex()].materialIdx, 0)];

    // Emissive
    //We terminate early on emissive materials
    if (length(material.emission.rgb) > 0.f) {
        float3 emissionColor = material.emission.rgb * (material.emissionImgIdx != -1 ? 
            sceneImages[NonUniformResourceIndex(material.emissionImgIdx)]
            .SampleLevel(samplers[NonUniformResourceIndex( material.emissionSamplerIdx + 1)], pointInfo.uv, 0.f) .rgb : 
            float3(1.f));

        payload.throughput = emissionColor * material.emission[3], 1.f;
        payload.throughput.b = min(-payload.throughput.b, -EPSILON);
        return;
    }

    //APPLY MATERIAL MAPS
    // Normal map
    if (material.norImgIdx >= 0) {
        float3 mNor = sceneImages[NonUniformResourceIndex(material.norImgIdx)]
                          .SampleLevel(samplers[NonUniformResourceIndex(material.norSamplerIdx + 1)], pointInfo.uv, 0.f) .rgb;

        applyNormalMap(pointInfo, mNor);
    }
    float3 wo = -normalize(mulNor(pointInfo, mul(WorldToObject3x4(), float4(WorldRayDirection(), 0.f)).xyz));

    // Metallic-Roughness
    material.metallicRoughness = material.metallicRoughness * (material.metallicRoughnessImgIdx != -1 ? 
        sceneImages[NonUniformResourceIndex(material.metallicRoughnessImgIdx)]
        .SampleLevel( samplers[NonUniformResourceIndex(material.metallicRoughnessSamplerIdx + 1)], pointInfo.uv, 0.f).bg : 
        float2(1.f));
    
    // Using .y instead of .g because the Slang syntax highlighting extension behaves really funky... :(
    material.metallicRoughness.y = max(material.metallicRoughness.y, EPSILON);

    // Albedo
    material.color *= (material.colorImgIdx != -1 ? 
        sceneImages[NonUniformResourceIndex(material.colorImgIdx)]
        .SampleLevel( samplers[NonUniformResourceIndex(material.colorSamplerIdx + 1)], pointInfo.uv, 0.f) : 
        float4(1.f));

    float3 wi;
    {
        // TODO: Handle alpha better
        if (material.color.a < .2f) {
            payload.throughput = float3(1.f);
            payload.rayDir = WorldRayDirection();
            payload.rayOrigin = WorldRayOrigin() + (RayTCurrent() + EPSILON) * WorldRayDirection();
            payload.seed = seed;
            return;
        }

        float3 bsdf;
        float pdf;
        //TODO: Optimize
        {
            float2 xi = float2(rand(), rand());
            float rand = rand();
            float3 wh;
            float3 f;
            float etaA = entering ? 1.f : material.ior;
            float etaB = entering ? material.ior : 1.f;
            wi = sampleWiCookTorrance(material.metallicRoughness.x, material.metallicRoughness.y, material.transmissiveness, rand, wo, wh, etaA, etaB, f, xi);
            payload.throughput = bsdfCookTorrance(wo, wh, wi, material.color.rgb, material.metallicRoughness.x, material.metallicRoughness.y, 
                f, material.transmissiveness, etaA, etaB, rand);
            payload.throughput /= pdfCookTorrance(wo, wh, wi, material.color.rgb, material.metallicRoughness.x, material.metallicRoughness.y, 
                f, material.transmissiveness, etaA, etaB, rand);
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
    payload.throughput = hdrImage.SampleLevel(samplers[0], uv, 0.f).rgb;
    payload.throughput.b = min(-payload.throughput.b, -EPSILON);
}

[shader("anyhit")]
void anyHit() {

}