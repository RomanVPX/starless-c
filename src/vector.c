#include "vector.h"
#include <math.h> // Make sure math.h is included

Vec3d vec3d_add(Vec3d a, Vec3d b) { return (Vec3d){a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d vec3d_sub(Vec3d a, Vec3d b) { return (Vec3d){a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3d vec3d_mul_scalar(Vec3d v, double s) { return (Vec3d){v.x * s, v.y * s, v.z * s}; }
Vec3d vec3d_div_scalar(Vec3d v, double s) { return (Vec3d){v.x / s, v.y / s, v.z / s}; } // Add check for s != 0 if needed
double vec3d_dot(Vec3d a, Vec3d b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3d vec3d_cross(Vec3d a, Vec3d b) {
    return (Vec3d){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}
double vec3d_norm_sqr(Vec3d v) { return vec3d_dot(v, v); }
double vec3d_norm(Vec3d v) { return sqrt(vec3d_norm_sqr(v)); }
Vec3d vec3d_normalize(Vec3d v) {
    double n = vec3d_norm(v);
    // Add check for n != 0 if robust handling is needed
    if (n == 0.0) return (Vec3d){0,0,0}; // Or handle error
    return vec3d_div_scalar(v, n);
}