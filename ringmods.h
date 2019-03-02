//
//  ringmods.h
//
// based on mutable instruments' Warps code

#ifndef ringmods_h
#define ringmods_h
#include <math.h>

inline float diode(float x)
{
    // Approximation of diode non-linearity from:
    // Julian Parker - "A simple Digital model of the diode-based ring-modulator."
    // Proc. DAFx-11
    float sign = x > 0.0f ? 1.0f : -1.0f;
    float dead_zone = fabs(x) - 0.667f;
    dead_zone += fabs(dead_zone);
    dead_zone *= dead_zone;
    return 0.04324765822726063f * dead_zone * sign;
}

inline float SoftLimit(float x)
{
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

inline float analog_ringmod(float modulator, float carrier, float parameter)
{
    carrier *= 2.0f;
    float ring = diode(modulator + carrier) + diode(modulator - carrier);
    ring *= (4.0f + parameter * 24.0f);
    return SoftLimit(ring);
}

inline float digital_ringmod(float x_1, float x_2, float parameter)
{
    float ring = 4.0f * x_1 * x_2 * (1.0f + parameter * 8.0f);
    return ring / (1.0f + fabs(ring));
}

#endif /* ringmods_h */
