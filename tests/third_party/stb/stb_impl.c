/* The stb_image and stb_image_write implementations, compiled once (as C++,
   see tests/CMakeLists.txt).
   From github.com/nothings/stb at 2c980bb (ADR-015). Test code only: used to
   read the dmg-acid2 reference screenshot and to save a frame that differs. */
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
