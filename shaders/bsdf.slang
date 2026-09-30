#include "util.slang"

inline float cos2Theta(float3 v) { return v.z * v.z; }

inline float sin2Theta(float3 v) { return 1.f - cos2Theta(v); }

inline float tan2Theta(float3 v) { return (cos2Theta(v) == 0.f) ? INFINITY : sin2Theta(v) / cos2Theta(v); }

//Note that wo here must be aligned with the normal

//TODO: Add anisotropy?
//Adapted from PBRT 4e
inline float3 sampleWh(float roughness, float3 wo, float2 xi) {
    if (roughness == 0.f) return float3(0.f, 0.f, 1.f);

    //Move our wo from the squashed space of the brdf lobe to the hemisphere space
    float alpha = roughness * roughness;
    float3 w = normalize(float3(alpha * wo.x, alpha * wo.y, wo.z));

    float3 t1 = (w.z < 1.f - EPSILON) ? normalize(cross(float3(0.f, 0.f, 1.f), w)) : float3(1.f, 0.f, 0.f);
    float3 t2 = cross(w, t1);

    float2 p = toDiskUniform(xi);

    float h = sqrt(1.f - p.x * p.x);
    p.y = lerp(h, p.y, (1.f + w.z) / 2.f);

    float pz = sqrt(max(0.f, 1.f - dot(p, p)));
    float3 nh = p.x * t1 + p.y * t2 + pz * w;
    return normalize(float3(alpha * nh.x, alpha * nh.y, max(EPSILON, nh.z)));
}


//f0 is the Fresnel reflectance when viewed parallel to the normal
inline float3 brdfTorranceSparrow(float3 wo, float3 wi, float3 f0, float roughness) {
    //Perfectly specular case
    if (roughness == 0.f) return f0;

    if (wo.z < 0.f) return float3(0.f);
    if (wi.z < 0.f) return float3(0.f);

    float3 wh = normalize(wo + wi);

    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;

    float d;
    {
        float tan2Theta = tan2Theta(wh);
        if (tan2Theta == INFINITY) return 0.f;

        float cos4Theta = cos2Theta(wh) * cos2Theta(wh);

        //Isotropic right now
        float e = (cos2Theta(wh) / alpha2 + sin2Theta(wh) / alpha2) * tan2Theta;

        d = 1.f / (PI * alpha2 * cos4Theta * (1 + e) * (1 + e));
    }

    float g;
    {
        //Clamp cosines away from 0 so it doesn't blow up at grazing angles
        float cosWo = max(abs(wo.z), EPSILON);
        float cosWi = max(abs(wi.z), EPSILON);

        float tan2Wo = (1.f - cosWo * cosWo) / (cosWo * cosWo);
        float lambdaWo = .5f * (-1.f + sqrt(1.f + alpha2 * tan2Wo));

        float tan2Wi = (1.f - cosWi * cosWi) / (cosWi * cosWi);
        float lambdaWi = .5f * (-1.f + sqrt(1.f + alpha2 * tan2Wi));

        g = 1.f / (1.f + lambdaWo + lambdaWi);
    }

    float dotWoWh = dot(wo, wh);

    //Shlick approximation of Fresnel
    float3 f = f0 + (float3(1.f) - f0) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh);

    float cosThetaO = wo.z;
    float cosThetaI = wi.z;

    //Clamp the denominator cosines away from 0 to avoid blow out at grazing angles
    float denom = 4.f * max(cosThetaO, EPSILON) * max(cosThetaI, EPSILON);

    return d * g * f * max(wi.z, 0.f) / denom;
}

inline float pdfTorranceSparrow(float3 wo, float3 wh, float roughness) {
    if (roughness == 0.f) return 1.f; //Dirac delta distribution

    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;

    float d;
    {
        float tan2Theta = tan2Theta(wh);
        if (tan2Theta == INFINITY) return 0.f;

        float cos4Theta = cos2Theta(wh) * cos2Theta(wh);

        //Isotropic right now
        float e = (cos2Theta(wh) / alpha2 + sin2Theta(wh) / alpha2) * tan2Theta;

        d = 1.f / (PI * alpha2 * cos4Theta * (1 + e) * (1 + e));
    }

    float g1;
    {
        float cosWo = max(abs(wo.z), EPSILON);

        float tan2Wo = (1.f - cosWo * cosWo) / (cosWo * cosWo);
        float lambdaWo = .5f * (-1.f + sqrt(1.f + alpha2 * tan2Wo));

        g1 = 1.f / (1.f + lambdaWo);
    }

    return max(d * g1 / (4.f * max(abs(wo.z), EPSILON)), EPSILON);
}

inline float3 brdfCookTorrance(float3 wo, float3 wi, float3 albedo, float metallic, float roughness, float rand) {
    float3 wh = normalize(wo + wi);

    float dotWoWh = dot(wo, wh);

    float3 f0 = (1.f - metallic) * float3(.04f) + metallic * albedo;
    float3 f = f0 + (float3(1) - f0) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh);

    float3 ks = f;
    float3 kd = (1.f - metallic) * (float3(1.f) - ks);
    return brdfTorranceSparrow(wo, wi, f0, roughness) + kd * albedo * max(wi.z, 0.f) / PI;
}

inline float pdfCookTorrance(float3 wo, float3 wi, float metallic, float roughness, float rand) {
    float prGlossy = .4f + .6f * metallic;

    float3 wh = normalize(wo + wi);

    return prGlossy * pdfTorranceSparrow(wo, wh, roughness) + (1.f - prGlossy) * (wi.z / PI);
}