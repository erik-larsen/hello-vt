#pragma once
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <assert.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef WIN32                // Windows
    #define PATH_SEPERATOR "\\"
#elif defined(__APPLE__)    // Mac
    #include <fcntl.h>      // For F_GLOBAL_NOCACHE
    #import <TargetConditionals.h>
    #define PATH_SEPERATOR "/"
#elif defined(linux)        // Linux
    #define PATH_SEPERATOR "/"
    #include <stdlib.h>
    #include <locale.h>
#else
    #error COULD_NOT_GUESS_TARGET_SYSTEM
#endif

#include <queue>
#include <map>
#include <string>

#include <thread>
#define THREAD_ID static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()))
#if ENABLE_MT
#include <atomic>
#include <mutex>
#include <condition_variable>
#endif

#include <OpenGLES/GLES2/gl2.h>
#include <OpenGLES/GLES2/gl2ext.h>
#define glBindFramebufferEXT glBindFramebuffer
#define GL_FRAMEBUFFER_EXT    GL_FRAMEBUFFER
#define glDeleteFramebuffersEXT glDeleteFramebuffers
#define glGenFramebuffersEXT glGenFramebuffers
#define glFramebufferTexture2DEXT glFramebufferTexture2D
#define glCheckFramebufferStatusEXT glCheckFramebufferStatus
#define GL_MAX_TEXTURE_IMAGE_UNITS_ARB GL_MAX_TEXTURE_IMAGE_UNITS
#define GL_UNSIGNED_INT_8_8_8_8_REV    GL_UNSIGNED_BYTE
#define GL_BGRA GL_BGRA_EXT

using namespace std;

#include "LibVT_Config.h"

void vtShutdown();
static inline void vt_fatal(const char *err, ...) {va_list ap; va_start (ap, err); vtShutdown(); vfprintf (stderr, err, ap); va_end (ap); exit(1); }

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

#define PAGE_TABLE(m, x, y)         (vt.pageTables[(m)][(y) * (vt.cfg.virtTexDimensionPages >> (m)) + (x)])
#define EXTRACT_MIP(page)           (BYTE1(page))

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

#define MAX_PHYS_TEX_DIMENSION_PAGES 64

struct storageInfo
{
    clock_t     clockUsed;
    uint16_t    x, y;
    uint8_t     mip;
};

struct vtConfig
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
    vtConfig                cfg;
    uint16_t                mipTranslation[12];
    uint32_t                pageTableMipOffsets[12];
    GLuint                  fbo, fboColorTexture, fboDepthTexture, physicalTexture, pageTableTexture, mipcalcTexture, pboReadback, pboPagetable, pboPhystex;

    bool                    mipLevelTouched[12];
    uint16_t                mipLevelMinrow[12];
    uint16_t                mipLevelMaxrow[12];
    storageInfo             textureStorageInfo[MAX_PHYS_TEX_DIMENSION_PAGES][MAX_PHYS_TEX_DIMENSION_PAGES];

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
    std::thread             loaderThread;
    std::atomic<bool>       shutdownThreads{false};
    std::condition_variable neededPagesAvailableCondition;
    std::mutex              neededPagesMutex;
    std::mutex              newPagesMutex;
    std::mutex              cachedPagesMutex;
#endif

#if ENABLE_MT > 1
    std::thread             decompressorThread;
    std::mutex              compressedMutex;
    queue<uint32_t>         newCompressedPages;
    map<uint32_t, void *>   compressedPages;
    map<uint32_t, uint32_t> compressedPagesSizes;
    std::condition_variable compressedPagesAvailableCondition;
#endif
};

extern vtData vt;

// LibVT_Config.cpp
void    vtInitConfig(const char *_tileDir, const char *_pageExtension, const uint8_t _pageBorder, const uint8_t _mipChainLength, const uint16_t _pageDimension);

// LibVT_PageLoading.cpp
void    vtInitPageLoader(const char *_tileDir);
void    vtLoadNeededPages();

// LibVT_PageCache.cpp
void    vtTouchCachedPage(uint32_t pageInfo);
void    vtSplitPagelistIntoCachedAndNoncachedLOCK(queue<uint32_t> *s, queue<uint32_t> *cached, queue<uint32_t> *nonCached);
bool    vtIsPageInCacheLOCK(uint32_t pageInfo);
void    vtInsertPageIntoCacheLOCK(uint32_t pageInfo, void * image_data);
void *  vtRetrieveCachedPageLOCK(uint32_t pageInfo);
void    vtReduceCacheIfNecessaryLOCK(clock_t currentTime);

// LibVT_PageTable.cpp
void    vtInitPageTable();
