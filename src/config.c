#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "blackbody.h"
#include "config_defaults.h"
#include "image.h"
#include "ini.h"

// --- Function Prototypes ---
bool string_to_bool(const char *str);
bool parse_resolution(const char *res_str, int resolution[2]);
bool parse_vec3d(const char *str, Vec3d *vec);
bool parse_int_list(const char *str, int *arr, int expected_count);

// --- Helper Functions ---
bool parse_vec3d(const char *str, Vec3d *vec)
{
    if (!str || !vec) return false;
    double x, y, z;
    if (sscanf(str, "%lf,%lf,%lf", &x, &y, &z) == 3)
    {
        vec->x = x;
        vec->y = y;
        vec->z = z;
        return true;
    }
    return false;
}

bool parse_int_list(const char *str, int *arr, int expected_count)
{
    if (!str || !arr || expected_count <= 0) return false;

    char *str_copy = strdup(str);
    if (!str_copy) return false;

    char *token;
    const char *delim = ",";
    int count = 0;

    token = strtok(str_copy, delim);
    while (token != NULL && count < expected_count)
    {
        // Trim whitespace
        while (isspace((unsigned char)*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';

        if (strlen(token) > 0)
        {
            char *endptr;
            long val = strtol(token, &endptr, 10);
            if (*endptr == '\0') { arr[count++] = (int)val; }
            else
            {
                count = -1; // Error
                break;
            }
        }
        else
        {
            count = -1; // Error on empty token
            break;
        }
        token = strtok(NULL, delim);
    }

    free(str_copy);
    return count == expected_count;
}

bool string_to_bool(const char *str)
{
    if (!str) { return false; }

#ifdef _WIN32
    #define STRCASECMP _stricmp
#else
    #define STRCASECMP strcasecmp
#endif

    if (strcmp(str, "1") == 0 || STRCASECMP(str, "true") == 0 || STRCASECMP(str, "yes") == 0) { return true; }

#undef STRCASECMP
    return false;
}

bool parse_resolution(const char *res_str, int resolution[2])
{
    if (!res_str) return false;

    // Find the 'x' separator
    const char *x_pos = strchr(res_str, 'x');
    if (!x_pos || x_pos == res_str || *(x_pos + 1) == '\0') { return false; }

    int width = 0, height = 0;
    if (sscanf(res_str, "%dx%d", &width, &height) == 2)
    {
        if (width > 0 && height > 0)
        {
            resolution[0] = width;
            resolution[1] = height;
            return true;
        }
    }
    return false;
}


// --- INI Parsing Callback ---
static int scene_ini_callback(void *user, const char *section, const char *name, const char *value)
{
    IniParseUserData *user_data = (IniParseUserData *)user;
    Config *cfg = user_data->cfg;
    bool *override_res = user_data->override_res_flag;

    // --- Handle LoFi/HiFi Section Parameters ---
    if (strcmp(section, "lofi") == 0 || strcmp(section, "hifi") == 0)
    {
        // Skip processing if we're in the wrong section for our mode
        bool is_correct_section =
                (cfg->lofi && strcmp(section, "lofi") == 0) || (!cfg->lofi && strcmp(section, "hifi") == 0);

        if (!is_correct_section) { return 1; } // Skip this section

        if (strcmp(name, "Resolution") == 0)
        {
            if (!(*override_res) && !parse_int_list(value, cfg->resolution, 2))
            {
                fprintf(stderr, "Warning: Invalid format for Resolution '%s'. Using defaults.\n", value);
            }
            return 1;
        }
        if (strcmp(name, "Iterations") == 0)
        {
            cfg->n_iterations = atoi(value);
            return 1;
        }
        if (strcmp(name, "SSAA") == 0)
        {
            cfg->ssaa_level = (atoi(value) <= 0) ? 1 : atoi(value);
            return 1;
        }
        if (strcmp(name, "Stepsize") == 0)
        {
            cfg->step_size = atof(value);
            return 1;
        }
    }

    // --- Geometry Section ---
    else if (strcmp(section, "geometry") == 0)
    {
        if (strcmp(name, "Cameraposition") == 0)
        {
            if (!parse_vec3d(value, &cfg->camera_pos))
            {
                fprintf(stderr, "Warning: Invalid format for Cameraposition '%s'\n", value);
            }
        }
        else if (strcmp(name, "Fieldofview") == 0) { cfg->tan_fov = atof(value); }
        else if (strcmp(name, "Lookat") == 0)
        {
            if (!parse_vec3d(value, &cfg->look_at))
            {
                fprintf(stderr, "Warning: Invalid format for Lookat '%s'\n", value);
            }
        }
        else if (strcmp(name, "Upvector") == 0)
        {
            if (!parse_vec3d(value, &cfg->up_vector))
            {
                fprintf(stderr, "Warning: Invalid format for Upvector '%s'\n", value);
            }
        }
        else if (strcmp(name, "Distort") == 0) { cfg->distort = string_to_bool(value); }
        else if (strcmp(name, "Diskinner") == 0) { cfg->disk_inner_radius = atof(value); }
        else if (strcmp(name, "Diskouter") == 0) { cfg->disk_outer_radius = atof(value); }
    }

    // --- Materials Section ---
    else if (strcmp(section, "materials") == 0)
    {
        if (strcmp(name, "Horizongrid") == 0) { cfg->horizon_grid = string_to_bool(value); }
        else if (strcmp(name, "Disktexture") == 0)
        {
            free((void *)cfg->disk_texture_path); // Free old path if any
            cfg->disk_texture_path = NULL;

            if (strcasecmp(value, "none") == 0) { cfg->disk_texture_mode = DT_NONE; }
            else if (strcasecmp(value, "solid") == 0) { cfg->disk_texture_mode = DT_SOLID; }
            else if (strcasecmp(value, "grid") == 0) { cfg->disk_texture_mode = DT_GRID; }
            else if (strcasecmp(value, "blackbody") == 0) { cfg->disk_texture_mode = DT_BLACKBODY; }
            else if (strcasecmp(value, "texture") == 0)
            {
                cfg->disk_texture_mode = DT_TEXTURE;
                cfg->disk_texture_path = strdup(DEFAULT_DISK_TEXTURE_PATH);
                if (!cfg->disk_texture_path)
                {
                    fprintf(stderr, "Error: Memory allocation failed for default disk texture path\n");
                    return 0;
                }
                printf("Info: Using default disk texture path: %s\n", DEFAULT_DISK_TEXTURE_PATH);
            }
            else
            {
                // Assume it's a path for texture mode
                cfg->disk_texture_mode = DT_TEXTURE;
                cfg->disk_texture_path = strdup(value);
                if (!cfg->disk_texture_path)
                {
                    fprintf(stderr, "Error: Memory allocation failed for disk texture path\n");
                    return 0;
                }
                printf("Info: Assuming Disktexture value '%s' is a path for DT_TEXTURE mode.\n", value);
            }
        }
        else if (strcmp(name, "Skytexture") == 0)
        {
            free((void *)cfg->sky_texture_path); // Free old path if any
            cfg->sky_texture_path = NULL;

            if (strcasecmp(value, "none") == 0) { cfg->sky_texture_mode = ST_NONE; }
            else if (strcasecmp(value, "final") == 0) { cfg->sky_texture_mode = ST_FINAL; }
            else if (strcasecmp(value, "texture") == 0)
            {
                cfg->sky_texture_mode = ST_TEXTURE;
                cfg->sky_texture_path = strdup(DEFAULT_SKY_TEXTURE_PATH);
                if (!cfg->sky_texture_path) { return 0; }
                printf("Info: Using default sky texture path: %s\n", DEFAULT_SKY_TEXTURE_PATH);
            }
            else
            {
                // Assume it's a path for texture mode
                cfg->sky_texture_mode = ST_TEXTURE;
                cfg->sky_texture_path = strdup(value);
                if (!cfg->sky_texture_path) { return 0; }
                printf("Info: Using custom sky texture path: %s\n", value);
            }
        }
        else if (strcmp(name, "Blackbodyramp") == 0)
        {
            free(cfg->blackbody_ramp_path); // Free previous path (default)
            cfg->blackbody_ramp_path = NULL;

            if (value && strlen(value) > 0)
            {
                cfg->blackbody_ramp_path = strdup(value);    // Use custom path
                if (!cfg->blackbody_ramp_path) { return 0; } // Allocation error
                printf("Info: Using custom blackbody ramp path: %s\n", value);
            }
            else
            {
                // If value is empty or missing, revert to default explicitly
                cfg->blackbody_ramp_path = strdup(DEFAULT_BLACKBODY_RAMP_PATH);
                if (!cfg->blackbody_ramp_path) { return 0; } // Allocation error
                printf("Info: Blackbodyramp value invalid/empty, using default: %s\n", DEFAULT_BLACKBODY_RAMP_PATH);
            }
        }
        else if (strcmp(name, "Skydiskratio") == 0) { cfg->sky_disk_ratio = atof(value); }
        else if (strcmp(name, "Fogdo") == 0) { cfg->fog_do = string_to_bool(value); }
        else if (strcmp(name, "Fogmult") == 0) { cfg->fog_mult = atof(value); }
        else if (strcmp(name, "Fogskip") == 0) { cfg->fog_skip = (atoi(value) <= 0) ? 1 : atoi(value); }
        else if (strcmp(name, "Blurdo") == 0) { cfg->blur_do = string_to_bool(value); }
        else if (strcmp(name, "Airy_bloom") == 0) { cfg->airy_bloom = string_to_bool(value); }
        else if (strcmp(name, "Airy_radius") == 0) { cfg->airy_radius = atof(value); }
        else if (strcmp(name, "Gain") == 0) { cfg->gain = atof(value); }
        else if (strcmp(name, "ACESExposure") == 0) { cfg->aces_exposure = atof(value); }
        else if (strcmp(name, "Normalize") == 0) { cfg->normalize = atof(value); }
        else if (strcmp(name, "sRGBOut") == 0) { cfg->srgb_out = string_to_bool(value); }
        else if (strcmp(name, "sRGBIn") == 0) { cfg->srgb_in = string_to_bool(value); }
        else if (strcmp(name, "Diskmultiplier") == 0) { cfg->disk_multiplier = atof(value); }
        else if (strcmp(name, "Diskintensitydo") == 0) { cfg->disk_intensity_do = string_to_bool(value); }
        else if (strcmp(name, "Redshift") == 0) { cfg->redshift = atof(value); }
    }

    return 1; // Success (even for unknown keys)
}


// --- Main Config Loading Function ---
bool load_config(int argc, char *argv[], Config *cfg)
{
    // Initialize with defaults
    cfg->resolution[0] = DEFAULT_RESOLUTION_WIDTH;
    cfg->resolution[1] = DEFAULT_RESOLUTION_HEIGHT;
    cfg->n_iterations = DEFAULT_ITERATIONS;
    cfg->ssaa_level = DEFAULT_SSAA_LEVEL;
    cfg->step_size = DEFAULT_STEPSIZE;
    cfg->n_threads = DEFAULT_THREADS;
    cfg->chunk_size = DEFAULT_CHUNKSIZE;
    cfg->lofi = DEFAULT_LOFI;
    cfg->method = DEFAULT_METHOD;

    cfg->camera_pos = (Vec3d)DEFAULT_CAMERA_POS;
    cfg->tan_fov = DEFAULT_TAN_FOV;
    cfg->look_at = (Vec3d)DEFAULT_LOOK_AT;
    cfg->up_vector = (Vec3d)DEFAULT_UP_VECTOR;
    cfg->distort = DEFAULT_DISTORT;
    cfg->disk_inner_radius = DEFAULT_DISK_INNER_RADIUS;
    cfg->disk_outer_radius = DEFAULT_DISK_OUTER_RADIUS;

    cfg->horizon_grid = DEFAULT_HORIZON_GRID;
    cfg->disk_texture_mode = DEFAULT_DISK_TEXTURE_MODE;
    cfg->sky_texture_mode = DEFAULT_SKY_TEXTURE_MODE;
    cfg->disk_texture_path = strdup(DEFAULT_DISK_TEXTURE_PATH);
    cfg->sky_texture_path = strdup(DEFAULT_SKY_TEXTURE_PATH);
    cfg->disk_texture = NULL;
    cfg->sky_texture = NULL;
    cfg->sky_disk_ratio = DEFAULT_SKY_DISK_RATIO;
    cfg->fog_do = DEFAULT_FOG_DO;
    cfg->fog_mult = DEFAULT_FOG_MULT;
    cfg->fog_skip = DEFAULT_FOG_SKIP;
    cfg->blur_do = DEFAULT_BLUR_DO;
    cfg->airy_bloom = DEFAULT_AIRY_BLOOM;
    cfg->airy_radius = DEFAULT_AIRY_RADIUS;
    cfg->gain = DEFAULT_GAIN;
    cfg->aces_exposure = DEFAULT_ACES_EXPOSURE;
    cfg->normalize = DEFAULT_NORMALIZE;
    cfg->srgb_out = DEFAULT_SRGB_OUT;
    cfg->srgb_in = DEFAULT_SRGB_IN;

    cfg->blackbody_ramp_path = strdup(DEFAULT_BLACKBODY_RAMP_PATH);
    cfg->blackbody_ramp_data = NULL;
    cfg->blackbody_ramp_size = 0;
    cfg->disk_multiplier = DEFAULT_DISK_MULTIPLIER;
    cfg->disk_intensity_do = DEFAULT_DISK_INTENSITY_DO;
    cfg->redshift = DEFAULT_REDSHIFT;

    const char *scene_fname = DEFAULT_SCENE_FILENAME;
    bool override_res = false;

    // Check initial allocations
    if (!cfg->disk_texture_path || !cfg->sky_texture_path || !cfg->blackbody_ramp_path)
    {
        fprintf(stderr, "Error: Failed to allocate memory for default paths during initialization.\n");
        // Need to free whatever *was* allocated before returning
        free(cfg->disk_texture_path);
        free(cfg->sky_texture_path);
        free(cfg->blackbody_ramp_path);
        return false;
    }

    // Parse command line arguments
    printf("Parsing command line arguments...\n");
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-d") == 0)
        {
            cfg->lofi = true;
            printf("  Found -d: Enabling Lo-Fi mode defaults\n");
        }
        else if (strcmp(argv[i], "--no-graph") == 0 || strcmp(argv[i], "--no-display") == 0 ||
                 strcmp(argv[i], "--no-shuffle") == 0 || strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--no-bs") == 0)
        {
            printf("  Found %s: (Ignoring, not implemented or relevant)\n", argv[i]);
        }
        else if (strncmp(argv[i], "-c", 2) == 0 && strlen(argv[i]) > 2 && isdigit((unsigned char)argv[i][2]))
        {
            long csize = strtol(argv[i] + 2, NULL, 10);
            if (csize > 0)
            {
                cfg->chunk_size = (int)csize;
                printf("  Found -c: Setting chunk size to %d\n", cfg->chunk_size);
            }
            else { fprintf(stderr, "Warning: Invalid chunk size '%s'. Ignoring.\n", argv[i] + 2); }
        }
        else if (strncmp(argv[i], "-j", 2) == 0 && strlen(argv[i]) > 2 && isdigit((unsigned char)argv[i][2]))
        {
            long threads = strtol(argv[i] + 2, NULL, 10);
            if (threads > 0)
            {
                cfg->n_threads = (int)threads;
                printf("  Found -j: Setting thread count to %d\n", cfg->n_threads);
            }
            else { fprintf(stderr, "Warning: Invalid thread count '%s'. Ignoring.\n", argv[i] + 2); }
        }
        else if (strncmp(argv[i], "-r", 2) == 0 && strlen(argv[i]) > 2)
        {
            if (parse_resolution(argv[i] + 2, cfg->resolution))
            {
                printf("  Found -r: Setting resolution override to %dx%d\n", cfg->resolution[0], cfg->resolution[1]);
                override_res = true;
            }
            else
            {
                fprintf(stderr, "Error: Invalid resolution format in '%s'. Use WxH (e.g., -r640x480).\n", argv[i]);
                return false;
            }
        }
        else if (argv[i][0] == '-')
        {
            fprintf(stderr, "Error: Unrecognized option: %s\n", argv[i]);
            return false;
        }
        else
        {
            scene_fname = argv[i];
            printf("  Found scene file argument: %s\n", scene_fname);
        }
    }

    // Check if scene file exists
    if (access(scene_fname, F_OK) == -1)
    {
        fprintf(stderr, "Error: Scene file \"%s\" does not exist or is not accessible.\n", scene_fname);
        return false;
    }
    printf("Using scene file: %s\n", scene_fname);

    // Parse INI file
    printf("Parsing INI file: %s...\n", scene_fname);
    IniParseUserData user_data = {cfg, &override_res};
    if (ini_parse(scene_fname, scene_ini_callback, &user_data) < 0)
    {
        fprintf(stderr, "Error: Can't load or parse scene file '%s'\n", scene_fname);
        free_config_textures(cfg); // Free everything allocated so far
        return false;
    }
    printf("Finished parsing INI file.\n");

    // Load textures based on config
    printf("Loading textures based on configuration...\n");
    Texture *original_sky_texture = NULL;

    if (cfg->disk_texture_mode == DT_TEXTURE)
    {
        if (cfg->disk_texture_path && strlen(cfg->disk_texture_path) > 0)
        {
            printf("  Loading disk texture: %s\n", cfg->disk_texture_path);
            cfg->disk_texture = load_texture(cfg->disk_texture_path);
            if (!cfg->disk_texture)
            {
                fprintf(stderr, "Warning: Failed to load disk texture '%s'. Check path and file.\n",
                        cfg->disk_texture_path);
            }
        }
        else { fprintf(stderr, "Warning: Disk texture mode is TEXTURE, but no valid path was found.\n"); }
    }

    if (cfg->sky_texture_mode == ST_TEXTURE)
    {
        if (cfg->sky_texture_path && strlen(cfg->sky_texture_path) > 0)
        {
            printf("  Loading sky texture: %s\n", cfg->sky_texture_path);
            original_sky_texture = load_texture(cfg->sky_texture_path);
            if (!original_sky_texture)
            {
                fprintf(stderr, "Warning: Failed to load sky texture '%s'. Check path and file.\n",
                        cfg->sky_texture_path);
            }
        }
        else { fprintf(stderr, "Warning: Sky texture mode is TEXTURE, but no valid path was found.\n"); }
    }

    // Apply sky texture resizing (if HiFi and texture loaded)
    if (!cfg->lofi && original_sky_texture)
    {
        printf("HiFi mode: Resizing sky texture by 2.0x for higher quality...\n");
        cfg->sky_texture = resize_texture(original_sky_texture, 2.0f);

        if (cfg->sky_texture)
        {
            printf("  Sky texture resized successfully.\n");
            free_texture(original_sky_texture);
            original_sky_texture = NULL;
        }
        else
        {
            fprintf(stderr, "Warning: Sky texture resizing failed. Using original texture.\n");
            cfg->sky_texture = original_sky_texture;
            original_sky_texture = NULL;
        }
    }
    else
    {
        cfg->ssaa_level = 1; // Reset SSAA level if not HiFi
        printf("LoFi mode: Skipping sky texture resizing.\n");
        cfg->sky_texture = original_sky_texture;
        original_sky_texture = NULL;
    }

    // Load Blackbody Ramp Conditionally
    if (cfg->disk_texture_mode == DT_BLACKBODY)
    {
        if (cfg->blackbody_ramp_path && strlen(cfg->blackbody_ramp_path) > 0)
        {
            printf("Loading blackbody ramp (required by Disktexture mode): %s...\n", cfg->blackbody_ramp_path);
            // Pass pointers to store the results in the config struct
            if (!load_blackbody_ramp_from_file(cfg->blackbody_ramp_path, &cfg->blackbody_ramp_data,
                                               &cfg->blackbody_ramp_size))
            {
                fprintf(stderr, "Error: Failed to load required blackbody ramp.\n");
                free_config_textures(cfg); // Cleans up textures and paths
                return false;
            }
        }
        else
        {
            // This case should ideally not happen due to default path logic, but check anyway
            fprintf(stderr, "Error: Disktexture mode is BLACKBODY, but no valid ramp path was configured.\n");
            free_config_textures(cfg);
            return false;
        }
    }
    else { printf("Disktexture mode is not BLACKBODY, skipping blackbody ramp loading.\n"); }

    // Compute derived values
    printf("Computing derived configuration values...\n");
    compute_derived_config(cfg);

    // Final validation
    if (vec3d_norm(cfg->camera_pos) <= 1.0)
    {
        fprintf(stderr, "Error: Observer is inside the event horizon (r <= 1.0). Set Cameraposition further out.\n");
        free_config_textures(cfg);
        return false;
    }

    // Print summary of final configuration
    printf("Configuration loaded successfully.\n");
    printf("Final Resolution: %dx%d\n", cfg->resolution[0], cfg->resolution[1]);
    printf("Iterations: %d, Step Size: %f\n", cfg->n_iterations, cfg->step_size);
    printf("SSAA Level: %d\n", cfg->ssaa_level);
    printf("Threads: %d, Chunk Size: %d\n", cfg->n_threads, cfg->chunk_size);
    printf("Disk Mode: %d, Sky Mode: %d\n", cfg->disk_texture_mode, cfg->sky_texture_mode);
    if (cfg->disk_texture_path) printf("Disk Path: %s\n", cfg->disk_texture_path);
    if (cfg->sky_texture_path) printf("Sky Path: %s\n", cfg->sky_texture_path);
    if (cfg->blackbody_ramp_path) printf("Blackbody Ramp Path: %s\n", cfg->blackbody_ramp_path);
    if (cfg->blackbody_ramp_data) printf("Blackbody Ramp Data: Loaded (%d samples)\n", cfg->blackbody_ramp_size);

    return true;
}


// Calculate derived configuration values
void compute_derived_config(Config *cfg)
{
    // Calculate squared radii for faster checks
    cfg->disk_inner_sqr = cfg->disk_inner_radius * cfg->disk_inner_radius;
    cfg->disk_outer_sqr = cfg->disk_outer_radius * cfg->disk_outer_radius;

    // Create view matrix from camera parameters
    Vec3d front = vec3d_normalize(vec3d_sub(cfg->look_at, cfg->camera_pos));
    Vec3d left = vec3d_normalize(vec3d_cross(cfg->up_vector, front));
    Vec3d new_up = vec3d_normalize(vec3d_cross(front, left));

    cfg->view_matrix[0] = left;
    cfg->view_matrix[1] = new_up;
    cfg->view_matrix[2] = front;
}


// Free allocated resources
void free_config_textures(Config *cfg)
{
    // Free textures if loaded
    if (cfg->disk_texture)
    {
        free_texture(cfg->disk_texture);
        cfg->disk_texture = NULL;
    }
    if (cfg->sky_texture)
    {
        free_texture(cfg->sky_texture);
        cfg->sky_texture = NULL;
    }

    // Free path strings (they should have been allocated initially)
    if (cfg->disk_texture_path)
    {
        free(cfg->disk_texture_path);
        cfg->disk_texture_path = NULL;
    }
    if (cfg->sky_texture_path)
    {
        free(cfg->sky_texture_path);
        cfg->sky_texture_path = NULL;
    }
    if (cfg->blackbody_ramp_path)
    {
        free(cfg->blackbody_ramp_path);
        cfg->blackbody_ramp_path = NULL;
    }

    // Free blackbody ramp data
    if (cfg->blackbody_ramp_data)
    {
        printf("Freeing blackbody ramp data (%d samples)...\n", cfg->blackbody_ramp_size);
        free(cfg->blackbody_ramp_data);
        cfg->blackbody_ramp_data = NULL;
        cfg->blackbody_ramp_size = 0;
    }
}
