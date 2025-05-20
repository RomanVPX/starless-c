#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "vector.h"
#include "color.h"

typedef struct Texture Texture;
typedef struct ImageF ImageF;

// --- Enums ---
typedef enum { METH_LEAPFROG, METH_RK4 } IntegrationMethod;
typedef enum { ST_NONE, ST_TEXTURE, ST_FINAL } SkyTextureMode;
typedef enum { DT_NONE, DT_TEXTURE, DT_SOLID, DT_GRID, DT_BLACKBODY } DiskTextureMode;

// --- Config Structure ---
typedef struct Config {
    // Resolution & Performance
    int resolution[2];
    int n_iterations;
    double step_size;
    int n_threads;
    int chunk_size;
    bool lofi;
    IntegrationMethod method;

    // Camera & View
    Vec3d camera_pos;
    double tan_fov;
    Vec3d look_at;
    Vec3d up_vector;

    // Geometry
    bool distort; // Gravitational lensing
    double disk_inner_radius;
    double disk_outer_radius;
    double disk_inner_sqr; // Derived
    double disk_outer_sqr; // Derived

    // Materials
    bool horizon_grid;
    DiskTextureMode disk_texture_mode;
    SkyTextureMode sky_texture_mode;
    char *disk_texture_path; // Allocated path string (or NULL)
    char *sky_texture_path;  // Allocated path string (or NULL)
    Texture *disk_texture;   // Loaded texture struct
    Texture *sky_texture;    // Loaded texture struct
    double sky_disk_ratio;
    bool fog_do;
    double fog_mult;
    int fog_skip; // Fog applies every fog_skip steps

    // Post-Processing
    bool blur_do;
    double bloom_cut;
    bool airy_bloom;
    double airy_radius;
    double gain;
    double aces_exposure; // Exposure for ACES tonemapping
    double normalize;
    bool srgb_in;
    bool srgb_out;

    // Blackbody Disk Specific
    char *blackbody_ramp_path;
    ColorRGB *blackbody_ramp_data;
    int blackbody_ramp_size;
    double disk_multiplier;
    bool disk_intensity_do;
    double redshift;

    // Derived / Internal
    Vec3d view_matrix[3]; // [0]=left, [1]=up, [2]=front
} Config;

// --- UserData for ini_parse callbacks ---
typedef struct {
    Config *cfg;                // Pointer to the main config struct
    bool *override_res_flag;    // Pointer to the command-line override flag for resolution
} IniParseUserData;

// --- Function Declarations ---
bool load_config(int argc, char *argv[], Config *cfg);
void free_config_textures(Config *cfg);
void compute_derived_config(Config *cfg);

#endif // CONFIG_H
