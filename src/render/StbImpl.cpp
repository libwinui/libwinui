// SPDX-License-Identifier: MIT
// The one translation unit that compiles the stb_image and stb_image_write implementations.
// Everything else includes these headers for their declarations only.
#if defined(_MSC_VER)
#pragma warning(push, 0)
// stb_image_write uses sprintf, which /sdl (LIBWINUI_ENABLE_HARDENING) makes an error in spite of the
// pragma above.
#define _CRT_SECURE_NO_WARNINGS // NOLINT(bugprone-reserved-identifier): the CRT's own switch
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_write.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
