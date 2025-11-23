#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blackbody.h"
#include "config_default_paths.h"
#include "image.h"
#include "ini.h"
#if defined(_WIN32)
    #include <io.h>
    #ifndef F_OK
        #define F_OK 0
    #endif
    #define SSCANF     sscanf_s
    #define STRDUP     _strdup
    #define STRTOK     strtok_s
    #define STRCASECMP _stricmp
    #define ACCESS     _access
#else
    #include <unistd.h>
    #define SSCANF     sscanf
    #define STRDUP     strdup
    #define STRTOK     strtok_r
    #define STRCASECMP strcasecmp
    #define ACCESS     access
#endif

// --- Function Prototypes ---
bool string_to_bool(const char *str);
bool parse_resolution(const char *res_str, int resolution[2]);
bool parse_vec3d(const char *str, Vec3d *vec);
bool parse_int_list(const char *str, int *arr, int expected_count);

// --- Helper Functions ---
bool string_to_bool(const char *str)
{
    if (!str) { return false; }
    if (strcmp(str, "1") == 0 || STRCASECMP(str, "true") == 0 || STRCASECMP(str, "yes") == 0) { return true; }
    return false;
}

// -- Parser Functions --
bool parse_vec3d(const char *str, Vec3d *vec)
{
    if (!str || !vec) { return false; }
    double x, y, z;
    if (SSCANF(str, "%lf,%lf,%lf", &x, &y, &z) == 3)
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
    if (!str || !arr || expected_count <= 0) { return false; }

    char *str_copy = STRDUP(str);
    if (!str_copy) { return false; }

    const char *delimiter = ",";
    int count = 0;

    char *save_ptr = NULL;

    char *token = STRTOK(str_copy, delimiter, &save_ptr);
    while (token != NULL && count < expected_count)
    {
        // Trim whitespace
        while (isspace((unsigned char)*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';

        if (strlen(token) > 0)
        {
            char *end_ptr;
            long val = strtol(token, &end_ptr, 10);
            if (*end_ptr == '\0') { arr[count++] = (int)val; }
            else
            {
                printf("Error parsing token: '%s', non-integer part: '%s'\n", token, end_ptr);
                count = -1;
                break;
            }
        }
        else
        {
            printf("Error: empty token found after trim.\n");
            count = -1;
            break;
        }
        token = STRTOK(NULL, delimiter, &save_ptr);
    }

    free(str_copy);
    return count == expected_count;
}

bool parse_resolution(const char *res_str, int resolution[2])
{
    if (!res_str) return false;

    // Find the 'x' separator
    const char *x_pos = strchr(res_str, 'x');
    if (!x_pos || x_pos == res_str || *(x_pos + 1) == '\0') { return false; }

    int width = 0, height = 0;
    if (SSCANF(res_str, "%dx%d", &width, &height) == 2)
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

static bool parse_enum_value(const char *value, int *mode_out, const char **names, int num_names)
{
    for (int i = 0; i < num_names; i++)
    {
        if (STRCASECMP(value, names[i]) == 0)
        {
            *mode_out = i;
            return true;
        }
    }
    return false;
}

static int parse_disk_texture_mode(Config *cfg, const char *value)
{
    int num_names = sizeof(disk_texture_mode_names) / sizeof(char *);
    int mode;
    free(cfg->disk_texture_path);
    cfg->disk_texture_path = NULL;
    if (parse_enum_value(value, &mode, disk_texture_mode_names, num_names))
    {
        cfg->disk_texture_mode = (DiskTextureMode)mode;
        if (cfg->disk_texture_mode == DT_TEXTURE)
        {
            cfg->disk_texture_path = STRDUP(DEFAULT_DISK_TEXTURE_PATH);
            if (!cfg->disk_texture_path) return 0;
            printf("  Info: Default disk path: %s\n", DEFAULT_DISK_TEXTURE_PATH);
        }
        return 1;
    }
    cfg->disk_texture_mode = DT_TEXTURE;
    cfg->disk_texture_path = STRDUP(value);
    if (!cfg->disk_texture_path) return 0;
    printf("  Info: Custom disk path: %s\n", value);
    return 1;
}

static int parse_sky_texture_mode(Config *cfg, const char *value)
{
    int num_names = sizeof(sky_texture_mode_names) / sizeof(char *);
    int mode;
    free(cfg->sky_texture_path);
    cfg->sky_texture_path = NULL;
    if (parse_enum_value(value, &mode, sky_texture_mode_names, num_names))
    {
        cfg->sky_texture_mode = (SkyTextureMode)mode;
        if (cfg->sky_texture_mode == ST_TEXTURE)
        {
            cfg->sky_texture_path = STRDUP(DEFAULT_SKY_TEXTURE_PATH);
            if (!cfg->sky_texture_path) return 0;
            printf("  Info: Default sky path: %s\n", DEFAULT_SKY_TEXTURE_PATH);
        }
        return 1;
    }
    cfg->sky_texture_mode = ST_TEXTURE;
    cfg->sky_texture_path = STRDUP(value);
    if (!cfg->sky_texture_path) return 0;
    printf("  Info: Custom sky path: %s\n", value);
    return 1;
}

static int parse_blackbody_ramp_path(Config *cfg, const char *value)
{
    free(cfg->blackbody_ramp_path);
    if (value && strlen(value) > 0)
    {
        cfg->blackbody_ramp_path = STRDUP(value);
        if (!cfg->blackbody_ramp_path) return 0;
        printf("  Info: Custom blackbody path: %s\n", value);
    }
    else
    {
        cfg->blackbody_ramp_path = STRDUP(DEFAULT_BLACKBODY_RAMP_PATH);
        if (!cfg->blackbody_ramp_path) return 0;
        printf("  Warning: Empty blackbody path, using default: %s\n", DEFAULT_BLACKBODY_RAMP_PATH);
    }
    return 1;
}

// --- INI Parsing Callback ---
static int scene_ini_callback(void *user, const char *section, const char *name, const char *value)
{
    IniParseUserData *user_data = (IniParseUserData *)user;
    Config *cfg = user_data->cfg;
    bool *override_res = user_data->override_res_flag;

    // _PARSE macros for different types, used in FIELD_DEF below (initMacro##_PARSE)
    #define INIT_INT_PARSE(field)    cfg->field = atoi(value)
    #define INIT_DOUBLE_PARSE(field) cfg->field = atof(value)
    #define INIT_BOOL_PARSE(field)   cfg->field = string_to_bool(value)
    #define INIT_VEC3_PARSE(field) \
        if (!parse_vec3d(value, &cfg->field)) { fprintf(stderr, "  Warning: Invalid vec3 '%s' for %s\n", value, #field); }
    #define INIT_INT_ARRAY2_PARSE(field) \
        if (!parse_int_list(value, cfg->field, 2)) { fprintf(stderr, "  Warning: Invalid int list '%s' for %s\n", value, #field); }
    #define INIT_STRING_PARSE(field)    \
        {                               \
            free(cfg->field);           \
            cfg->field = STRDUP(value); \
            if (!cfg->field) return 0;  \
        }
    #define INIT_ENUM_PARSE(field) /* processed through strcmp in FIELD_DEF */
    #define INIT_NULL_PARSE(field) /* skip, not from ini */

    if (strcmp(section, "lofi") == 0 || strcmp(section, "hifi") == 0)
    {
        bool is_correct_section = (cfg->lofi && strcmp(section, "lofi") == 0) || (!cfg->lofi && strcmp(section, "hifi") == 0);
        if (!is_correct_section) return 1;

        #define FIELD_DEF(fieldName, cfgKey, initMacro, defLiteral)               \
            if (cfgKey[0] != '\0' && strcmp(name, cfgKey) == 0)                   \
            {                                                                     \
                if (strcmp(cfgKey, "Resolution") == 0 && *override_res) return 1; \
                if (strcmp(cfgKey, "SSAA") == 0)                                  \
                {                                                                 \
                    cfg->ssaa_level = (atoi(value) <= 0) ? 1 : atoi(value);       \
                    return 1;                                                     \
                }                                                                 \
                initMacro##_PARSE(fieldName);                                     \
                return 1;                                                         \
            }

        #define SEC_LOFIHIFI
        #include "x_config_fields.h"
    }
    else if (strcmp(section, "geometry") == 0)
    {
        #define FIELD_DEF(fieldName, cfgKey, initMacro, defLiteral) \
            if (cfgKey[0] != '\0' && strcmp(name, cfgKey) == 0)     \
            {                                                       \
                initMacro##_PARSE(fieldName);                       \
                return 1;                                           \
            }

        #define SEC_GEOMETRY
        #include "x_config_fields.h"
    }
    else if (strcmp(section, "materials") == 0)
    {
        #define FIELD_DEF(fieldName, cfgKey, initMacro, defLiteral)                                         \
            if (cfgKey[0] != '\0' && strcmp(name, cfgKey) == 0)                                             \
            {                                                                                               \
                if (strcmp(cfgKey, "Disktexture") == 0) { return parse_disk_texture_mode(cfg, value); }     \
                if (strcmp(cfgKey, "Skytexture") == 0) { return parse_sky_texture_mode(cfg, value); }       \
                if (strcmp(cfgKey, "Blackbodyramp") == 0) { return parse_blackbody_ramp_path(cfg, value); } \
                initMacro##_PARSE(fieldName);                                                               \
                return 1;                                                                                   \
            }

        #define SEC_MATERIALS
        #include "x_config_fields.h"
    }

    #undef INIT_INT_PARSE
    #undef INIT_DOUBLE_PARSE
    #undef INIT_BOOL_PARSE
    #undef INIT_VEC3_PARSE
    #undef INIT_INT_ARRAY2_PARSE
    #undef INIT_STRING_PARSE
    #undef INIT_ENUM_PARSE
    #undef INIT_NULL_PARSE

    fprintf(stderr, "  Warning: Unknown '%s=%s' in [%s]\n", name, value, section);
    return 1;
}

// --- Main Config Loading Function ---
bool load_config(int argc, char *argv[], Config *cfg)
{
    if (!cfg) { return false; }

    // Initialize with defaults
    #define INIT_STRING(fieldName, cfgKey, defLiteral) cfg->fieldName = STRDUP(defLiteral)
    #define INIT_INT(fieldName, cfgKey, defLiteral)    cfg->fieldName = defLiteral
    #define INIT_NULL   INIT_INT
    #define INIT_ENUM   INIT_INT
    #define INIT_DOUBLE INIT_INT
    #define INIT_BOOL   INIT_INT
    #define INIT_VEC3   INIT_INT
    #define INIT_INT_ARRAY2(fieldName, cfgKey, defLiteral) \
        do {                                               \
            cfg->fieldName[0] = defLiteral[0];             \
            cfg->fieldName[1] = defLiteral[1];             \
        } while (0)

    #define FIELD_DEF(fieldName, cfgKey, initMacro, defLiteral) initMacro(fieldName, cfgKey, defLiteral)

    #define SEC_ALL
    #include "x_config_fields.h"

    const char *scene_filename = DEFAULT_SCENE_FILENAME;
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
        else if (strcmp(argv[i], "--no-graph") == 0 || strcmp(argv[i], "--no-display") == 0 || strcmp(argv[i], "--no-shuffle") == 0 ||
                 strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--no-bs") == 0)
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
            scene_filename = argv[i];
            printf("  Found scene file argument: %s\n", scene_filename);
        }
    }

    // Check if scene file exists
    if (ACCESS(scene_filename, F_OK) == -1)
    {
        fprintf(stderr, "Error: Scene file \"%s\" does not exist or is not accessible.\n", scene_filename);
        return false;
    }

    // Set scene file path and base name
    if (cfg->scene_file_path) { free(cfg->scene_file_path); }
    cfg->scene_file_path = STRDUP(scene_filename);
    if (!cfg->scene_file_path)
    {
        fprintf(stderr, "Error: Memory allocation failed for scene file path.\n");
        return false;
    }

    // Extract base name from scene file path
    const char *name_start_ptr = strrchr(scene_filename, '/');
#ifdef _WIN32
    if (!name_start_ptr) { name_start_ptr = strrchr(scene_filename, '\\'); }
#endif

    if (name_start_ptr) { name_start_ptr++; } // Skip the slash
    else { name_start_ptr = scene_filename; } // No slash found, use the whole string

    if (cfg->scene_base_name) { free(cfg->scene_base_name); }
    cfg->scene_base_name = STRDUP(name_start_ptr);
    if (!cfg->scene_base_name)
    {
        fprintf(stderr, "Error: Memory allocation failed for scene base name.\n");
        free(cfg->scene_file_path);
        return false;
    }
    char *dot = strrchr(cfg->scene_base_name, '.');
    if (dot && dot != cfg->scene_base_name && (strcmp(dot, ".scene") == 0 || strcmp(dot, ".SCENE") == 0)) { *dot = '\0'; }

    printf("  Using scene file: %s\n", cfg->scene_file_path);

    // Parse INI file
    printf("Parsing INI file: %s...\n", cfg->scene_file_path);
    IniParseUserData user_data = {cfg, &override_res};
    if (ini_parse(cfg->scene_file_path, scene_ini_callback, &user_data) < 0)
    {
        fprintf(stderr, "! Error: Can't load or parse scene file '%s'\n", cfg->scene_file_path);
        free_config_textures(cfg);
        return false;
    }
    printf("  Finished parsing INI file.\n");

    // Load textures based on config
    if (cfg->disk_texture_mode == DT_TEXTURE || cfg->sky_texture_mode == ST_TEXTURE)
    {
        printf("Loading textures based on configuration...\n");
    }

    Texture *original_sky_texture = NULL;

    if (cfg->disk_texture_mode == DT_TEXTURE)
    {
        if (cfg->disk_texture_path && strlen(cfg->disk_texture_path) > 0)
        {
            printf("  Loading disk texture: %s\n", cfg->disk_texture_path);
            cfg->disk_texture = load_texture(cfg->disk_texture_path);
            if (!cfg->disk_texture)
            {
                fprintf(stderr, "  Warning: Failed to load disk texture '%s'. Check path and file.\n", cfg->disk_texture_path);
            }
        }
        else { fprintf(stderr, "  Warning: Disk texture mode is TEXTURE, but no valid path was found.\n"); }
    }

    if (cfg->sky_texture_mode == ST_TEXTURE)
    {
        if (cfg->sky_texture_path && strlen(cfg->sky_texture_path) > 0)
        {
            printf("  Loading sky texture: %s\n", cfg->sky_texture_path);
            original_sky_texture = load_texture(cfg->sky_texture_path);
            if (!original_sky_texture)
            {
                fprintf(stderr, "  Warning: Failed to load sky texture '%s'. Check path and file.\n", cfg->sky_texture_path);
            }
        }
        else { fprintf(stderr, "  Warning: Sky texture mode is TEXTURE, but no valid path was found.\n"); }
    }

    // Apply sky texture resizing (if HiFi and texture loaded)
    if (!cfg->lofi && original_sky_texture)
    {
        printf("  HiFi mode: Resizing sky texture by 2.0x for higher quality...\n");
        cfg->sky_texture = resize_texture(original_sky_texture, 2.0f);

        if (cfg->sky_texture)
        {
            printf("    Sky texture resized successfully.\n");
            free_texture(original_sky_texture);
            original_sky_texture = NULL;
        }
        else
        {
            fprintf(stderr, "    Warning: Sky texture resizing failed. Using original texture.\n");
            cfg->sky_texture = original_sky_texture;
            original_sky_texture = NULL;
        }
    }
    else
    {
        printf("  LoFi mode: Skipping sky texture resizing.\n");
        cfg->sky_texture = original_sky_texture;
        original_sky_texture = NULL;
    }

    // Adjust SSAA level based on LoFi/HiFi mode
    printf("Adjusting SSAA level based on mode...\n");
    if (cfg->lofi)
    {
        printf("  LoFi mode: Setting SSAA level to 1.\n");
        cfg->ssaa_level = 1;
    }
    else
    {
        printf("  HiFi mode: SSAA level is set to %d.\n", cfg->ssaa_level);
    }

    // Load Blackbody Ramp Conditionally
    if (cfg->disk_texture_mode == DT_BLACKBODY)
    {
        if (cfg->blackbody_ramp_path && strlen(cfg->blackbody_ramp_path) > 0)
        {
            printf("Loading blackbody ramp (required by Disktexture mode): %s...\n", cfg->blackbody_ramp_path);
            // Pass pointers to store the results in the config struct
            if (!load_blackbody_ramp_from_file(cfg->blackbody_ramp_path, &cfg->blackbody_ramp_data, &cfg->blackbody_ramp_size))
            {
                fprintf(stderr, "! Error: Failed to load required blackbody ramp.\n");
                free_config_textures(cfg);
                return false;
            }
        }
        else
        {
            // This case should ideally not happen due to default path logic, but check anyway
            fprintf(stderr, "! Error: Disktexture mode is BLACKBODY, but no valid ramp path was configured.\n");
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
        fprintf(stderr, "! Error: Observer is inside the event horizon (r <= 1.0). Set Cameraposition further out.\n");
        free_config_textures(cfg);
        return false;
    }

    // Print summary of final configuration
    // TODO: Make separate full summary function using FIELD_DEF for consistency
    printf("Configuration loaded successfully:\n");
    printf("  Scene base name: %s\n", cfg->scene_base_name);
    printf("  Final Resolution: %dx%d\n", cfg->resolution[0], cfg->resolution[1]);
    printf("  Iterations: %d, Step Size: %f\n", cfg->n_iterations, cfg->step_size);
    printf("  SSAA Level: %d\n", cfg->ssaa_level);
    printf("  Threads: %d, Chunk Size: %d\n", cfg->n_threads, cfg->chunk_size);
    printf("  Disk Mode: %d, Sky Mode: %d\n", cfg->disk_texture_mode, cfg->sky_texture_mode);
    if (cfg->disk_texture_path) printf("  Disk Path: %s\n", cfg->disk_texture_path);
    if (cfg->sky_texture_path) printf("  Sky Path: %s\n", cfg->sky_texture_path);
    if (cfg->blackbody_ramp_path) printf("  Blackbody Ramp Path: %s\n", cfg->blackbody_ramp_path);
    if (cfg->blackbody_ramp_data) printf("  Blackbody Ramp Data: Loaded (%d samples)\n", cfg->blackbody_ramp_size);
    if (cfg->disk_texture_mode == DT_BLACKBODY)
    {
        printf("  Blackbody:\n");
        printf("    Disk Multiplier: %f\n", cfg->disk_multiplier);
        printf("    Redshift: %f\n", cfg->redshift);
        if (cfg->disk_add_structure)
        {
            printf("    Disk Structure: Enabled\n");
            printf("     Spiral Arms: %d\n", cfg->disk_structure_spiral_arms);
            printf("     Rings Frequency: (%f, %f, %f)\n",
                                            cfg->disk_structure_rings_freq.x,
                                            cfg->disk_structure_rings_freq.y,
                                            cfg->disk_structure_rings_freq.z);
            printf("     Spiral Pitch: %f\n", cfg->disk_structure_spiral_pitch);
            printf("     Position Variation: %f\n", cfg->disk_structure_position_variation);
            printf("     Modulation: %f\n", cfg->disk_structure_modulation);
        }
        else
        {
            printf("    Disk Structure: Disabled\n");
        }
    }
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
        printf("  Freeing blackbody ramp data (%d samples)...\n", cfg->blackbody_ramp_size);
        free(cfg->blackbody_ramp_data);
        cfg->blackbody_ramp_data = NULL;
        cfg->blackbody_ramp_size = 0;
    }
}
