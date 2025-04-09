#include "config.h"
#include <stdio.h>
#include <string.h> // For strcmp, strdup, strtol, strtod, sscanf
#include <stdlib.h> // For free, exit, EXIT_FAILURE
#include <stdbool.h>
#include <unistd.h> // For access() to check file existence
#include <ctype.h>  // For isdigit


// --- Helper function to convert string to bool ---
bool string_to_bool(const char* str) {
    if (!str) return false;
    if (strcmp(str, "1") == 0 || strcasecmp(str, "true") == 0 || strcasecmp(str, "yes") == 0) {
        return true;
    }
    return false;
}


// --- Function to parse a resolution string like "WxH" ---
bool parse_resolution(const char* res_str, int resolution[2]) {
    if (!res_str) return false;
    // Find the 'x' separator
    const char* x_pos = strchr(res_str, 'x');
    if (!x_pos || x_pos == res_str || *(x_pos + 1) == '\0') {
        // 'x' not found, or it's at the beginning/end
        return false;
    }
    // Use sscanf for potentially more robust parsing than manual strtol
    int width = 0, height = 0;
    if (sscanf(res_str, "%dx%d", &width, &height) == 2) {
         if (width > 0 && height > 0) {
            resolution[0] = width;
            resolution[1] = height;
            return true;
         }
    }
     // Fallback or alternative: manual parsing
     /*
    char* endptr_w;
    long w = strtol(res_str, &endptr_w, 10);

    if (endptr_w != x_pos || w <= 0 || w > 32767) { // Check conversion and range
        return false;
    }

    char* endptr_h;
    long h = strtol(x_pos + 1, &endptr_h, 10);

    // Check conversion, range, and that the whole height part was consumed
    if (*endptr_h != '\0' || h <= 0 || h > 32767) {
        return false;
    }
    resolution[0] = (int)w;
    resolution[1] = (int)h;
    return true;
    */
   return false;
}


// --- Main Config Loading Function ---
bool load_config(int argc, char *argv[], Config *cfg) {

    // --- Set Defaults (moved defaults here for clarity) ---
    cfg->resolution[0] = 160;
    cfg->resolution[1] = 120;
    cfg->n_iterations = 1000; // Default Iterations from Python
    cfg->step_size = 0.02;
    cfg->n_threads = 4;
    cfg->chunk_size = 9000; // Default Chunksize from Python
    cfg->lofi = false; // Default to HIFI unless -d is given
    cfg->method = METH_RK4;

    cfg->camera_pos = (Vec3d){0.0, 1.0, -10.0};
    cfg->tan_fov = 1.5; // Raw FoV value from Python
    cfg->look_at = (Vec3d){0.0, 0.0, 0.0};
    cfg->up_vector = (Vec3d){0.0, 1.0, 0.0};
    cfg->distort = true;
    cfg->disk_inner_radius = 1.5;
    cfg->disk_outer_radius = 4.0;

    cfg->horizon_grid = true;
    cfg->disk_texture_mode = DT_NONE; // Will be set by INI usually
    cfg->sky_texture_mode = ST_NONE;  // Will be set by INI usually
    cfg->disk_texture_path = NULL;    // Paths will be set by INI
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
    cfg->redshift = 1.0;

    const char *scene_fname = "scenes/default.scene"; // Default scene file
    bool override_res = false; // Flag if -r was used

    // --- Parse Command Line Arguments ---
    printf("Parsing command line arguments...\n");
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0) {
            cfg->lofi = true;
            printf("  Found -d: Enabling Lo-Fi mode defaults (will be applied during INI parse).\n");
        } else if (strcmp(argv[i], "--no-graph") == 0) {
            printf("  Found --no-graph: (Ignoring, graph not implemented).\n");
            // DRAWGRAPH = false; // No equivalent needed yet
        } else if (strcmp(argv[i], "--no-display") == 0) {
             printf("  Found --no-display: (Ignoring, display not implemented).\n");
            // DISABLE_DISPLAY = 1;
        } else if (strcmp(argv[i], "--no-shuffle") == 0) {
            printf("  Found --no-shuffle: (Note: Shuffling not implemented yet).\n");
            // DISABLE_SHUFFLING = 1;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--no-bs") == 0) {
             printf("  Found -o/--no-bs: (Ignoring related display/graph flags).\n");
             // Set related flags off if implemented later
        } else if (strncmp(argv[i], "-c", 2) == 0 && strlen(argv[i]) > 2 && isdigit(argv[i][2])) {
             long csize = strtol(argv[i] + 2, NULL, 10);
             if (csize > 0) {
                 cfg->chunk_size = (int)csize;
                 printf("  Found -c: Setting chunk size to %d\n", cfg->chunk_size);
             } else {
                 fprintf(stderr, "Warning: Invalid chunk size '%s'. Ignoring.\n", argv[i] + 2);
             }
        } else if (strncmp(argv[i], "-j", 2) == 0 && strlen(argv[i]) > 2 && isdigit(argv[i][2])) {
            long threads = strtol(argv[i] + 2, NULL, 10);
            if (threads > 0) {
                cfg->n_threads = (int)threads;
                 printf("  Found -j: Setting thread count to %d\n", cfg->n_threads);
            } else {
                 fprintf(stderr, "Warning: Invalid thread count '%s'. Ignoring.\n", argv[i] + 2);
            }
        } else if (strncmp(argv[i], "-r", 2) == 0 && strlen(argv[i]) > 2) {
            if (parse_resolution(argv[i] + 2, cfg->resolution)) {
                printf("  Found -r: Setting resolution override to %dx%d\n", cfg->resolution[0], cfg->resolution[1]);
                override_res = true;
            } else {
                fprintf(stderr, "Error: Resolution format unreadable in '%s'. Use WxH (e.g., -r640x480).\n", argv[i]);
                return false; // Fatal error like Python code
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unrecognized option: %s\n", argv[i]);
            return false; // Fatal error
        } else {
            // Assume it's the scene filename
            scene_fname = argv[i];
             printf("  Found scene file argument: %s\n", scene_fname);
        }
    }

    // --- Check if scene file exists ---
    if (access(scene_fname, F_OK) == -1) {
        fprintf(stderr, "Error: Scene file \"%s\" does not exist or is not accessible.\n", scene_fname);
        return false;
    }
    printf("Using scene file: %s\n", scene_fname);


    // --- TODO: Parse INI file (scene_fname) ---
    // This is where we'll use inih later.
    // The INI parser should read into the cfg struct.
    // It should respect the cfg->lofi flag set by '-d' to choose
    // between [lofi] and [hifi] sections for Resolution, Iterations, Stepsize.
    // It should *not* override resolution if override_res is true.
    printf("--- Placeholder: INI Parsing would happen here for '%s' ---\n", scene_fname);
    printf("--- Using hardcoded defaults + command-line args for now ---\n");


    // --- Load Textures Based on Config (Placeholder paths for now) ---
    // This needs to happen *after* INI parsing sets the paths and modes
    printf("Loading textures (placeholder - requires INI parse)...\n");
    // Example: If INI parsing sets disk_texture_mode = DT_TEXTURE and
    // disk_texture_path = "textures/adisk.jpg"
    // if (cfg->disk_texture_mode == DT_TEXTURE && cfg->disk_texture_path) {
    //     cfg->disk_texture = load_texture(cfg->disk_texture_path);
    //     if (!cfg->disk_texture) {
    //         fprintf(stderr, "Warning: Failed to load disk texture '%s'\n", cfg->disk_texture_path);
    //         // Decide if this is fatal or fallback to another mode?
    //     }
    // }
    // Similarly for sky texture...


    // --- Compute Derived Values ---
    printf("Computing derived configuration values...\n");
    compute_derived_config(cfg); // Should be called *after* all base values are set

    // Final checks
    if (vec3d_norm(cfg->camera_pos) <= 1.0) { // Use 1.0 for Schwarzschild radius
        fprintf(stderr, "Error: Observer is inside the event horizon (r <= 1.0). Set Cameraposition further out.\n");
        return false;
    }

    printf("Configuration loaded successfully.\n");
    printf("Final Resolution: %dx%d\n", cfg->resolution[0], cfg->resolution[1]);
    printf("Iterations: %d, Step Size: %f\n", cfg->n_iterations, cfg->step_size);
    printf("Threads: %d, Chunk Size: %d\n", cfg->n_threads, cfg->chunk_size);

    return true; // Success
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
