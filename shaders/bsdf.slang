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

inline float d(float3 wh, float alpha2) {

    float tan2Theta = tan2Theta(wh);
    if (tan2Theta == INFINITY) return 0.f;

    float cos4Theta = cos2Theta(wh) * cos2Theta(wh);

    // Isotropic right now
    float e = (cos2Theta(wh) / alpha2 + sin2Theta(wh) / alpha2) * tan2Theta;

    return 1.f / (PI * alpha2 * cos4Theta * (1 + e) * (1 + e));
}

inline float lambda(float x, float alpha2) {

    return .5f * (-1.f + sqrt(1.f + alpha2 * x));
}

inline float g(float3 wo, float3 wi, float alpha2) {
    // Clamp cosines away from 0 so it doesn't blow up at grazing angles
    float cosWo = max(abs(wo.z), EPSILON);
    float cosWi = max(abs(wi.z), EPSILON);

    float tan2Wo = (1.f - cosWo * cosWo) / (cosWo * cosWo);
    float lambdaWo = .5f * (-1.f + sqrt(1.f + alpha2 * tan2Wo));

    float tan2Wi = (1.f - cosWi * cosWi) / (cosWi * cosWi);
    float lambdaWi = .5f * (-1.f + sqrt(1.f + alpha2 * tan2Wi));

    return 1.f / (1.f + lambdaWo + lambdaWi);
}

// Schlick approximation of Fresnel
inline float3 f(float3 f0, float3 wo, float3 wh) {
    float dotWoWh = max(dot(wo, wh), 0.f);
    return f0 + (float3(1.f) - f0) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh) * (1.f - dotWoWh);
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

    float d = d(wh, alpha2);

    float g = g(wo, wi, alpha2);

    float dotWoWh = dot(wo, wh);

    // Shlick approximation of Fresnel
    float3 f = f(f0, wo, wh);

    float cosThetaO = wo.z;
    float cosThetaI = wi.z;

    //Clamp the denominator cosines away from 0 to avoid blow out at grazing angles
    float denom = 4.f * max(cosThetaO, EPSILON) * max(cosThetaI, EPSILON);

    return d * g * f * max(wi.z, 0.f) / denom;
}

inline float g1(float3 wo, float alpha2) {
    float cosWo = max(abs(wo.z), EPSILON);

    float tan2Wo = (1.f - cosWo * cosWo) / (cosWo * cosWo);
    float lambdaWo = .5f * (-1.f + sqrt(1.f + alpha2 * tan2Wo));

    return 1.f / (1.f + lambdaWo);
}

inline float pdfTorranceSparrow(float3 wo, float3 wh, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;

    float d = d(wh, alpha2);

    float g1 = g1(wo, alpha2);

    return max(d * g1 / (4.f * max(abs(wo.z), EPSILON)), EPSILON);
}

inline float3 fDielectric(float3 wo, float3 wh, float etaA, float etaB) {
    float cosThetaI = clamp(dot(wo, wh), -1.f, 1.f);
    float eta = etaA / etaB;

    float sin2ThetaI = max(float(0), float(1 - cosThetaI * cosThetaI));
    float sin2ThetaT = eta * eta * sin2ThetaI;

    if (sin2ThetaT >= 1) return float3(1.f);

    float cosThetaT = sqrt(1.f - sin2ThetaT);

    float rParallel = ((etaB * cosThetaI) - (etaA * cosThetaT)) /
                      ((etaB * cosThetaI) + (etaA * cosThetaT));
    float rPerpendicular = ((etaA * cosThetaI) - (etaB * cosThetaT)) /
                      ((etaA * cosThetaI) + (etaB * cosThetaT));
    float fR = (rParallel * rParallel + rPerpendicular * rPerpendicular) / 2.f;

    return float3(fR);
}

inline float3 sampleWiCookTorrance(float metallic, float roughness, float transmissiveness, float r, float3 wo, inout float3 wh, 
float etaA, float etaB, inout float3 f, float2 xi) {
    float pr = 1.f;

    wh = sampleWh(roughness, wo, xi);
    f = fDielectric(wo, wh, etaA, etaB);

    if (r < 1.f - metallic) { // Dielectric
        pr *= 1.f - metallic;

        if (r < f.x * pr) { // Specular reflection
            return reflect(-wo, wh);
        } else { // Transmission or albedo
            r -= f.x * pr;
            pr *= (1.f - f.x);
            if (r < transmissiveness * pr) { // Specular BTDF
                return refract(-wo, wh, etaA / etaB);
            } else { // Diffuse BRDF
                return toHemisphereCosineWeighted(float2(rand(), rand()));
            }
        }
    } else { // Sample the metallic lobe
        return reflect(-wo, wh);
    }
}

//Based on the Khronos group's glTF spec: https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md
//Currently this probably would cause a good amount of thread divergence, and variance too.
inline float3 bsdfCookTorrance(float3 wo, float3 wh, float3 wi, float3 albedo, float metallic, 
float roughness, float3 f, float transmissiveness, inout float etaA, float etaB, float rand) {
    float pr = 1.f;
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;

    float3 f = fDielectric(wo, wh, etaA, etaB);

    if (rand < 1.f - metallic) { //Dielectric
        pr *= 1.f - metallic;

        if (rand < f.x * pr) { // Specular reflection
            pr *= f.x;
            return d(wh, alpha2) * g(wo, wi, alpha2) / abs(4.f * wo.z);
        } else {
            rand -= f.x * pr;
            pr *= (1.f - f.x);
            if (rand < transmissiveness * pr) { // Specular BTDF
                float denom = dot(wi, wh) + dot(wo, wh) * etaA / etaB;
                denom *= denom * max(wo.z, EPSILON) * etaB / etaA * etaB / etaA;

                return albedo * d(wh, alpha2) * g(wo, wi, alpha2) * abs(dot(wo, wh) * dot(wi, wh) / (denom));
            } else { // Diffuse BRDF
                return albedo * max(wi.z, 0.f) / PI;
            }
        }
    } else { // Sample the metallic lobe
        pr *= metallic;
        float3 f0 = (1.f - metallic) * float3(.04f) + metallic * albedo;
        return brdfTorranceSparrow(wo, wi, albedo, roughness);
    }
}

inline float pdfCookTorrance(float3 wo, float3 wh, float3 wi, float3 albedo, float metallic, 
float roughness, float3 f, float transmissiveness, float etaA, float etaB, float rand) {
    float pr = 1.f;

    if (rand < 1.f - metallic) { // Dielectric
        pr *= 1.f - metallic;

        if (rand < f.x * pr) { // Specular reflection
            return pdfTorranceSparrow(wo, wh, roughness);
        } else {
            rand -= f.x * pr;
            pr *= (1.f - f.x);

            if (rand < transmissiveness * pr) { // Specular BTDF
                float s = dot(wi, wh) + dot(wo, wh) * etaA / etaB;
                return pdfTorranceSparrow(wo, wh, roughness) * 4.f * abs(dot(wo, wh)) * abs(dot(wi, wh)) / (s * s);
            } else { // Diffuse BRDF
                return wi.z / PI;
            }
        }
    } else { // Sample the metallic lobe
        return pdfTorranceSparrow(wo, wh, roughness);
    }
}