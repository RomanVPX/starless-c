#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include "vector.h"
#include "color.h"

typedef struct Texture Texture;
typedef struct ImageF ImageF;

// --- Enums ---
#define SKY_TEXTURE_MODE_TABLE \
    X(ST_NONE, "none") \
    X(ST_TEXTURE, "texture") \
    X(ST_FINAL, "final")

#define DISK_TEXTURE_MODE_TABLE \
    X(DT_NONE, "none") \
    X(DT_TEXTURE, "texture") \
    X(DT_SOLID, "solid") \
    X(DT_GRID, "grid") \
    X(DT_BLACKBODY, "blackbody")

#define X(a, b) a,
typedef enum { SKY_TEXTURE_MODE_TABLE } SkyTextureMode;
typedef enum { DISK_TEXTURE_MODE_TABLE } DiskTextureMode;
#undef X

#define X(a, b) [a] = b,
static const char *sky_texture_mode_names[] = { SKY_TEXTURE_MODE_TABLE };
static const char *disk_texture_mode_names[] = { DISK_TEXTURE_MODE_TABLE };
#undef X


// --- Config Structure ---
typedef struct Config
{
    // File & Scene
    char *scene_file_path; // Path to the scene file (or NULL if not loaded from file)
    char *scene_base_name; // Base name of the scene file (without path and extension)

    // Resolution & Performance
    int resolution[2];
    int n_iterations;
    double step_size;
    int ssaa_level; // Raytracer supersampling (1 = no SS, 2 = 2x2, 3 = 3x3...)
    int n_threads;
    int chunk_size;
    bool lofi;

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
    bool airy_bloom;
    double airy_radius;
    double gain;
    double aces_exposure; // Exposure for ACES tonemapping
    double normalize;
    bool srgb_in;
    bool srgb_out;

    // Blackbody Disk Specifics
    char *blackbody_ramp_path;
    ColorRGB *blackbody_ramp_data;
    int blackbody_ramp_size;
    double disk_multiplier;
    bool disk_intensity_do;
    double redshift;

    // Blackbody Disk Opacity
    bool disk_opacity_falloff;
    double disk_opacity_falloff_exp;

    // Blackbody Disk Structure
    bool disk_add_structure;
    int disk_structure_spiral_arms;
    Vec3d disk_structure_rings_freq; // Frequency multipliers of (thin, medium, thick) rings
    double disk_structure_spiral_pitch;
    double disk_structure_position_variation;
    double disk_structure_modulation;

    // Derived / Internal
    Vec3d view_matrix[3]; // [0]=left, [1]=up, [2]=front
} Config;

// --- UserData for ini_parse callbacks ---
typedef struct
{
    Config *cfg;             // Pointer to the main config struct
    bool *override_res_flag; // Pointer to the command-line override flag for resolution
} IniParseUserData;

// --- Function Declarations ---
bool load_config(int argc, char *argv[], Config *cfg);
void free_config_textures(Config *cfg);
void compute_derived_config(Config *cfg);

#endif // CONFIG_H
