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

// Returns false if the sample is invalid (failed refraction or wrong hemisphere for the chosen lobe)
inline bool sampleWi(float3 wo, PBR pbr, float r, float2 xi, out float3 wi) {
    float m = pbr.metallicRoughness.x;
    float fn = fDielectric(wo, float3(0, 0, 1), pbr.etaA, pbr.etaB).x;
    float pDiffuse = (1.f - m) * (1.f - pbr.transmissiveness) * (1.f - fn);

    if (r < pDiffuse) {
        wi = toHemisphereCosineWeighted(xi);
        return wi.z > 0.f;
    }

    float3 wh = sampleWh(pbr.metallicRoughness.y, wo, xi);
    if (r < pDiffuse + m) { // Metallic
        wi = reflect(-wo, wh);
        return wi.z > 0.f;
    }

    r = (r - pDiffuse - m) / max(1.f - pDiffuse - m, EPSILON);
    float R = fDielectric(wo, wh, pbr.etaA, pbr.etaB).x;
    float T = pbr.transmissiveness * (1.f - R);
    if (r * (R + T) < R) { // Specular reflection
        wi = reflect(-wo, wh);
        return wi.z > 0.f;
    }

    wi = refract(-wo, wh, pbr.etaA / pbr.etaB);
    return wi.z < 0.f;
}

//Based on the Khronos group's glTF spec: https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_transmission/README.md
//Currently this probably would cause a good amount of thread divergence, and variance too.
inline void evalCookTorrance(float3 wo, float3 wi, PBR pbr, out float3 bsdf, out float pdf) {
    if (wo.z < 0.f) {
        pdf = 0.f;
        bsdf = float3(0.f);
        return;
    }

    float alpha2 = pbr.metallicRoughness.y;
    alpha2 *= alpha2 * alpha2 * alpha2;

    float cosThetaO = max(wo.z, EPSILON);
    float cosThetaI = max(wi.z, EPSILON);

    float m = pbr.metallicRoughness.x;
    float fn = fDielectric(wo, float3(0, 0, 1), pbr.etaA, pbr.etaB).x;
    float pDiffuse = (1.f - m) * (1.f - pbr.transmissiveness) * (1.f - fn);
    float pDielectric = 1.f - m - pDiffuse;

    if (wi.z < 0) { // Transmissive
        float3 wh = normalize(pbr.etaA * wo + pbr.etaB * wi);
        if (wh.z < 0.f) wh = -wh;
        if (dot(wh, wi) >= 0.f || dot(wo, wh) < 0.f || pbr.etaA == pbr.etaB) {
            bsdf = float3(0.f);
            pdf = 0.f;
            return;
        }

        float fd = fDielectric(wo, wh, pbr.etaA, pbr.etaB).x;
        float prTrans = (1.f - m) * (1.f - fd) * pbr.transmissiveness;

        float R = fd;
        float T = pbr.transmissiveness * (1.f - R);

        float absDotWoWh = abs(dot(wo, wh));
        float absDotWiWh = abs(dot(wi, wh));

        float d = d(wh, alpha2);
        float g = g(wo, wi, alpha2);
        float s = -absDotWiWh + absDotWoWh * pbr.etaA / pbr.etaB;

        pdf = pDielectric * (R + T > 0.f ? T / (R + T) : 0.f) *
              d * g1(wo, alpha2) * absDotWoWh * absDotWiWh / (cosThetaO * s * s);

        float denom = s * s * cosThetaO * pbr.etaB / pbr.etaA * pbr.etaB / pbr.etaA;

        bsdf = prTrans * pbr.color.rgb * d * g * absDotWoWh * absDotWiWh / denom;

    } else {
        float3 wh = normalize(wo + wi);
        float fd = fDielectric(wo, wh, pbr.etaA, pbr.etaB).x;

        float prSpecular = (1.f - m) * fd;
        float prDiffuse = (1.f - m - prSpecular) * (1.f - pbr.transmissiveness);

        float R = fd;
        float T = pbr.transmissiveness * (1.f - R);

        float d = d(wh, alpha2);
        float g = g(wo, wi, alpha2);

        float denom = 4.f * cosThetaO;

        //Set the bsdf and pdf values
        bsdf = m * d * g * f(pbr.color.rgb, wo, wh) / denom + // Metallic contribution
               prSpecular * d * g / denom + // Specular reflection contribution
               prDiffuse * pbr.color.rgb * cosThetaI / PI; //Diffuse contribution

        float pdfR = d * g1(wo, alpha2) / denom;
        pdf = m * pdfR + // Metallic contribution
              pDielectric * (R + T > 0.f ? R / (R + T) : 0.f) * pdfR + // Specular reflection contribution
              pDiffuse * cosThetaI / PI; //Diffuse contribution
    }
}

inline float powerHeuristic(int nf, float fPdf, int ng, float gPdf) {
    float denom = (float(nf) * fPdf) * (float(nf) * fPdf) + (float(ng) * gPdf) * (float(ng) * gPdf);
    if (denom == 0.f) {
        return 0.f;
    }

    return (float(nf) * fPdf) * (float(nf) * fPdf) / denom;
}