/* @note stb_image (zlib inflate for .rttex) and stb_image_write (PNG) by Sean Barrett, public domain: https://github.com/nothings/stb */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
