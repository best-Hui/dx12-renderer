struct PixelShaderInput
{
    float2 Uv : TEXCOORD0;
    float4 ColorAndAlpha : COLOR0;
};

float4 main(PixelShaderInput IN) : SV_TARGET
{
    const float2 centeredUv = IN.Uv * 2.0f - 1.0f;
    const float distanceToCenter = length(centeredUv);
    const float outerAlpha = 1.0f - smoothstep(0.78f, 1.0f, distanceToCenter);
    const float innerAlpha = 1.0f - smoothstep(0.22f, 0.42f, distanceToCenter);
    const float ringAlpha = smoothstep(0.50f, 0.62f, distanceToCenter) * (1.0f - smoothstep(0.72f, 0.90f, distanceToCenter));
    const float alpha = saturate(max(innerAlpha, ringAlpha * 0.85f) * outerAlpha) * IN.ColorAndAlpha.a;

    clip(alpha - 0.001f);

    const float3 color = lerp(IN.ColorAndAlpha.rgb * 0.65f, 1.0f.xxx, innerAlpha * 0.35f);
    return float4(color, alpha);
}
