#ifndef CONFIG_DEFAULTS_H
#define CONFIG_DEFAULTS_H

#include "vector.h"
#include "config.h" // For enums like DT_NONE etc.

// --- Default Values ---

// Resolution & Performance
#define DEFAULT_RESOLUTION_WIDTH 160
#define DEFAULT_RESOLUTION_HEIGHT 120
#define DEFAULT_ITERATIONS 1000
#define DEFAULT_SSAA_LEVEL 2
#define DEFAULT_STEPSIZE 0.02
#define DEFAULT_THREADS 4
#define DEFAULT_CHUNKSIZE 9000
#define DEFAULT_LOFI false

// Geometry
#define DEFAULT_CAMERA_POS {0.0, 1.0, -10.0}
#define DEFAULT_TAN_FOV 1.5
#define DEFAULT_LOOK_AT {0.0, 0.0, 0.0}
#define DEFAULT_UP_VECTOR {0.0, 1.0, 0.0}
#define DEFAULT_DISTORT true
#define DEFAULT_DISK_INNER_RADIUS 1.5
#define DEFAULT_DISK_OUTER_RADIUS 4.0

// Materials & Effects
#define DEFAULT_HORIZON_GRID true
#define DEFAULT_DISK_TEXTURE_MODE DT_NONE
#define DEFAULT_SKY_TEXTURE_MODE ST_NONE
#define DEFAULT_DISK_TEXTURE_PATH "textures/adisk.jpg"
#define DEFAULT_SKY_TEXTURE_PATH "textures/bgedit.jpg"
#define DEFAULT_SKY_DISK_RATIO 1.0
#define DEFAULT_FOG_DO true
#define DEFAULT_FOG_MULT 0.02
#define DEFAULT_FOG_SKIP 1
#define DEFAULT_BLUR_DO true
#define DEFAULT_AIRY_BLOOM true
#define DEFAULT_AIRY_RADIUS 1.0
#define DEFAULT_GAIN 1.0
#define DEFAULT_ACES_EXPOSURE 1.0
#define DEFAULT_NORMALIZE -1.0 // Off by default
#define DEFAULT_SRGB_OUT true
#define DEFAULT_SRGB_IN true

// Blackbody Disk Specific
#define DEFAULT_BLACKBODY_RAMP_PATH "blackbody_ramp/blackbody_ramp_1000_50000K_4096_linear_srgb_normalized.ramp"
#define DEFAULT_DISK_MULTIPLIER 100.0
#define DEFAULT_DISK_INTENSITY_DO true
#define DEFAULT_REDSHIFT 1.0

// Blackbody Disk Structure
#define DEFAULT_DISK_ADD_STRUCTURE false
#define DEFAULT_DISK_STRUCTURE_SPIRAL_ARMS 5
#define DEFAULT_DISK_STRUCTURE_RINGS_FREQ {1.0, 1.0, 1.0}
#define DEFAULT_DISK_STRUCTURE_SPIRAL_PITCH 0.3
#define DEFAULT_DISK_STRUCTURE_POSITION_VARIATION 0.15
#define DEFAULT_DISK_STRUCTURE_MODULATION 0.5

// Scene File
#define DEFAULT_SCENE_FILENAME "scenes/new/default.scene"

#endif // CONFIG_DEFAULTS_H
