#include "config.h"
#include <stdio.h>
#include <string.h> // For strcmp, strdup, strtol, strtod, sscanf, strcasecmp
#include <stdlib.h> // For free, exit, EXIT_FAILURE, atoi, atof
#include <stdbool.h>
#include <unistd.h> // For access() to check file existence
#include <ctype.h>  // For isdigit, isspace
#include "ini.h"
#include "image.h"

// --- Function Prototypes ---
bool string_to_bool(const char* str);
bool parse_resolution(const char* res_str, int resolution[2]);
bool parse_vec3d(const char* str, Vec3d *vec);
bool parse_int_list(const char* str, int* arr, int expected_count);
void compute_derived_config(Config *cfg);

// --- Helper function definitions ---
// Definition for: parse_vec3d
bool parse_vec3d(const char* str, Vec3d *vec) {
    if (!str || !vec) return false;
    double x, y, z;
    if (sscanf(str, "%lf,%lf,%lf", &x, &y, &z) == 3) {
        vec->x = x;
        vec->y = y;
        vec->z = z;
        return true;
    }
    return false;
}

// Definition for: parse_int_list
bool parse_int_list(const char* str, int* arr, int expected_count) {
    if (!str || !arr || expected_count <= 0) return false;
    char* str_copy = strdup(str); // Work on a copy as strtok modifies it
    if (!str_copy) return false;

    char* token;
    const char* delim = ",";
    int count = 0;
    token = strtok(str_copy, delim);
    while (token != NULL && count < expected_count) {
        // Trim whitespace (optional but good)
        while (isspace((unsigned char)*token)) token++; // Cast to unsigned char for safety with ctype functions
        char* end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';

        if (strlen(token) > 0) { // Check if token is not empty after trimming
            char* endptr;
            long val = strtol(token, &endptr, 10);
            if (*endptr == '\0') { // Ensure whole token was parsed as integer
                 arr[count++] = (int)val;
            } else {
                // Parsing failed for this token
                count = -1; // Indicate error
                break;
            }
        } else {
             // Empty token (e.g., "1,,3") - treat as error or skip? Let's error.
             count = -1;
             break;
        }
        token = strtok(NULL, delim);
    }
    free(str_copy);
    return count == expected_count;
}

// Definition for: string_to_bool
bool string_to_bool(const char* str) {
    if (!str) return false;
     // Use strcasecmp if available (needs <strings.h> usually, but often included by string.h on POSIX)
    #ifdef _WIN32
        // Windows doesn't have strcasecmp, use _stricmp
        #define C_STRCASECMP _stricmp
    #else
        #define C_STRCASECMP strcasecmp
    #endif
    if (strcmp(str, "1") == 0 || C_STRCASECMP(str, "true") == 0 || C_STRCASECMP(str, "yes") == 0) {
        return true;
    }
    #undef C_STRCASECMP
    return false;
}

// Definition for: parse_resolution
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
   return false;
}


// --- INI parsing callback function definition ---
static int scene_ini_callback(void* user, const char* section, const char* name, const char* value) {
    Config* pconfig = (Config*)user;
    // --- HACK to get override_res flag ---
    // Check if the user pointer itself is usable to store this flag address temporarily
    // This avoids the need for the extra pointer in the Config struct itself.
    // Let's try passing the address of override_res directly as the user pointer in ini_parse call.
    // NO - user pointer should point to the Config struct.
    // Stick with the internal pointer for now, it's explicit.
     bool *p_override_res = pconfig->internal_override_res_ptr;
     if (!p_override_res) {
         fprintf(stderr,"Runtime Error: internal_override_res_ptr not set in config for ini_handler! Cannot check -r flag.\n");
         return 0; // Stop parsing
     }

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    // --- Handle LoFi/HiFi sections for specific overrides ---
    if (MATCH("lofi", "Resolution") || MATCH("hifi", "Resolution")) {
        if ((pconfig->lofi && MATCH("lofi", "Resolution")) ||
            (!pconfig->lofi && MATCH("hifi", "Resolution"))) {
            if (!(*p_override_res)) { // Only parse if -r wasn't used
                 if (!parse_int_list(value, pconfig->resolution, 2)) {
                      fprintf(stderr, "Warning: Invalid format for Resolution '%s' in section [%s]. Using default/previous.\n", value, section);
                 }
            }
        }
    } else if (MATCH("lofi", "Iterations") || MATCH("hifi", "Iterations")) {
         if ((pconfig->lofi && MATCH("lofi", "Iterations")) ||
            (!pconfig->lofi && MATCH("hifi", "Iterations"))) {
             pconfig->n_iterations = atoi(value);
         }
    } else if (MATCH("lofi", "Stepsize") || MATCH("hifi", "Stepsize")) {
         if ((pconfig->lofi && MATCH("lofi", "Stepsize")) ||
            (!pconfig->lofi && MATCH("hifi", "Stepsize"))) {
              pconfig->step_size = atof(value);
         }
    }
    // --- Geometry Section ---
    else if (MATCH("geometry", "Cameraposition")) { if (!parse_vec3d(value, &pconfig->camera_pos)) fprintf(stderr, "Warning: Invalid format for Cameraposition '%s'\n", value); }
    else if (MATCH("geometry", "Fieldofview")) { pconfig->tan_fov = atof(value); } // Assuming value is already tan(FoV_H/2)*aspect or similar
    else if (MATCH("geometry", "Lookat")) { if (!parse_vec3d(value, &pconfig->look_at)) fprintf(stderr, "Warning: Invalid format for Lookat '%s'\n", value); }
    else if (MATCH("geometry", "Upvector")) { if (!parse_vec3d(value, &pconfig->up_vector)) fprintf(stderr, "Warning: Invalid format for Upvector '%s'\n", value); }
    else if (MATCH("geometry", "Distort")) { pconfig->distort = string_to_bool(value); }
    else if (MATCH("geometry", "Diskinner")) { pconfig->disk_inner_radius = atof(value); }
    else if (MATCH("geometry", "Diskouter")) { pconfig->disk_outer_radius = atof(value); }

    // --- Materials Section ---
    else if (MATCH("materials", "Horizongrid")) { pconfig->horizon_grid = string_to_bool(value); }
    else if (MATCH("materials", "Disktexture")) {
        // Store path first, as it might be needed even if mode changes later
        char* new_path = strdup(value); // Use temp var
        if (new_path) { // Check if strdup succeeded
            free((void*)pconfig->disk_texture_path); // Free old path if any
            pconfig->disk_texture_path = new_path; // Assign new path
        } else {
             fprintf(stderr, "Warning: Failed to duplicate Disktexture path string.\n");
        }

        // Convert string mode to enum based on the *value* (which is also the path for texture mode)
        if (strcasecmp(value, "none") == 0) pconfig->disk_texture_mode = DT_NONE;
        else if (strcasecmp(value, "texture") == 0) {
            pconfig->disk_texture_mode = DT_TEXTURE;
            // PROBLEM: If value is "texture", how do we get the actual path?
            // The original Python code used Disktexture = path/to/texture.jpg
            // Let's assume the INI does the same.
            // So, if value is not "none", "solid", etc. assume it IS the path.
            // Re-evaluate logic:
            free((void*)pconfig->disk_texture_path); // Free old path
            pconfig->disk_texture_path = strdup(value); // Assume value is path or mode
            if (!pconfig->disk_texture_path) { /* Handle strdup error */ }

            if (strcasecmp(value, "none") == 0) pconfig->disk_texture_mode = DT_NONE;
            else if (strcasecmp(value, "solid") == 0) pconfig->disk_texture_mode = DT_SOLID;
            else if (strcasecmp(value, "grid") == 0) pconfig->disk_texture_mode = DT_GRID;
            else if (strcasecmp(value, "blackbody") == 0) pconfig->disk_texture_mode = DT_BLACKBODY;
            else {
                // Assume it's a path for texture mode
                pconfig->disk_texture_mode = DT_TEXTURE;
                // If we want to be robust, check if file exists? Maybe later.
                printf("Info: Assuming Disktexture value '%s' is a path for DT_TEXTURE mode.\n", value);
            }
        }
        // This logic seems flawed. Let's revert to simpler Python-like logic:
        // If the value matches a known mode string, set that mode. Otherwise assume it's a path.
        if (strcasecmp(value, "none") == 0) { pconfig->disk_texture_mode = DT_NONE; free((void*)pconfig->disk_texture_path); pconfig->disk_texture_path = NULL; }
        else if (strcasecmp(value, "solid") == 0) { pconfig->disk_texture_mode = DT_SOLID; free((void*)pconfig->disk_texture_path); pconfig->disk_texture_path = NULL; }
        else if (strcasecmp(value, "grid") == 0) { pconfig->disk_texture_mode = DT_GRID; free((void*)pconfig->disk_texture_path); pconfig->disk_texture_path = NULL; }
        else if (strcasecmp(value, "blackbody") == 0) { pconfig->disk_texture_mode = DT_BLACKBODY; free((void*)pconfig->disk_texture_path); pconfig->disk_texture_path = NULL; }
        else { // Assume it's a texture path
            pconfig->disk_texture_mode = DT_TEXTURE;
            free((void*)pconfig->disk_texture_path); // Free potentially old path
            pconfig->disk_texture_path = strdup(value);
            if (!pconfig->disk_texture_path) { fprintf(stderr, "Error: strdup failed for disk texture path\n"); return 0;}
        }
    }
    else if (MATCH("materials", "Skytexture")) {
         // Similar logic for Skytexture
         if (strcasecmp(value, "none") == 0) { pconfig->sky_texture_mode = ST_NONE; free((void*)pconfig->sky_texture_path); pconfig->sky_texture_path = NULL; }
         else if (strcasecmp(value, "final") == 0) { pconfig->sky_texture_mode = ST_FINAL; free((void*)pconfig->sky_texture_path); pconfig->sky_texture_path = NULL; }
         else { // Assume it's a texture path
            pconfig->sky_texture_mode = ST_TEXTURE;
            free((void*)pconfig->sky_texture_path);
            pconfig->sky_texture_path = strdup(value);
            if (!pconfig->sky_texture_path) { fprintf(stderr, "Error: strdup failed for sky texture path\n"); return 0;}
         }
    }
    else if (MATCH("materials", "Skydiskratio")) { pconfig->sky_disk_ratio = atof(value); }
    else if (MATCH("materials", "Fogdo")) { pconfig->fog_do = string_to_bool(value); }
    else if (MATCH("materials", "Fogmult")) { pconfig->fog_mult = atof(value); }
    else if (MATCH("materials", "Fogskip")) { pconfig->fog_skip = atoi(value); if (pconfig->fog_skip <= 0) pconfig->fog_skip = 1; }
    else if (MATCH("materials", "Blurdo")) { pconfig->blur_do = string_to_bool(value); }
    else if (MATCH("materials", "Bloomcut")) { pconfig->bloom_cut = atof(value); }
    else if (MATCH("materials", "Airy_bloom")) { pconfig->airy_bloom = string_to_bool(value); }
    else if (MATCH("materials", "Airy_radius")) { pconfig->airy_radius = atof(value); }
    else if (MATCH("materials", "Gain")) { pconfig->gain = atof(value); }
    else if (MATCH("materials", "Normalize")) { pconfig->normalize = atof(value); }
    else if (MATCH("materials", "sRGBOut")) { pconfig->srgb_out = string_to_bool(value); }
    else if (MATCH("materials", "sRGBIn")) { pconfig->srgb_in = string_to_bool(value); }
    else if (MATCH("materials", "Diskmultiplier")) { pconfig->disk_multiplier = atof(value); }
    else if (MATCH("materials", "Diskintensitydo")) { pconfig->disk_intensity_do = string_to_bool(value); }
    else if (MATCH("materials", "Redshift")) { pconfig->redshift = atof(value); }
    // else {
         // Optional: Warn about unknown keys
         // fprintf(stderr, "Warning: Unknown config key [%s] %s = %s\n", section, name, value);
    // }

    #undef MATCH
    return 1; // Success
}

// --- Main Config Loading Function definition ---
bool load_config(int argc, char *argv[], Config *cfg) {
    cfg->resolution[0] = 160;
    cfg->resolution[1] = 120;
    cfg->n_iterations = 1000;
    cfg->step_size = 0.02;
    cfg->n_threads = 4;
    cfg->chunk_size = 9000;
    cfg->lofi = false;
    cfg->method = METH_RK4;
    cfg->camera_pos = (Vec3d){0.0, 1.0, -10.0};
    cfg->tan_fov = 1.5;
    cfg->look_at = (Vec3d){0.0, 0.0, 0.0};
    cfg->up_vector = (Vec3d){0.0, 1.0, 0.0};
    cfg->distort = true;
    cfg->disk_inner_radius = 1.5;
    cfg->disk_outer_radius = 4.0;
    cfg->horizon_grid = true;
    cfg->disk_texture_mode = DT_NONE;
    cfg->sky_texture_mode = ST_NONE;
    cfg->disk_texture_path = NULL;
    cfg->sky_texture_path = NULL;
    cfg->disk_texture = NULL;
    cfg->sky_texture = NULL;
    cfg->sky_disk_ratio = 1.0;
    cfg->fog_do = true;
    cfg->fog_mult = 0.02;
    cfg->fog_skip = 1;
    cfg->blur_do = true;
    cfg->bloom_cut = 2.0;
    cfg->airy_bloom = true;
    cfg->airy_radius = 1.0;
    cfg->gain = 1.0;
    cfg->normalize = -1.0;
    cfg->srgb_out = true;
    cfg->srgb_in = true;
    cfg->disk_multiplier = 100.0;
    cfg->disk_intensity_do = true;
    cfg->redshift = 1.0;
    cfg->internal_override_res_ptr = NULL;

    const char *scene_fname = "scenes/default.scene";
    bool override_res = false;

    printf("Parsing command line arguments...\n");
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0) {
            cfg->lofi = true;
            printf("  Found -d: Enabling Lo-Fi mode defaults (will be applied during INI parse).\n");
        } else if (strcmp(argv[i], "--no-graph") == 0) {
            printf("  Found --no-graph: (Ignoring, graph not implemented).\n");
        } else if (strcmp(argv[i], "--no-display") == 0) {
            printf("  Found --no-display: (Ignoring, display not implemented).\n");
        } else if (strcmp(argv[i], "--no-shuffle") == 0) {
            printf("  Found --no-shuffle: (Note: Shuffling not implemented yet).\n");
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--no-bs") == 0) {
            printf("  Found -o/--no-bs: (Ignoring related display/graph flags).\n");
        } else if (strncmp(argv[i], "-c", 2) == 0 && strlen(argv[i]) > 2 && isdigit((unsigned char)argv[i][2])) { // Cast to unsigned char
            long csize = strtol(argv[i] + 2, NULL, 10);
            if (csize > 0) {
                cfg->chunk_size = (int)csize;
                printf("  Found -c: Setting chunk size to %d\n", cfg->chunk_size);
            } else {
                fprintf(stderr, "Warning: Invalid chunk size '%s'. Ignoring.\n", argv[i] + 2);
            }
        } else if (strncmp(argv[i], "-j", 2) == 0 && strlen(argv[i]) > 2 && isdigit((unsigned char)argv[i][2])) { // Cast to unsigned char
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
                return false;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unrecognized option: %s\n", argv[i]);
            return false;
        } else {
            scene_fname = argv[i];
            printf("  Found scene file argument: %s\n", scene_fname);
        }
    }

    if (access(scene_fname, F_OK) == -1) {
        fprintf(stderr, "Error: Scene file \"%s\" does not exist or is not accessible.\n", scene_fname);
        return false;
    }
    printf("Using scene file: %s\n", scene_fname);

    printf("Parsing INI file: %s...\n", scene_fname);
    cfg->internal_override_res_ptr = &override_res;
    if (ini_parse(scene_fname, scene_ini_callback, cfg) < 0) {
        fprintf(stderr, "Error: Can't load or parse scene file '%s'\n", scene_fname);
        cfg->internal_override_res_ptr = NULL;
        // Free any paths potentially strdup'd before error
        free((void*)cfg->disk_texture_path); cfg->disk_texture_path = NULL;
        free((void*)cfg->sky_texture_path); cfg->sky_texture_path = NULL;
        return false;
    }
    cfg->internal_override_res_ptr = NULL;
    printf("Finished parsing INI file.\n");

    // --- Load Textures Based on Config ---
    printf("Loading textures based on configuration...\n");
    Texture *original_sky_texture = NULL; // Temporary pointer
    if (cfg->disk_texture_mode == DT_TEXTURE) {
        if (cfg->disk_texture_path && strlen(cfg->disk_texture_path) > 0) {
            printf("  Loading disk texture: %s\n", cfg->disk_texture_path);
            cfg->disk_texture = load_texture(cfg->disk_texture_path);
            if (!cfg->disk_texture) {
                fprintf(stderr, "Warning: Failed to load disk texture '%s'. Check path and file.\n", cfg->disk_texture_path);
            }
        } else {
             fprintf(stderr, "Warning: Disktexture mode is TEXTURE, but no valid path was found or stored in config.\n");
        }
    }
    if (cfg->sky_texture_mode == ST_TEXTURE) {
        if (cfg->sky_texture_path && strlen(cfg->sky_texture_path) > 0) {
            printf("  Loading sky texture: %s\n", cfg->sky_texture_path);
            original_sky_texture = load_texture(cfg->sky_texture_path); // Load into temp var
            if (!original_sky_texture) {
                fprintf(stderr, "Warning: Failed to load sky texture '%s'. Check path and file.\n", cfg->sky_texture_path);
                // Fallback?
                // cfg->sky_texture_mode = ST_NONE;
            }
        } else {
            fprintf(stderr, "Warning: Skytexture mode is TEXTURE, but no valid path ('%s') found in config.\n", cfg->sky_texture_path);
            // Fallback
            // cfg->sky_texture_mode = ST_NONE;
        }
    }

    // --- Apply Sky Texture Resizing (if HiFi and texture loaded) ---
    if (!cfg->lofi && original_sky_texture) {
        printf("HiFi mode: Attempting to resize sky texture by 2.0x...\n");
        // Use the resize function from image.c
        cfg->sky_texture = resize_texture(original_sky_texture, 2.0f); // Assign resized to final config pointer

        if (cfg->sky_texture) {
                printf("  Sky texture resized successfully.\n");
                free_texture(original_sky_texture); // Free the original small texture
                original_sky_texture = NULL; // Avoid double free
        } else {
                fprintf(stderr, "Warning: Sky texture resizing failed. Using original texture.\n");
                cfg->sky_texture = original_sky_texture; // Resizing failed, assign original to final pointer
                original_sky_texture = NULL; // Avoid double free later
        }
    } else {
        // If LoFi or no original texture, just use the original (if any)
        cfg->sky_texture = original_sky_texture;
        original_sky_texture = NULL; // Avoid double free
    }
    // At this point, cfg->sky_texture points to the potentially resized texture,
    // or the original one, or NULL. original_sky_texture is NULL.

    // --- Compute Derived Values ---
    printf("Computing derived configuration values...\n");
    compute_derived_config(cfg);

     if (vec3d_norm(cfg->camera_pos) <= 1.0) {
        fprintf(stderr, "Error: Observer is inside the event horizon (r <= 1.0). Set Cameraposition further out.\n");
        // Free loaded resources before returning false
        free_config_textures(cfg); // Includes freeing paths
        return false;
    }

    printf("Configuration loaded successfully.\n");
    printf("Final Resolution: %dx%d\n", cfg->resolution[0], cfg->resolution[1]);
    printf("Iterations: %d, Step Size: %f\n", cfg->n_iterations, cfg->step_size);
    printf("Threads: %d, Chunk Size: %d\n", cfg->n_threads, cfg->chunk_size);
    // Optional: Print more final config values for debugging
    printf("Disk Mode: %d, Sky Mode: %d\n", cfg->disk_texture_mode, cfg->sky_texture_mode);
    if(cfg->disk_texture_path) printf("Disk Path: %s\n", cfg->disk_texture_path);
    if(cfg->sky_texture_path) printf("Sky Path: %s\n", cfg->sky_texture_path);

    return true;
}


// Definition for: compute_derived_config
void compute_derived_config(Config *cfg) {
    cfg->disk_inner_sqr = cfg->disk_inner_radius * cfg->disk_inner_radius;
    cfg->disk_outer_sqr = cfg->disk_outer_radius * cfg->disk_outer_radius;
    Vec3d front = vec3d_normalize(vec3d_sub(cfg->look_at, cfg->camera_pos));
    Vec3d left = vec3d_normalize(vec3d_cross(cfg->up_vector, front));
    Vec3d new_up = vec3d_normalize(vec3d_cross(front, left));
    cfg->view_matrix[0] = left;
    cfg->view_matrix[1] = new_up;
    cfg->view_matrix[2] = front;
}

// Definition for: free_config_textures
void free_config_textures(Config *cfg) {
     if (cfg->disk_texture) {
        printf("Freeing disk texture...\n");
        free_texture(cfg->disk_texture);
        cfg->disk_texture = NULL;
    }
    if (cfg->sky_texture) {
        printf("Freeing sky texture...\n");
        free_texture(cfg->sky_texture);
        cfg->sky_texture = NULL;
    }
    if (cfg->disk_texture_path) {
        printf("Freeing disk texture path string...\n");
        free((void*)cfg->disk_texture_path);
        cfg->disk_texture_path = NULL;
    }
     if (cfg->sky_texture_path) {
        printf("Freeing sky texture path string...\n");
        free((void*)cfg->sky_texture_path);
        cfg->sky_texture_path = NULL;
    }
}
