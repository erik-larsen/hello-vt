#include "LibVT_Internal.h"
#include "LibVT.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static void *verifyImage(void *image_data, uint32_t *pic_size, int width, int height)
{
    assert(image_data);

    if (*pic_size == 0)
        *pic_size = width;
    else
        assert(((uint32_t)width == *pic_size) && ((uint32_t)height == *pic_size));

    return image_data;
}

void * vtuDecompressImageFile(const char *imagePath, uint32_t *pic_size)
{
    #if DEBUG_LOG > 0
        printf("Thread %llu: Load & decompress image file: %s\n", THREAD_ID, imagePath);
    #endif

    int width, height, bitdepth;
    void * image_data = stbi_load(imagePath, &width, &height, &bitdepth, STBI_rgb);

    return verifyImage(image_data, pic_size, width, height);
}

void * vtuDecompressImageBuffer(const void *file_data, uint32_t file_size, uint32_t *pic_size)
{
    #if DEBUG_LOG > 0
        printf("Thread %llu: Decompress image in-memory: %p\n", THREAD_ID, file_data);
    #endif

    int width, height, bitdepth;
    void * image_data = stbi_load_from_memory((const stbi_uc *)file_data, file_size, &width, &height, &bitdepth, STBI_rgb);

    return verifyImage(image_data, pic_size, width, height);
}