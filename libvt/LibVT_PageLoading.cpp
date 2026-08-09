#include "LibVT_Internal.h"
#include "LibVT.h"

// TODO: Consolidate duplicate code blocks below

#ifdef __EMSCRIPTEN__

// On the web, tiles are fetched from the server via (same-origin) URLs instead of
// being read from disk. We use *synchronous* emscripten_fetch(), which is legal
// because all callers of these functions run on pthreads (web workers), never on
// the browser main thread (see vtInitPageLoader below, which skips the main-thread
// probing/precaching that the native build performs).

#include <emscripten/fetch.h>
#include <string.h>

char vtFileExists(char *path)
{
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "HEAD");
    attr.attributes = EMSCRIPTEN_FETCH_SYNCHRONOUS;

    emscripten_fetch_t *fetch = emscripten_fetch(&attr, path);
    char exists = (fetch && fetch->status == 200);
    if (fetch)
        emscripten_fetch_close(fetch);

    printf("Thread %llu: URL %s: %s\n", THREAD_ID, exists ? "exists" : "does not exist", path);
    return exists;
}

void * vtLoadFile(const char *filePath, const uint32_t offset, uint32_t *file_size)
{
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;

    emscripten_fetch_t *fetch = emscripten_fetch(&attr, filePath);
    if (!fetch || fetch->status != 200 || fetch->numBytes <= offset)
    {
        printf("Error: tried to load nonexisting URL: %s (HTTP status %d)\n", filePath, fetch ? fetch->status : -1);
        if (fetch)
            emscripten_fetch_close(fetch);
        return NULL;
    }

    uint32_t fs = 0;
    uint32_t *fsp = (file_size != NULL) ? file_size : &fs;

    if (*fsp != 0)
        *fsp = *fsp - offset;       // caller knows the expected size
    else
        *fsp = ((uint32_t) fetch->numBytes) - offset;

    char *fileData = (char *) malloc(*fsp);
    assert(fileData);
    memcpy(fileData, fetch->data + offset, *fsp);

    emscripten_fetch_close(fetch);

    return fileData;
}

#else // native file I/O

char vtFileExists(char *path)
{
    FILE *f;

    f = fopen(path, "r");
    if (f)
    {
        fclose(f);
        printf("Thread %llu: File exists: %s\n", THREAD_ID, path);
        return 1;
    }
    else {
        printf("Thread %llu: File does not exist: %s\n", THREAD_ID, path);
        return 0;
    }
}

void * vtLoadFile(const char *filePath, const uint32_t offset, uint32_t *file_size)
{
    uint32_t fs = 0;
    uint32_t *fsp = &fs;

    FILE *f = fopen(filePath, "rb");
    if (!f)
    {
        printf("Error: tried to load nonexisting file");
        return NULL;
    }
#if defined(__APPLE__)
    fcntl(f->_file, F_GLOBAL_NOCACHE, 1); // prevent the OS from caching this file in RAM
#endif
    assert(f);

    if (file_size != NULL)
        fsp = file_size;

    if (*fsp != 0)
        *fsp = *fsp - offset;
    else
    {
        fseek(f , 0 , SEEK_END);
        *fsp = ftell(f) - offset;
    }

    fseek(f, offset, SEEK_SET);

    char *fileData = (char *) malloc(*fsp);
    assert(fileData);

    #ifdef DEBUG
        size_t result = fread(fileData, 1, *fsp, f);
        assert(result == *fsp);
    #else
        fread(fileData, 1, *fsp, f);
    #endif

    fclose (f);

    return fileData;
}

#endif // __EMSCRIPTEN__

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void * vtDecompressImageBuffer(const void *file_data, uint32_t file_size, uint32_t *pic_size)
{
    #if DEBUG_LOG > 0
        printf("Thread %llu: Decompress image in-memory: %p\n", THREAD_ID, file_data);
    #endif

    int width, height, bitdepth;
    void * image_data = stbi_load_from_memory((const stbi_uc *)file_data, file_size, &width, &height, &bitdepth, STBI_rgb);

    // verify image
    assert(image_data);

    if (*pic_size == 0)
        *pic_size = width;
    else
        assert(((uint32_t)width == *pic_size) && ((uint32_t)height == *pic_size));

    return image_data;
}

void * vtDecompressImageFile(const char *imagePath, uint32_t *pic_size)
{
    #if DEBUG_LOG > 0
        printf("Thread %llu: Load & decompress image file: %s\n", THREAD_ID, imagePath);
    #endif

    // Load the raw file data into a buffer first
    uint32_t file_size = 0;
    void *file_data = vtLoadFile(imagePath, 0, &file_size);
    if (file_data && file_size > 0)
    {
        // Now decompress from that buffer
        void *image_data = vtDecompressImageBuffer(file_data, file_size, pic_size);

        // Free the intermediate compressed buffer
        free(file_data);
        return image_data;
    }
    else
        return NULL;
}

#if ENABLE_MT < 2
void vtLoadNeededPages()
{
    char imagePath[255];

#if ENABLE_MT
    const int pageLimit = 1; // limit to 1 page load at a time
    while (!vt.shutdownThreads)
#else
    const int pageLimit = 10; // limit to 10 pages loaded at a time
#endif
    {
        queue<uint32_t> neededPages;
        {    // lock
            LOCK(vt.neededPagesMutex)

            #if ENABLE_MT
                // sleep as long as there are no pages to be loaded, or shutdown requested
                vt.neededPagesAvailableCondition.wait(scoped_lock, [&]{ return !vt.neededPages.empty() || vt.shutdownThreads; });
                if (vt.shutdownThreads)
                    break;
            #endif

            uint8_t i = 0;
            while (!vt.neededPages.empty() && i < pageLimit) // TODO: all this copying could use preallocation of necessary space (not only here)
            {
                neededPages.push(vt.neededPages.front());
                vt.neededPages.pop_front();
                ++i;
            }
        }    // unlock

        while (!neededPages.empty())
        {
            const uint32_t pageInfo = neededPages.front();neededPages.pop();
            const uint16_t y_coord = EXTRACT_Y(pageInfo), x_coord = EXTRACT_X(pageInfo);
            const uint8_t mip = EXTRACT_MIP(pageInfo);

            // load tile from cache or harddrive
            if (!vtIsPageInCacheLOCK(pageInfo))
            {
                snprintf(imagePath, 255, "%s%stiles_b%u_level%u%stile_%u_%u_%u.%s", vt.cfg.tileDir.c_str(), PATH_SEPERATOR, vt.cfg.pageBorder, mip, PATH_SEPERATOR, mip, x_coord, y_coord, vt.cfg.pageCodec.c_str());

                #if DEBUG_LOG > 0
                    printf("Thread %llu: Loading and decompressing page from disk: mip:%u %u/%u\n", THREAD_ID, mip, x_coord, y_coord);
                #endif

                void *image_data = vtDecompressImageFile(imagePath, &vt.cfg.pageDimension);

                vtInsertPageIntoCacheLOCK(pageInfo, image_data);
            }

            // usleep(500000); // for testin' what happens when pages are loaded slowly
            {    // lock
                LOCK(vt.newPagesMutex)
                vt.newPages.push(pageInfo);
            }    // unlock
        }
    }
}
#else
void vtLoadNeededPagesDecoupled()
{
    char imagePath[255];

    const int pageLimit = 1; // limit to 1 page load at a time
    while (!vt.shutdownThreads)
    {
        queue<uint32_t> neededPages;
        {    // lock
            LOCK(vt.neededPagesMutex)

            {
                // sleep as long as there are no pages to be loaded, or shutdown requested
                vt.neededPagesAvailableCondition.wait(scoped_lock, [&]{ return !vt.neededPages.empty() || vt.shutdownThreads; });
                if (vt.shutdownThreads)
                    break;
            }

            uint8_t i = 0;    
            while (!vt.neededPages.empty() && i < pageLimit)
            {
                neededPages.push(vt.neededPages.front());
                vt.neededPages.pop_front();
                ++i;
            }
        }    // unlock

        while (!neededPages.empty())
        {
            const uint32_t pageInfo = neededPages.front();neededPages.pop();
            const uint16_t y_coord = EXTRACT_Y(pageInfo), x_coord = EXTRACT_X(pageInfo);
            const uint8_t mip = EXTRACT_MIP(pageInfo);

            // load tile from cache or harddrive
            if (!vtIsPageInCacheLOCK(pageInfo))
            {
                snprintf(imagePath, 255, "%s%stiles_b%u_level%u%stile_%u_%u_%u.%s", vt.cfg.tileDir.c_str(), PATH_SEPERATOR, vt.cfg.pageBorder, mip, PATH_SEPERATOR, mip, x_coord, y_coord, vt.cfg.pageCodec.c_str());

                #if DEBUG_LOG > 0
                    printf("Thread %llu: Loading page from disk: Mip:%u %u/%u (%i)\n", THREAD_ID, mip, x_coord, y_coord, pageInfo);
                #endif

                uint32_t file_size = 0;
                void *file_data = vtLoadFile(imagePath, 0, &file_size);
                if (file_data && file_size > 0)
                {    // lock
                    LOCK(vt.compressedMutex)

                    vt.newCompressedPages.push(pageInfo);
                    vt.compressedPages.insert(pair<uint32_t, void *>(pageInfo, file_data));
                    vt.compressedPagesSizes.insert(pair<uint32_t, uint32_t>(pageInfo, file_size));

                    vt.compressedPagesAvailableCondition.notify_one();
                }    // unlock
            }
        }
    }
}

void vtDecompressNeededPagesDecoupled()
{
    const int pageLimit = 5;  // limit to 5 pages at once
    while (!vt.shutdownThreads)
    {
        queue<uint32_t>    neededPages;

        {    // lock
            LOCK(vt.compressedMutex)

            // sleep as long as there are no pages to be loaded, or shutdown requested
            vt.compressedPagesAvailableCondition.wait(scoped_lock, [&]{ return !vt.newCompressedPages.empty() || vt.shutdownThreads; });
            if (vt.shutdownThreads)
                break;

            uint8_t i = 0;   
            while (!vt.newCompressedPages.empty() && i < pageLimit)
            {
                neededPages.push(vt.newCompressedPages.front());vt.newCompressedPages.pop();
                ++i;
            }
        }    // unlock

        while (!neededPages.empty())
        {
            const uint32_t pageInfo = neededPages.front();neededPages.pop();
            void *file_data;
            uint32_t size;

            {    // lock
                LOCK(vt.compressedMutex)

                file_data = vt.compressedPages.find(pageInfo)->second;
                size = vt.compressedPagesSizes.find(pageInfo)->second;

                vt.compressedPages.erase(pageInfo);
                vt.compressedPagesSizes.erase(pageInfo);
            }    // unlock

            if (file_data && size) // this prevents problems because pages can be added twice because they are already loaded but not decompressed
            {
                #if DEBUG_LOG > 0
                    const uint16_t y_coord = EXTRACT_Y(pageInfo), x_coord = EXTRACT_X(pageInfo);
                    const uint8_t mip = EXTRACT_MIP(pageInfo);
                    printf("Thread %llu: Decompressing page from buffer: Mip:%u %u/%u (%i)\n", THREAD_ID, mip, x_coord, y_coord, pageInfo);
                #endif

                void *image_data = vtDecompressImageBuffer(file_data, size, &vt.cfg.pageDimension);

                free(file_data);
                vtInsertPageIntoCacheLOCK(pageInfo, image_data);

                {    // lock
                    LOCK(vt.newPagesMutex)
                    vt.newPages.push(pageInfo);
                }    // unlock
            }
        }
    }
}
#endif

void vtCachePages(queue<uint32_t> pagesToCache)
{
    char imagePath[255];

    while (!pagesToCache.empty())
    {
        const uint32_t pageInfo = pagesToCache.front();pagesToCache.pop();
        const uint16_t y_coord = EXTRACT_Y(pageInfo), x_coord = EXTRACT_X(pageInfo);
        const uint8_t mip = EXTRACT_MIP(pageInfo);

        // load tile from cache or harddrive
        if (!vtIsPageInCacheLOCK(pageInfo))
        {
            // convert from lower left coordinates (opengl) to top left (tile store on disk)
            snprintf(imagePath, 255, "%s%stiles_b%u_level%u%stile_%u_%u_%u.%s",
                vt.cfg.tileDir.c_str(), PATH_SEPERATOR, vt.cfg.pageBorder, mip, PATH_SEPERATOR, mip, x_coord, vt.mipTranslation[mip] - y_coord, vt.cfg.pageCodec.c_str());

            #if DEBUG_LOG > 0
                printf("Thread %llu: Caching page from disk: Mip:%u %u/%u\n", THREAD_ID, mip, x_coord, y_coord);
            #endif

            void *image_data = vtDecompressImageFile(imagePath, &vt.cfg.pageDimension);

            vtInsertPageIntoCacheLOCK(pageInfo, image_data);
        }
    }
}

void vtInitPageLoader(const char *_tileDir)
{
#ifndef __EMSCRIPTEN__
    // check the tile store
    char buf[255];
    for (uint8_t i = 0; i < 16; i++)
    {
        snprintf(buf, 255, "%s%stiles_b%u_level%u%stile_%u_0_0.%s", _tileDir, PATH_SEPERATOR, vt.cfg.pageBorder, i, PATH_SEPERATOR, i, vt.cfg.pageCodec.c_str());

        if (vtFileExists(buf) != (i < vt.cfg.mipChainLength))
            vt_fatal("Error: %s doesn't seem to be a page store with MIP_CHAIN_LENGTH = %u, vt.cfg.pageCodec.c_str() = %s and vt.cfg.pageBorder = %u!", vt.cfg.tileDir.c_str(), vt.cfg.mipChainLength, vt.cfg.pageCodec.c_str(), vt.cfg.pageBorder);
    }

    // precache some pages
    queue<uint32_t>    pagesToCache;
    for (uint8_t i = vt.cfg.mipChainLength - HIGHEST_MIP_LEVELS_TO_PRECACHE; i < vt.cfg.mipChainLength; i++)
        for (uint8_t x = 0; x < (vt.cfg.virtTexDimensionPages >> i); x++)
            for (uint8_t y = 0; y < (vt.cfg.virtTexDimensionPages >> i); y++)
                pagesToCache.push(MAKE_PAGE_INFO(i, x, y));
    vtCachePages(pagesToCache);
#else
    // On the web we cannot perform synchronous fetches on the browser main thread,
    // so skip the tile store probing and the blocking precache. The resident pages
    // pushed below are loaded asynchronously by the loader thread instead. If the
    // tile store URL or config is wrong, tile fetches will 404 with errors logged
    // to the console. (Note: with HIGHEST_MIP_LEVELS_TO_PRECACHE > HIGHEST_MIP_LEVELS_TO_KEEP
    // some pages would not be precached on the web; both are 1 by default.)
#endif

    // push the resident pages
    for (uint8_t i = vt.cfg.mipChainLength - HIGHEST_MIP_LEVELS_TO_KEEP; i < vt.cfg.mipChainLength; i++)
        for (uint8_t x = 0; x < (vt.cfg.virtTexDimensionPages >> i); x++)
            for (uint8_t y = 0; y < (vt.cfg.virtTexDimensionPages >> i); y++)
                vt.neededPages.push_back(MAKE_PAGE_INFO(i, x, y));

    #if ENABLE_MT == 1
        vt.loaderThread = std::thread(&vtLoadNeededPages);
    #elif ENABLE_MT == 2
        vt.loaderThread = std::thread(&vtLoadNeededPagesDecoupled);
        vt.decompressorThread = std::thread(&vtDecompressNeededPagesDecoupled);
    #endif
}

bool vtScan(const char *_tileDir, char * _pageExtension, uint8_t *_pageBorder, uint8_t *_mipChainLength, uint32_t *_pageDimension)
{
    bool success = false;
    DIR *dp;
    struct dirent *ep;
    string tilestring = string("");
    string codec = string("    ");

    *_mipChainLength = (uint8_t)0;

    dp = opendir (_tileDir);
    if (dp != NULL)
    {
        while ((ep = readdir(dp)))
        {
            int level, border;
            string dir = string(ep->d_name);

            if (dir.find("tiles_b", 0) != string::npos)
            {
                sscanf(ep->d_name, "tiles_b%d_level%d", &border, &level);

                *_pageBorder = border;
                if (tilestring == "") tilestring = string(ep->d_name);
                if (++level > *_mipChainLength) *_mipChainLength = level;
            }
        }
        closedir(dp);

        dp = opendir (string(string(_tileDir) + string("/") + string (tilestring)).c_str());
        if (dp != NULL)
        {
            while ((ep = readdir(dp)))
            {
                string file = string(ep->d_name);

                if (file.find("tile_", 0) != string::npos)
                {
                    uint32_t len = (uint32_t) file.length();
                    codec = file.substr(len - 4);
                    if (codec[0] == '.')
                        codec = codec.substr(1);
                    *_pageDimension = 0;
                    void *image = vtDecompressImageFile(string(string(_tileDir) + string("/") + string (tilestring) + string("/") + file).c_str(), _pageDimension);
                    free(image);
                    success = true;
                    break;
                }
            }
            closedir(dp);
        }
    }

    _pageExtension[0] = codec[0];
    _pageExtension[1] = codec[1];
    _pageExtension[2] = codec[2];
    _pageExtension[3] = codec[3];

    return success;
}
