#ifndef CONFIG_DEFAULTS_H
#define CONFIG_DEFAULTS_H

#include "vector.h"
#include "config.h" // For enums like DT_NONE etc.

// --- Default Values ---

// Resolution & Performance
#define DEFAULT_THREADS 4
#define DEFAULT_CHUNKSIZE 9000
#define DEFAULT_LOFI false

// Materials & Effects
#define DEFAULT_DISK_TEXTURE_PATH "textures/adisk.jpg"
#define DEFAULT_SKY_TEXTURE_PATH "textures/bgedit.jpg"

// Blackbody Disk Specific
#define DEFAULT_BLACKBODY_RAMP_PATH "blackbody_ramp/blackbody_ramp_1000_50000K_4096_linear_srgb_normalized.ramp"

// Scene File
#define DEFAULT_SCENE_FILENAME "scenes/new/default.scene"

#endif // CONFIG_DEFAULTS_H
