#include "config.h"
#include <stdio.h>
#include <string.h> // For strcmp, strdup
#include <stdlib.h> // For free
#include <stdbool.h>

// Placeholder: Just sets minimal defaults so main doesn't crash
bool load_config(int argc, char *argv[], Config *cfg) {
    // --- Set Defaults (similar to Python defaults dictionary) ---
    cfg->resolution[0] = 160; // Default low res
    cfg->resolution[1] = 120;
    cfg->n_iterations = 100;  // Low iterations for testing
    cfg->step_size = 0.02;
    cfg->n_threads = 4;
    cfg->chunk_size = 1000; // Small chunk size for testing
    cfg->lofi = true; // Assume lofi unless args say otherwise
    cfg->method = METH_RK4;

    cfg->camera_pos = (Vec3d){0.0, 1.0, -10.0};
    cfg->tan_fov = 1.5; // Raw FoV value from Python
    cfg->look_at = (Vec3d){0.0, 0.0, 0.0};
    cfg->up_vector = (Vec3d){0.0, 1.0, 0.0}; // Default Up
    cfg->distort = true;
    cfg->disk_inner_radius = 1.5;
    cfg->disk_outer_radius = 4.0;

    cfg->horizon_grid = true;
    cfg->disk_texture_mode = DT_GRID; // Default to grid for now
    cfg->sky_texture_mode = ST_NONE;  // Default to black sky
    cfg->disk_texture_path = NULL;
    cfg->sky_texture_path = NULL;
    cfg->disk_texture = NULL;
    cfg->sky_texture = NULL;
    cfg->sky_disk_ratio = 1.0; // Should read from config
    cfg->fog_do = true;
    cfg->fog_mult = 0.02;
    cfg->fog_skip = 1;
    cfg->blur_do = true;
    cfg->bloom_cut = 2.0;
    cfg->airy_bloom = true;
    cfg->airy_radius = 1.0;
    cfg->gain = 1.0;
    cfg->normalize = -1.0; // Off
    cfg->srgb_out = true;
    cfg->srgb_in = true;

    cfg->disk_multiplier = 100.0;
    cfg->disk_intensity_do = true;
    cfg->redshift = 1.0; // Redshift factor multiplier for BB disk temp

    // --- TODO: Parse Command Line Arguments (override defaults) ---
    // e.g., loop through argv, check for "-r640x480", "-j8", etc.

    // --- TODO: Parse INI file (override defaults/args) ---
    // Use inih or custom parser on SCENE_FNAME

    // --- Compute Derived Values ---
    compute_derived_config(cfg);

    // --- TODO: Load Textures Based on Config ---
    // if (cfg->disk_texture_mode == DT_TEXTURE && cfg->disk_texture_path) {
    //     cfg->disk_texture = load_texture(cfg->disk_texture_path);
    // }
    // if (cfg->sky_texture_mode == ST_TEXTURE && cfg->sky_texture_path) {
    //     cfg->sky_texture = load_texture(cfg->sky_texture_path);
    // }


    printf("Config loaded (placeholder defaults).\n");
    printf("Resolution: %dx%d\n", cfg->resolution[0], cfg->resolution[1]);

    return true; // Success for now
}

void compute_derived_config(Config *cfg) {
    // Disk radii squared
    cfg->disk_inner_sqr = cfg->disk_inner_radius * cfg->disk_inner_radius;
    cfg->disk_outer_sqr = cfg->disk_outer_radius * cfg->disk_outer_radius;

    // View Matrix calculation (like Python code)
    Vec3d front = vec3d_normalize(vec3d_sub(cfg->look_at, cfg->camera_pos));
    Vec3d left = vec3d_normalize(vec3d_cross(cfg->up_vector, front));
    Vec3d new_up = vec3d_normalize(vec3d_cross(front, left)); // Ensure orthonormal

    cfg->view_matrix[0] = left;   // Left vector
    cfg->view_matrix[1] = new_up; // Up vector
    cfg->view_matrix[2] = front;  // Front vector

     // Convert FoV angle (if that's what 1.5 means) to tan(FoV/2) for projection?
     // The Python code uses TANFOV = 1.5 directly in projection. Let's assume it's already
     // the tangent of half the *horizontal* FoV scaled by aspect ratio or something similar.
     // For now, we just copy the value. If the projection looks wrong later, revisit this.
     // cfg->tan_fov = tan(cfg->field_of_view_degrees * M_PI / 180.0 / 2.0); // Example if FoV was degrees
}

// Free textures if they were loaded
void free_config_textures(Config *cfg) {
    if (cfg->disk_texture) {
        free_texture(cfg->disk_texture);
        cfg->disk_texture = NULL;
    }
    if (cfg->sky_texture) {
        free_texture(cfg->sky_texture);
        cfg->sky_texture = NULL;
    }
    // Free path strings if they were duplicated (e.g., with strdup)
    // free((void*)cfg->disk_texture_path); // Careful if paths point to argv or static strings
    // free((void*)cfg->sky_texture_path);
}
