#ifndef INTERPOLATION_H
#define INTERPOLATION_H
#include <math.h>


static inline double clamp(double x, double lower_limit, double upper_limit) { return fmax(lower_limit, fmin(upper_limit, x)); }

static inline double saturate(double x) { return fmax(0.0, fmin(1.0, x)); }


static inline double lerp(double a, double b, double t)
{
    return a + (b - a) * saturate(t);
}

static inline double smoothstep(double edge0, double edge1, double x)
{
    x = saturate((x - edge0) / (edge1 - edge0));

    return x * x * (3.0 - 2.0 * x);
}

static inline double smootherstep(double edge0, double edge1, double x)
{
    x = saturate((x - edge0) / (edge1 - edge0));
    return x * x * x * (x * (6.0 * x - 15.0) + 10.0);
}

#endif
