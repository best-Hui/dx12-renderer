#ifndef RAYTRACING_DEMO_PATH_TRACING_CONSTANTS_HLSLI
#define RAYTRACING_DEMO_PATH_TRACING_CONSTANTS_HLSLI

static const float PI = 3.14159265359f;
static const float INV_PI = 0.31830988618f;
static const float FLOAT_MACHINE_EPSILON = 5.9604644775390625e-8f;

float Gamma(uint n)
{
    const float ne = float(n) * FLOAT_MACHINE_EPSILON;
    return ne / max(1.0f - ne, FLOAT_MACHINE_EPSILON);
}

#endif
