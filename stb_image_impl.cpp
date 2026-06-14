// The single-header stb_image implementation lives in its own translation unit.
// Warnings from this third-party header are suppressed so the rest of the build
// stays clean under -Wall -Wextra.
#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wunknown-warning-option"
#  pragma clang diagnostic ignored "-Wunused-but-set-variable"
#  pragma clang diagnostic ignored "-Wunused-parameter"
#  pragma clang diagnostic ignored "-Wsign-compare"
#  pragma clang diagnostic ignored "-Wdeprecated-declarations" // stb HDR writer uses sprintf
#  pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__)
#  pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO_DEPRECATE
#include "stb_image.h"

// PNG writer used by the built-in GRP texture extractor (GrpExtract.cpp). Its
// own zlib-style compressor means no external zlib dependency.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO_DEPRECATE
#include "stb_image_write.h"
