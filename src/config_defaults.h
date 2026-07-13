#ifndef CONFIG_DEFAULTS_H
#define CONFIG_DEFAULTS_H

// These values are used if the config file does not specify them:
#define DEFAULT_BLACKBODY_RAMP_PATH "blackbody_ramp/bb_ramp_1000_50000K_8192_linear_srgb_normalized.ramp"
#define DEFAULT_DISK_TEXTURE_PATH "textures/adisk.jpg"
#define DEFAULT_SKY_TEXTURE_PATH "textures/bgedit.jpg"

// This scene is used if no scene file provided in command line:
#define DEFAULT_SCENE_PATH "scenes/new/default.scene"

#define DEFAULT_RAMP_TEMP_MIN 1000.0
#define DEFAULT_RAMP_TEMP_MAX 50000.0

#endif
