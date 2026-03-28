#include "config.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
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


// --- Strict Parsing Helpers ---
static bool strict_parse_int(const char *str, int *out)
{
    if (!str || *str == '\0') return false;
    char *end;
    errno = 0;
    long val = strtol(str, &end, 10);
    if (end == str || *end != '\0') return false;
    if (errno == ERANGE || val < INT_MIN || val > INT_MAX) return false;
    *out = (int)val;
    return true;
}

static bool strict_parse_double(const char *str, double *out)
{
    if (!str || *str == '\0') return false;
    char *end;
    errno = 0;
    double val = strtod(str, &end);
    if (end == str || *end != '\0') return false;
    if (errno == ERANGE) return false;
    *out = val;
    return true;
}

static bool strict_parse_bool(const char *str, bool *out)
{
    if (!str) return false;
    if (strcmp(str, "1") == 0 || STRCASECMP(str, "true") == 0 || STRCASECMP(str, "yes") == 0)
    {
        *out = true;
        return true;
    }
    if (strcmp(str, "0") == 0 || STRCASECMP(str, "false") == 0 || STRCASECMP(str, "no") == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

// --- Parsing Functions ---
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
                // fprintf(stderr, "Warning: Parsing token '%s' failed.\n", token);
                count = -1;
                break;
            }
        }
        token = STRTOK(NULL, delimiter, &save_ptr);
    }
    free(str_copy);
    return count == expected_count;
}

bool parse_resolution(const char *res_str, int resolution[2])
{
    if (!res_str) return false;
    int width = 0, height = 0;
    if (SSCANF(res_str, "%dx%d", &width, &height) == 2 && width > 0 && height > 0)
    {
        resolution[0] = width;
        resolution[1] = height;
        return true;
    }
    return false;
}

static int parse_positive_int_arg(const char *arg_name, const char *value_str)
{
    int val;
    if (!strict_parse_int(value_str, &val) || val <= 0)
    {
        fprintf(stderr, "! Error: Invalid or non-positive value '%s' for %s.\n",
                value_str ? value_str : "(null)", arg_name);
        return -1;
    }
    return val;
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

// Generic logic for "Smart" Enums
static void parse_smart_enum_generic(char **path_field, int *mode_field, const char *value,
                                     const char **enum_names, int enum_count,
                                     const char *default_path, int texture_mode_enum)
{
    int mode;
    if (parse_enum_value(value, &mode, enum_names, enum_count))
    {   // It's a keyword (e.g. "none", "texture", "grid")
        *mode_field = mode;
        if (*mode_field == texture_mode_enum)
        {   // Explicit "texture" keyword -> force default path
            free(*path_field);
            *path_field = STRDUP(default_path);
            if (*path_field) printf("  Using default path: %s\n", default_path);
        }
        else
        {   // "none", "grid", etc. -> clear path
            free(*path_field);
            *path_field = NULL;
        }
    }
    else
    {   // Not a keyword -> assume it's a file path
        *mode_field = texture_mode_enum;
        free(*path_field);
        *path_field = STRDUP(value);
        if (*path_field) printf("  Custom path detected: %s\n", value);
    }
}

// --- Specific Handlers for Smart Enums ---
static void handle_disk_texture_mode(Config *cfg, const char *value)
{
    parse_smart_enum_generic(&cfg->disk_texture_path, (int*)&cfg->disk_texture_mode, value,
                             disk_texture_mode_names, sizeof(disk_texture_mode_names) / sizeof(char*),
                             DEFAULT_DISK_TEXTURE_PATH, DT_TEXTURE);
}

static void handle_sky_texture_mode(Config *cfg, const char *value)
{
    parse_smart_enum_generic(&cfg->sky_texture_path, (int*)&cfg->sky_texture_mode, value,
                             sky_texture_mode_names, sizeof(sky_texture_mode_names) / sizeof(char*),
                             DEFAULT_SKY_TEXTURE_PATH, ST_TEXTURE);
}

// --- INI Parsing Callback ---
static int scene_ini_callback(void *user, const char *section, const char *name, const char *value)
{
    IniParseUserData *user_data = (IniParseUserData *)user;
    Config *cfg = user_data->cfg;
    bool *override_res = user_data->override_res_flag;

    // --- Define Parsers ---
    #define INIT_INT_PARSE(field) \
        do { int _v; if (strict_parse_int(value, &_v)) cfg->field = _v; \
             else fprintf(stderr, "  Warning: Invalid integer '%s' for %s\n", value, #field); } while(0)
    #define INIT_DOUBLE_PARSE(field) \
        do { double _v; if (strict_parse_double(value, &_v)) cfg->field = _v; \
             else fprintf(stderr, "  Warning: Invalid number '%s' for %s\n", value, #field); } while(0)
    #define INIT_BOOL_PARSE(field) \
        do { bool _v; if (strict_parse_bool(value, &_v)) cfg->field = _v; \
             else fprintf(stderr, "  Warning: Invalid boolean '%s' for %s\n", value, #field); } while(0)
    #define INIT_VEC3_PARSE(field)          \
        if (!parse_vec3d(value, &cfg->field)) { fprintf(stderr, "  Warning: Invalid vec3 '%s' for %s\n", value, #field); }
    #define INIT_INT_ARRAY2_PARSE(field)    \
        if (!parse_int_list(value, cfg->field, 2)) { fprintf(stderr, "  Warning: Invalid int list '%s' for %s\n", value, #field); }
    #define INIT_STRING_PARSE(field)        \
        do { \
            if (value && strlen(value) > 0) { \
                free(cfg->field); \
                cfg->field = STRDUP(value); \
            } \
        } while(0)
    #define INIT_SMART_ENUM_PARSE(field)    handle_##field(cfg, value)
    #define INIT_NULL_PARSE(field)          /* skip */

    #define FIELD_DEF(fieldName, cfgKey, initMacro, defLiteral) \
        if (cfgKey && cfgKey[0] != '\0' && strcmp(name, cfgKey) == 0) { \
            if (strcmp(cfgKey, "Resolution") == 0 && *override_res) return 1; \
            initMacro##_PARSE(fieldName); \
            return 1; \
        }

    // --- Section Routing ---
    bool is_lofi_hifi = (strcmp(section, "lofi") == 0 || strcmp(section, "hifi") == 0);
    if (is_lofi_hifi)
    {
        // Only parse if section matches current mode
        if ((cfg->lofi && strcmp(section, "lofi") == 0) || (!cfg->lofi && strcmp(section, "hifi") == 0))
        {
            #define SEC_LOFIHIFI
            #include "x_config_fields.h"
        }
    }
    else if (strcmp(section, "geometry") == 0)
    {
        #define SEC_GEOMETRY
        #include "x_config_fields.h"
    }
    else if (strcmp(section, "materials") == 0)
    {
        #define SEC_MATERIALS
        #include "x_config_fields.h"
    }

    // Cleanup
    #undef FIELD_DEF

    #undef INIT_INT_PARSE
    #undef INIT_DOUBLE_PARSE
    #undef INIT_BOOL_PARSE
    #undef INIT_VEC3_PARSE
    #undef INIT_INT_ARRAY2_PARSE
    #undef INIT_STRING_PARSE
    #undef INIT_SMART_ENUM_PARSE
    #undef INIT_NULL_PARSE

    // Log unhandled keys
    // fprintf(stderr, "  Debug: Unhandled key '%s' in section [%s]\n", name, section);
    return 1;
}

// --- Main Config Loading Function ---
bool load_config(int argc, char *argv[], Config *cfg)
{
    if (!cfg) { return false; }

    // --- Initialize Defaults ---
    #define INIT_INT_ARRAY2(fieldName, cfgKey, defLiteral)  do { cfg->fieldName[0] = defLiteral[0]; cfg->fieldName[1] = defLiteral[1]; } while(0)
    #define INIT_STRING(fieldName, cfgKey, defLiteral)      cfg->fieldName = STRDUP(defLiteral)
    #define INIT_INT(fieldName, cfgKey, defLiteral)         cfg->fieldName = defLiteral
    #define INIT_NULL           INIT_INT
    #define INIT_SMART_ENUM     INIT_INT
    #define INIT_DOUBLE         INIT_INT
    #define INIT_BOOL           INIT_INT
    #define INIT_VEC3           INIT_INT

    #define FIELD_DEF(fieldName, cfgKey, initMacro, defLiteral) initMacro(fieldName, cfgKey, defLiteral)
    #define SEC_ALL
    #include "x_config_fields.h"
    #undef FIELD_DEF

    // Sanity check allocations
    if (!cfg->disk_texture_path || !cfg->sky_texture_path || !cfg->blackbody_ramp_path)
    {
        fprintf(stderr, "!Error: Memory allocation failed for default paths.\n");
        free_config_textures(cfg);
        return false;
    }

    const char *scene_filename = NULL; // Will be set from args or default
    bool override_res = false;

    // --- Parse Command Line Arguments ---
    printf("Parsing command line arguments...\n");
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];

        // Scene Filename
        if (arg[0] != '-')
        {
            scene_filename = arg;
            continue;
        }

        // Options
        if (strcmp(arg, "-d") == 0)
        {
            cfg->lofi = true;
            printf("  Found -d: Enabling Lo-Fi mode defaults\n");
        }
        else if (strncmp(arg, "-c", 2) == 0)
        {
            int val = parse_positive_int_arg("-c", arg + 2);
            if (val > 0)
            {
                printf("  Found -c: Setting chunk size to %d (default: %d)\n", val, cfg->chunk_size);
                cfg->chunk_size = val;
            }
            else return false;
        }
        else if (strncmp(arg, "-j", 2) == 0)
        {
            int val = parse_positive_int_arg("-j", arg + 2);
            if (val > 0)
            {
                printf("  Found -j: Setting threads number to %d (default: %d)\n", val, cfg->n_threads);
                cfg->n_threads = val;
            }
            else return false;
        }
        else if (strncmp(arg, "-r", 2) == 0)
        {
            if (parse_resolution(arg + 2, cfg->resolution))
            {
                override_res = true;
                printf("  Found -r: Setting resolution to %dx%d\n", cfg->resolution[0], cfg->resolution[1]);
            }
            else
            {
                fprintf(stderr, "! Error: Invalid resolution format in '%s'. Use -rWxH with positive integers.\n", arg);
                return false;
            }
        }
        else
        {
            fprintf(stderr, "  Warning: Unknown option '%s', ignored.\n", arg);
        }
    }

    // --- Setup Scene File ---
    printf("Setting up scene...\n");
    if (!scene_filename)
    {
        printf("  No scene file specified, using default: %s\n", DEFAULT_SCENE_PATH);
        scene_filename = DEFAULT_SCENE_PATH;
    }

    if (ACCESS(scene_filename, F_OK) == -1)
    {
        fprintf(stderr, "! Error: Scene file \"%s\" not found.\n", scene_filename);
        return false;
    }

    free(cfg->scene_file_path);
    cfg->scene_file_path = STRDUP(scene_filename);

    const char *p = strrchr(scene_filename, '/');
#ifdef _WIN32
    if (!p) p = strrchr(scene_filename, '\\');
#endif
    p = (p ? p + 1 : scene_filename);
    free(cfg->scene_base_name);
    cfg->scene_base_name = STRDUP(p);
    char *dot = strrchr(cfg->scene_base_name, '.');
    if (dot && STRCASECMP(dot, ".scene") == 0) *dot = '\0';

    // --- Parse INI ---
    printf("Parsing scene file '%s'...\n", cfg->scene_file_path);
    IniParseUserData user_data = {cfg, &override_res};
    if (ini_parse(cfg->scene_file_path, scene_ini_callback, &user_data) < 0)
    {
        fprintf(stderr, "! Error: Can't parse scene file '%s'\n", cfg->scene_file_path);
        free_config_textures(cfg);
        return false;
    }

    // --- Load Resources ---
    printf("Loading resources...\n");
    // Disk Texture
    if (cfg->disk_texture_mode == DT_TEXTURE)
    {
        if (cfg->disk_texture_path)
        {
            printf("  Loading disk texture: %s\n", cfg->disk_texture_path);
            cfg->disk_texture = load_texture(cfg->disk_texture_path);
        }
        else fprintf(stderr, "  Warning: Disk mode is TEXTURE, but no path configured.\n");
    }

    // Sky Texture
    Texture *original_sky_texture = NULL;
    if (cfg->sky_texture_mode == ST_TEXTURE)
    {
        if (cfg->sky_texture_path)
        {
            printf("  Loading sky texture: %s\n", cfg->sky_texture_path);
            original_sky_texture = load_texture(cfg->sky_texture_path);
        }
        else fprintf(stderr, "  Warning: Sky mode is TEXTURE, but no path configured.\n");
    }

    // Resize Sky Texture for HiFi mode
    if (!cfg->lofi && original_sky_texture)
    {
        printf("    HiFi mode: Resizing sky texture...\n");
        cfg->sky_texture = resize_texture(original_sky_texture, 2.0f);
        if (cfg->sky_texture) free_texture(original_sky_texture);
        else
        {
            cfg->sky_texture = original_sky_texture; // Fallback
            fprintf(stderr, "    Warning: Resize failed, using original.\n");
        }
    }
    else
    {
        cfg->sky_texture = original_sky_texture;
    }

    // Blackbody Ramp
    if (cfg->disk_texture_mode == DT_BLACKBODY)
    {
        if (!cfg->blackbody_ramp_path) cfg->blackbody_ramp_path = STRDUP(DEFAULT_BLACKBODY_RAMP_PATH);
        printf("  Loading blackbody ramp: %s...\n", cfg->blackbody_ramp_path);
        if (!load_blackbody_ramp_from_file(cfg->blackbody_ramp_path, &cfg->blackbody_ramp_data, &cfg->blackbody_ramp_size))
        {
            fprintf(stderr, "! Error: Failed to load blackbody ramp.\n");
            free_config_textures(cfg);
            return false;
        }
    }

    // --- Compute Derived & Validate ---
    printf("Computing derived values...\n");
    compute_derived_config(cfg);
    if (vec3d_norm(cfg->camera_pos) <= 1.0)
    {
        fprintf(stderr, "! Error: Camera is inside the event horizon (r <= 1.0). Set Cameraposition further out.\n");
        free_config_textures(cfg);
        return false;
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
    if (cfg->disk_texture) { free_texture(cfg->disk_texture); cfg->disk_texture = NULL; }
    if (cfg->sky_texture)  { free_texture(cfg->sky_texture);  cfg->sky_texture = NULL; }

    if (cfg->disk_texture_path) { free(cfg->disk_texture_path); cfg->disk_texture_path = NULL; }
    if (cfg->sky_texture_path)  { free(cfg->sky_texture_path);  cfg->sky_texture_path = NULL; }
    if (cfg->blackbody_ramp_path) { free(cfg->blackbody_ramp_path); cfg->blackbody_ramp_path = NULL; }

    if (cfg->blackbody_ramp_data) { free(cfg->blackbody_ramp_data); cfg->blackbody_ramp_data = NULL; }

    if (cfg->scene_file_path) { free(cfg->scene_file_path); cfg->scene_file_path = NULL; }
    if (cfg->scene_base_name) { free(cfg->scene_base_name); cfg->scene_base_name = NULL; }
}
