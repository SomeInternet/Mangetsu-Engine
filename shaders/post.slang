struct PushConstants {
    uint32_t outputImageIdx;
};

[vk::push_constant]
PushConstants pushConstants;

[[vk::binding(0, 0)]] 
RWTexture2D<float4> radianceImage;

[[vk::binding(1, 0)]] 
[vk::image_format("rgba8")] 
RWTexture2D<float4> swapchainImages[];

float3 reinhard(float3 color) {
    return color / (color + float3(1));
}

float3 gammaCorrect(float3 color) {
    return pow(color, 1.f / 2.2f);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 globalId: SV_DispatchThreadID, uint3 localId: SV_GroupThreadID) { //SV means system value. We specified the dimensions of the workgroups, and in the dispatch command we specified the number of workgroups
    
    //TODO

    uint2 size;
    radianceImage.GetDimensions(size.x, size.y); //Store image dimensions in x, y

    if (globalId.x < size.x && globalId.y < size.y) {
        float3 color = gammaCorrect(reinhard(radianceImage[globalId.xy].rgb));

        swapchainImages[pushConstants.outputImageIdx][globalId.xy] = float4(color, 1.f);
    }
}