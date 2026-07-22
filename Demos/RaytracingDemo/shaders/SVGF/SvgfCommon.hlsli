#ifndef RAYTRACING_DEMO_SVGF_COMMON_HLSLI
#define RAYTRACING_DEMO_SVGF_COMMON_HLSLI

float3 DecodeSvgfNormal(float3 encoded)
{
    return normalize(encoded * 2.0f - 1.0f);
}

float SvgfLuminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 SvgfToneMap(float3 color)
{
    color = color / (color + 1.0f);
    return pow(saturate(color), 1.0f / 2.2f);
}

bool SvgfIsValidDepth(float depth)
{
    return depth < 1.0f;
}

float SvgfNormalWeight(float3 normalCenter, float3 normalSample, float phiNormal)
{
    return pow(saturate(dot(normalCenter, normalSample)), max(phiNormal, 0.001f));
}

float SvgfDepthWeight(float depthCenter, float depthSample, float3 positionCenter, float3 positionSample, float phiDepth)
{
    const float positionDistance = length(positionCenter - positionSample);
    const float depthDistance = abs(depthCenter - depthSample);
    const float scale = max(0.0001f, phiDepth * max(depthCenter, 0.001f));
    return exp(-(positionDistance + depthDistance) / scale);
}

float SvgfColorWeight(float3 colorCenter, float3 colorSample, float variance, float phiColor)
{
    const float lumaCenter = SvgfLuminance(colorCenter);
    const float lumaSample = SvgfLuminance(colorSample);
    const float sigma = max(0.0001f, phiColor * sqrt(max(variance, 0.0f)));
    return exp(-abs(lumaCenter - lumaSample) / sigma);
}

#endif
