#include "LibVT_Config.h"

#include <time.h>
#include <assert.h>
#include <math.h>
#include <sys/types.h>

// TARGET_GLES
#include <OpenGLES/GLES2/gl2.h>
#include <OpenGLES/GLES2/gl2ext.h>
#define glBindFramebufferEXT glBindFramebuffer
#define glOrtho glOrthof
#define GL_FRAMEBUFFER_EXT    GL_FRAMEBUFFER
#define glDeleteFramebuffersEXT glDeleteFramebuffers
#define glGenFramebuffersEXT glGenFramebuffers
#define glFramebufferTexture2DEXT glFramebufferTexture2D
#define glCheckFramebufferStatusEXT glCheckFramebufferStatus
#define GL_MAX_TEXTURE_IMAGE_UNITS_ARB GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_UNSIGNED_INT_8_8_8_8_REV    GL_UNSIGNED_BYTE
#define GL_BGRA GL_BGRA_EXT

#ifdef WIN32
    #define PATH_SEPERATOR "\\"
    #include <dirent.h>
    #include <stdio.h>
    #include <string.h>
    #include <stdarg.h>

#elif defined(__APPLE__)
    #include <fcntl.h>  // Add this include for F_GLOBAL_NOCACHE

    #import <TargetConditionals.h>
    #define PATH_SEPERATOR "/"
    #include <dirent.h>

#elif defined(linux)
    #define PATH_SEPERATOR "/"
    #include <dirent.h>
    #include <stdio.h>
    #include <string.h>
    #include <stdarg.h>
    #include <stdlib.h>
    #include <locale.h>

#else
    #error COULD_NOT_GUESS_TARGET_SYSTEM

#endif

#include <iostream>
#include <queue>
#include <map>
#include <vector>
#include <string>

#undef TIME_UTC

#include <thread>
#include <functional>
#define THREAD_ID static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()))

#if ENABLE_MT
#include <atomic>
#include <mutex>
#include <condition_variable>
#endif

void vtShutdown();
static inline void vt_fatal(const char *err, ...) {va_list ap; va_start (ap, err); vtShutdown(); vfprintf (stderr, err, ap); va_end (ap); exit(1); }

using namespace std;

enum {
    kCustomReadback = 1,
    kBackbufferReadPixels = 2,
    kBackbufferGetTexImage = 3,
    kFBOReadPixels = 4,
    kFBOGetTexImage = 5
};

enum {
    kTableFree = 0,
    kTableMappingInProgress = 1,
    kTableMapped = 0xFF
};

#define MIPPED_PHYSTEX              (VT_MIN_FILTER == GL_NEAREST_MIPMAP_NEAREST || \
                                     VT_MIN_FILTER == GL_LINEAR_MIPMAP_NEAREST || \
                                     VT_MIN_FILTER == GL_NEAREST_MIPMAP_LINEAR || \
                                     VT_MIN_FILTER == GL_LINEAR_MIPMAP_LINEAR)
#define READBACK_MODE_NONE          (READBACK_MODE == kCustomReadback)
#define READBACK_MODE_FBO           (READBACK_MODE >= kFBOReadPixels)
#define READBACK_MODE_BACKBUFFER    ((READBACK_MODE < kFBOReadPixels) && (!READBACK_MODE_NONE))
#define READBACK_MODE_GET_TEX_IMAGE (READBACK_MODE == kFBOGetTexImage || READBACK_MODE == kBackbufferGetTexImage)
#define READBACK_MODE_READ_PIXELS   (READBACK_MODE == kFBOReadPixels || READBACK_MODE == kBackbufferReadPixels)

#define DecompressionPNG 4
#define DecompressionJPEG 8
#define DecompressionAllFormats 16

#define DecompressionLibPNG 4
#define DecompressionSTBIPNG 5
#define DecompressionLibJPEG 8
#define DecompressionLibJPEGTurbo 9
#define DecompressionSTBIJPEG 11
#define DecompressionMac 16
#define DecompressionDevil 17
#define DecompressionImageMagick 18


#define BYTE1(v)                    ((uint8_t) (v))
#define BYTE2(v)                    ((uint8_t) (((uint32_t) (v)) >> 8))
#define BYTE3(v)                    ((uint8_t) (((uint32_t) (v)) >> 16))
#define BYTE4(v)                    ((uint8_t) (((uint32_t) (v)) >> 24))

#if LONG_MIP_CHAIN
    #define MAKE_PAGE_INFO(m, x, y) ((x << 20) + (y << 8) + m)
    #define EXTRACT_Y(page)         ((uint16_t) ((((uint32_t) (page)) >> 8) & 0xFFF))
    #define EXTRACT_X(page)         ((uint16_t) ((((uint32_t) (page)) >> 20) & 0xFFF))
#else
    #define MAKE_PAGE_INFO(m, x, y) ((x << 16) + (y << 8) + m)
    #define EXTRACT_Y(page)         (BYTE2(page))
    #define EXTRACT_X(page)         (BYTE3(page))
#endif


#define PAGE_TABLE(m, x, y)         (vt.pageTables[(m)][(y) * (c.virtTexDimensionPages >> (m)) + (x)])
#define EXTRACT_MIP(page)           (BYTE1(page))

#if LONG_MIP_CHAIN
    #define MIP_INFO(mip)           (c.mipChainLength - 1 - mip)
#else
    #define MIP_INFO(mip)           (vt.mipTranslation[mip])
#endif

#if ENABLE_MT
    #define LOCK(x)                 std::unique_lock<std::mutex> scoped_lock(x);
#else
    #define LOCK(x)
#endif

#ifdef DEBUG
    #define fast_assert(x)
#else
    #define fast_assert(x)          assert((x))
#endif

#define touchMipRow(mip, row)       {vt.mipLevelTouched[mip] = true; \
                                     vt.mipLevelMinrow[mip] = (vt.mipLevelMinrow[mip] < row) ? vt.mipLevelMinrow[mip] : row; \
                                     vt.mipLevelMaxrow[mip] = (vt.mipLevelMaxrow[mip] > row) ? vt.mipLevelMaxrow[mip] : row; }

#define MAX_PHYS_TEX_DIMENSION_PAGES 64

struct storageInfo
{
    clock_t     clockUsed;
    uint16_t    x, y;
    uint8_t     mip;
};

struct vtConfig // TODO: constify?
{
    uint32_t    pageDimension;
    string      tileDir, pageCodec;
    uint8_t     pageBorder, mipChainLength;

    // derived values:
    uint32_t    pageMemsize, maxCachedPages, physTexDimensionPages, virtTexDimensionPages, residentPages;
    GLenum      pageDataFormat, pageDataType;
};

struct vtData
{
    uint16_t                mipTranslation[12];
    uint32_t                pageTableMipOffsets[12];
    GLuint                  fbo, fboColorTexture, fboDepthTexture, physicalTexture, pageTableTexture, mipcalcTexture, pboReadback, pboPagetable, pboPhystex;

    bool                    mipLevelTouched[12];
    uint16_t                mipLevelMinrow[12];
    uint16_t                mipLevelMaxrow[12];
    storageInfo             textureStorageInfo[MAX_PHYS_TEX_DIMENSION_PAGES][MAX_PHYS_TEX_DIMENSION_PAGES];    // yes allocating this to the max size is a memory waste - it consumes 50k - but a vector of vectors is 1 magnitude slower

    uint16_t                necessaryPageCount, newPageCount, missingPageCount;
    float                   bias;
    uint32_t                *readbackBuffer, **pageTables;
    clock_t                 thisFrameClock;
    uint32_t                w, h, real_w, real_h;
    double                  projectionMatrix[4][4];
    double                  fovInDegrees;
    deque<uint32_t>         neededPages;
    queue<uint32_t>         newPages;
    map<uint32_t, void *>   cachedPages;
    map<uint32_t, clock_t>  cachedPagesAccessTimes;

#if ENABLE_MT
    std::atomic<bool>       shutdownThreads{false};
    std::condition_variable neededPagesAvailableCondition;
    std::mutex              neededPagesMutex;
    std::mutex              newPagesMutex;
    std::mutex              cachedPagesMutex;
    std::thread             backgroundThread;
#endif

#if ENABLE_MT > 1
    std::thread             backgroundThread2;
    std::mutex              compressedMutex;
    queue<uint32_t>         newCompressedPages;
    map<uint32_t, void *>   compressedPages;
    map<uint32_t, uint32_t> compressedPagesSizes;
    std::condition_variable compressedPagesAvailableCondition;
#endif
};

void        vtLoadNeededPages();
void        vtLoadNeededPagesDecoupled();
void        vtDecompressNeededPagesDecoupled();
void        vtCachePages(queue<uint32_t> pagesToCache);

void        vtcRemoveCachedPageLOCK(uint32_t pageInfo);
void        vtcTouchCachedPage(uint32_t pageInfo);
void        vtcSplitPagelistIntoCachedAndNoncachedLOCK(queue<uint32_t> *s, queue<uint32_t> *cached, queue<uint32_t> *nonCached);
bool        vtcIsPageInCacheLOCK(uint32_t pageInfo);
void        vtcInsertPageIntoCacheLOCK(uint32_t pageInfo, void * image_data);
void *      vtcRetrieveCachedPageLOCK(uint32_t pageInfo);
void        vtcReduceCacheIfNecessaryLOCK(clock_t currentTime);
void        _vtcRemoveCachedPage(uint32_t pageInfo);

void        vtUnmapPage(int mipmap_level, int x_coord, int y_coord, int x_storage_location, int y_storage_location);
void        vtUnmapPageCompleteley(int mipmap_level, int x_coord, int y_coord, int x_storage_location, int y_storage_location);

char        vtuFileExists(char *path);
void *      vtuLoadFile(const char *filePath, const uint32_t offset, uint32_t *file_size);
uint32_t *  vtuDownsampleImageRGB(const uint32_t *tex);
void        vtuPerspective(double m[4][4], double fovy, double aspect,    double zNear, double zFar);

void *      vtuDecompressImageFile(const char *imagePath, uint32_t *pic_size);
void *      vtuDecompressImageBuffer(const void *file_data, uint32_t file_size, uint32_t *pic_size);

#include "LibVT_Shaders.h"
GLuint      vtuCompileShaderWithPrelude(const char* prelude, const char* shaderSrc, GLenum type);
GLuint      vtuLoadShadersWithPrelude(const char* prelude, const char* vertSrc, const char* fragSrc);
