#ifndef FIELD_DEF
    #error "FIELD_DEF must be defined before including x_config_fields.h"
#endif

#include "config_default_paths.h"

#ifdef SEC_ALL
    #define SEC_LOFIHIFI
    #define SEC_GEOMETRY
    #define SEC_MATERIALS
    #define SEC_OTHER
#endif

/* .scene settings */
/*_______________________________________________________________________________________________________________________________*/
/*       (field_name_in_cfg,         ".ini param name / .png meta KeySuffix",    INIT_MACRO,       default_value);               */
/*-------------------------------------------------------------------------------------------------------------------------------*/
#ifdef SEC_LOFIHIFI
FIELD_DEF(resolution,                        "Resolution",                       INIT_INT_ARRAY2,  ((int[2]){640, 480})          );
FIELD_DEF(n_iterations,                      "Iterations",                       INIT_INT,         1000                          );
FIELD_DEF(ssaa_level,                        "SSAA",                             INIT_INT,         2                             );
FIELD_DEF(step_size,                         "Stepsize",                         INIT_DOUBLE,      0.02                          );
#endif

#ifdef SEC_GEOMETRY
FIELD_DEF(camera_pos,                        "Cameraposition",                   INIT_VEC3,        ((Vec3d){0.0, 1.0, -10.0})    );
FIELD_DEF(tan_fov,                           "Fieldofview",                      INIT_DOUBLE,      1.5                           );
FIELD_DEF(look_at,                           "Lookat",                           INIT_VEC3,        ((Vec3d){0.0, 0.0, 0.0})      );
FIELD_DEF(up_vector,                         "Upvector",                         INIT_VEC3,        ((Vec3d){0.0, 1.0, 0.0})      );
FIELD_DEF(distort,                           "Distort",                          INIT_BOOL,        true                          );
FIELD_DEF(disk_inner_radius,                 "Diskinner",                        INIT_DOUBLE,      1.5                           );
FIELD_DEF(disk_outer_radius,                 "Diskouter",                        INIT_DOUBLE,      4.0                           );
#endif

#ifdef SEC_MATERIALS
FIELD_DEF(horizon_grid,                      "Horizongrid",                      INIT_BOOL,        true                          );
FIELD_DEF(disk_texture_mode,                 "Disktexture",                      INIT_SMART_ENUM,  DT_NONE                       );
FIELD_DEF(sky_texture_mode,                  "Skytexture",                       INIT_SMART_ENUM,  ST_NONE                       );
FIELD_DEF(sky_disk_ratio,                    "Skydiskratio",                     INIT_DOUBLE,      1.0                           );
FIELD_DEF(fog_do,                            "Fogdo",                            INIT_BOOL,        true                          );
FIELD_DEF(fog_mult,                          "Fogmult",                          INIT_DOUBLE,      0.02                          );
FIELD_DEF(fog_skip,                          "Fogskip",                          INIT_INT,         1                             );
FIELD_DEF(blur_do,                           "Blurdo",                           INIT_BOOL,        true                          );
FIELD_DEF(airy_bloom,                        "Airy_bloom",                       INIT_BOOL,        true                          );
FIELD_DEF(airy_radius,                       "Airy_radius",                      INIT_DOUBLE,      1.0                           );
FIELD_DEF(gain,                              "Gain",                             INIT_DOUBLE,      1.0                           );
FIELD_DEF(normalize,                         "Normalize",                        INIT_DOUBLE,      -1.0                          );
FIELD_DEF(aces_exposure,                     "ACESExposure",                     INIT_DOUBLE,      1.0                           );
FIELD_DEF(srgb_out,                          "sRGBOut",                          INIT_BOOL,        true                          );
FIELD_DEF(srgb_in,                           "sRGBIn",                           INIT_BOOL,        true                          );
FIELD_DEF(blackbody_ramp_path,               "Blackbodyramp",                    INIT_STRING,      DEFAULT_BLACKBODY_RAMP_PATH   );
FIELD_DEF(disk_multiplier,                   "Diskmultiplier",                   INIT_DOUBLE,      100.0                         );
FIELD_DEF(disk_intensity_do,                 "Diskintensitydo",                  INIT_BOOL,        true                          );
FIELD_DEF(redshift,                          "Redshift",                         INIT_DOUBLE,      1.0                           );
FIELD_DEF(disk_add_structure,                "Diskaddstructure",                 INIT_BOOL,        false                         );
FIELD_DEF(disk_structure_spiral_arms,        "Diskstructure_spiral_arms",        INIT_INT,         5                             );
FIELD_DEF(disk_structure_rings_freq,         "Diskstructure_rings_freq",         INIT_VEC3,        ((Vec3d){1.0, 1.0, 1.0})      );
FIELD_DEF(disk_structure_spiral_pitch,       "Diskstructure_spiral_pitch",       INIT_DOUBLE,      0.3                           );
FIELD_DEF(disk_structure_position_variation, "Diskstructure_position_variation", INIT_DOUBLE,      0.15                          );
FIELD_DEF(disk_structure_modulation,         "Diskstructure_modulation",         INIT_DOUBLE,      0.5                           );
#endif

#ifdef SEC_OTHER
/* other Config fields                                                                                                           */
/*       (field_name_in_cfg,                  ".png meta tag",                   INIT_MACRO,      default_value);                */
/*_______________________________________________________________________________________________________________________________*/
FIELD_DEF(lofi,                              "Lo-Fi enabled",                    INIT_BOOL,        false                         );

FIELD_DEF(scene_file_path,                   "",                                 INIT_NULL,        NULL                          );
FIELD_DEF(scene_base_name,                   "",                                 INIT_NULL,        NULL                          );

FIELD_DEF(disk_texture_path,                 "Disk texture path",                INIT_STRING,      DEFAULT_DISK_TEXTURE_PATH     );
FIELD_DEF(sky_texture_path,                  "Sky texture path",                 INIT_STRING,      DEFAULT_SKY_TEXTURE_PATH      );

FIELD_DEF(disk_texture,                      "",                                 INIT_NULL,        NULL                          );
FIELD_DEF(sky_texture,                       "",                                 INIT_NULL,        NULL                          );

FIELD_DEF(blackbody_ramp_data,               "",                                 INIT_NULL,        NULL                          );
FIELD_DEF(blackbody_ramp_size,               "BB ramp size",                     INIT_INT,         0                             );

FIELD_DEF(n_threads,                         "Number of threads",                INIT_INT,         6                             );
FIELD_DEF(chunk_size,                        "Chunk size",                       INIT_INT,         3000                          );
#endif
/* ==============================================================================================================================*/

// Undefine all the macros that could be defined to avoid hard-to-debug fuckups.
// Sections:
#undef SEC_ALL
#undef SEC_LOFIHIFI
#undef SEC_GEOMETRY
#undef SEC_MATERIALS
#undef SEC_OTHER
// Initialization macros:
#undef INIT_INT_ARRAY2
#undef INIT_VEC3
#undef INIT_DOUBLE
#undef INIT_BOOL
#undef INIT_INT
#undef INIT_STRING
#undef INIT_NULL
#undef INIT_SMART_ENUM
// FIELD_DEF macro should be undefined by the includer.
