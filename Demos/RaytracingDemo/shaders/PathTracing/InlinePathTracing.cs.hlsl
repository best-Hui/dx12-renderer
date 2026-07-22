#include "PathTracing.rayquery.hlsli"
#include "PathTracingShared.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= Camera_Width || pixel.y >= Camera_Height)
    {
        return;
    }

    WritePathTracingOutput(pixel, Camera_Width, Camera_FrameIndex);
}
