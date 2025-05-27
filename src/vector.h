#ifndef VECTOR_H
#define VECTOR_H

#if defined(_MSC_VER)
    #define _USE_MATH_DEFINES
#endif
#define _GNU_SOURCE
#include <math.h>
#include <stdbool.h>

typedef struct {
    double x, y, z;
} Vec3d;

// Basic Operations
Vec3d vec3d_add(Vec3d a, Vec3d b);
Vec3d vec3d_sub(Vec3d a, Vec3d b);
Vec3d vec3d_mul_scalar(Vec3d v, double s);
Vec3d vec3d_div_scalar(Vec3d v, double s);
double vec3d_dot(Vec3d a, Vec3d b);
Vec3d vec3d_cross(Vec3d a, Vec3d b);
double vec3d_norm_sqr(Vec3d v);
double vec3d_norm(Vec3d v);
Vec3d vec3d_normalize(Vec3d v);

#endif // VECTOR_H
