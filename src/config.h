#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "image.h"
#include "vector.h"

// Enums matching Python code
typedef enum { METH_LEAPFROG, METH_RK4 } IntegrationMethod;
typedef enum { ST_NONE, ST_TEXTURE, ST_FINAL } SkyTextureMode;
typedef enum { DT_NONE, DT_TEXTURE, DT_SOLID, DT_GRID, DT_BLACKBODY } DiskTextureMode;

typedef struct {
    // Resolution & Performance
    int resolution[2];
    int n_iterations;
    double step_size;
    int n_threads;
    int chunk_size; // Approx pixels per chunk
    bool lofi;
    IntegrationMethod method;

    // Geometry
    Vec3d camera_pos;
    double tan_fov;
    Vec3d look_at;
    Vec3d up_vector;
    bool distort; // Gravitational lensing
    double disk_inner_radius;
    double disk_outer_radius;
    double disk_inner_sqr;
    double disk_outer_sqr;

    // Materials & Effects
    bool horizon_grid;
    DiskTextureMode disk_texture_mode;
    SkyTextureMode sky_texture_mode;
    const char* disk_texture_path; // Path if DT_TEXTURE
    const char* sky_texture_path;  // Path if ST_TEXTURE
    Texture *disk_texture;         // Loaded texture
    Texture *sky_texture;          // Loaded texture
    double sky_disk_ratio;
    bool fog_do;
    double fog_mult;
    int fog_skip;
    bool blur_do;
    double bloom_cut;
    bool airy_bloom;
    double airy_radius;
    double gain;
    double normalize; // -1 means off, > 0 is target max value
    bool srgb_out;
    bool srgb_in; // For input textures

    // Blackbody Disk Specific
    double disk_multiplier;
    bool disk_intensity_do;
    double redshift;

    // Internal/Derived
    Vec3d view_matrix[3]; // [0]=left, [1]=up, [2]=front
    bool *internal_override_res_ptr; // Internal pointer used during INI parsing

} Config;

// Function to load config from file and command line args
bool load_config(int argc, char *argv[], Config *cfg);
void free_config_textures(Config *cfg); // Free loaded textures
void compute_derived_config(Config *cfg); // Calculate view matrix, sqr radii etc.

#endif // CONFIG_H
