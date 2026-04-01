// Single translation unit that owns the stb_image implementation.
// All other files include <stb_image.h> without this define.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
