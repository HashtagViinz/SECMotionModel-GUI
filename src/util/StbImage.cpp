// Single stb_image implementation unit. The renderer decodes the embedded
// Earth texture from memory, so no external image file is needed at runtime.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
