#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include <stdbool.h>
#include "config.h"
#include "image.h"

// Applies the full post-processing pipeline to the image.
// The result is written back into output_image.
// Returns true on success, false on failure.
bool apply_postprocessing(const Config *cfg, ImageF *output_image);

#endif // POSTPROCESS_H
