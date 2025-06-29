#include "LibVT_Internal.h"
#include "LibVT.h"

extern vtData vt;
extern vtConfig c;

#if ENABLE_MT < 2
void vtLoadNeededPages()
{
    char buf[255];

#if ENABLE_MT
    const int limit = 1; // limit to 1 page load at a time
    while (!vt.shutdownThreads)
#else
    const int limit = 10;
#endif
    {
        queue<uint32_t>    neededPages;
        {    // lock
            LOCK(vt.neededPagesMutex)

            #if ENABLE_MT
                // sleep as long as there are no pages to be loaded, or shutdown requested
                vt.neededPagesAvailableCondition.wait(scoped_lock, [&]{ return !vt.neededPages.empty() || vt.shutdownThreads; });
                if (vt.shutdownThreads)
                    break;
            #endif

            uint8_t i = 0;    
            while(!vt.neededPages.empty() && i < limit) // TODO: all this copying could use preallocation of necessary space (not only here)
            {
                neededPages.push(vt.neededPages.front());
                vt.neededPages.pop_front();
                i ++;
            }

        }    // unlock

        while(!neededPages.empty())
        {
            const uint32_t pageInfo = neededPages.front();neededPages.pop();
            const uint16_t y_coord = EXTRACT_Y(pageInfo), x_coord = EXTRACT_X(pageInfo);
            const uint8_t mip = EXTRACT_MIP(pageInfo);
            void *image_data;

            // load tile from cache or harddrive
            if (!vtcIsPageInCacheLOCK(pageInfo))
            {
                // snprintf(buf, 255, "%s%stiles_b%u_level%u%stile_%u_%u_%u.%s", c.tileDir.c_str(), PATH_SEPERATOR, c.pageBorder, mip, PATH_SEPERATOR, mip, x_coord, vt.mipTranslation[mip] - y_coord, c.pageCodec.c_str()); // convert from lower left coordinates (opengl) to top left (tile store on disk)
                snprintf(buf, 255, "%s%stiles_b%u_level%u%stile_%u_%u_%u.%s", c.tileDir.c_str(), PATH_SEPERATOR, c.pageBorder, mip, PATH_SEPERATOR, mip, x_coord, y_coord, c.pageCodec.c_str());

                #if DEBUG_LOG > 0
                    printf("Thread %llu: Loading and decompressing page from disk: mip:%u %u/%u\n", THREAD_ID, mip, x_coord, y_coord);
                #endif

                image_data = vtuDecompressImageFile(buf, &c.pageDimension);

                vtcInsertPageIntoCacheLOCK(pageInfo, image_data);
            }
            //    else
            //        assert(0);

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
    char buf[255];

    const int limit = 1;
    while (!vt.shutdownThreads)
    {
        queue<uint32_t>    neededPages;
        {    // lock
            LOCK(vt.neededPagesMutex)

            {
                // sleep as long as there are no pages to be loaded, or shutdown requested
                vt.neededPagesAvailableCondition.wait(scoped_lock, [&]{ return !vt.neededPages.empty() || vt.shutdownThreads; });
                if (vt.shutdownThreads)
                    break;
            }

            uint8_t i = 0;    // limit to 5 pages at once
            while(!vt.neededPages.empty() && i < limit)
            {
                neededPages.push(vt.neededPages.front());vt.neededPages.pop_front();
                i ++;
            }
        }    // unlock

        while(!neededPages.empty())
        {
            const uint32_t pageInfo = neededPages.front();neededPages.pop();
            const uint16_t y_coord = EXTRACT_Y(pageInfo), x_coord = EXTRACT_X(pageInfo);
            const uint8_t mip = EXTRACT_MIP(pageInfo);

            // load tile from cache or harddrive
            if (!vtcIsPageInCacheLOCK(pageInfo))
            {
                // snprintf(buf, 255, "%s%stiles_b%u_level%u%stile_%u_%u_%u.%s", c.tileDir.c_str(), PATH_SEPERATOR, c.pageBorder, mip, PATH_SEPERATOR, mip, x_coord, vt.mipTranslation[mip] - y_coord, c.pageCodec.c_str()); // convert from lower left coordinates (opengl) to top left (tile store on disk)
                snprintf(buf, 255, "%s%stiles_b%u_level%u%stile_%u_%u_%u.%s", c.tileDir.c_str(), PATH_SEPERATOR, c.pageBorder, mip, PATH_SEPERATOR, mip, x_coord, y_coord, c.pageCodec.c_str());

                #if DEBUG_LOG > 0
                    printf("Thread %llu: Loading page from Disk: Mip:%u %u/%u (%i)\n", THREAD_ID, mip, x_coord, y_coord, pageInfo);
                #endif

                uint32_t size = 0;
                void *file_data = vtuLoadFile(buf, 0, &size);

                {    // lock
                    LOCK(vt.compressedMutex)

                    vt.newCompressedPages.push(pageInfo);
                    vt.compressedPages.insert(pair<uint32_t, void *>(pageInfo, file_data));
                    vt.compressedPagesSizes.insert(pair<uint32_t, uint32_t>(pageInfo, size));

                    vt.compressedPagesAvailableCondition.notify_one();
                }    // unlock
            }
        }
    }
}

void vtDecompressNeededPagesDecoupled()
{
    const int limit = 5;
    while (!vt.shutdownThreads)
    {
        queue<uint32_t>    neededPages;

        {    // lock
            LOCK(vt.compressedMutex)

            // sleep as long as there are no pages to be loaded, or shutdown requested
            vt.compressedPagesAvailableCondition.wait(scoped_lock, [&]{ return !vt.newCompressedPages.empty() || vt.shutdownThreads; });
            if (vt.shutdownThreads)
                break;

            uint8_t i = 0;    // limit to 5 pages at once
            while(!vt.newCompressedPages.empty() && i < limit)
            {
                neededPages.push(vt.newCompressedPages.front());vt.newCompressedPages.pop();
                i ++;
            }
        }    // unlock

        while(!neededPages.empty())
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

                void *image_data = vtuDecompressImageBuffer(file_data, size, &c.pageDimension);

                free(file_data);
                vtcInsertPageIntoCacheLOCK(pageInfo, image_data);

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
    char buf[255];

    while(!pagesToCache.empty())
    {
        const uint32_t pageInfo = pagesToCache.front();pagesToCache.pop();
        const uint16_t y_coord = EXTRACT_Y(pageInfo), x_coord = EXTRACT_X(pageInfo);
        const uint8_t mip = EXTRACT_MIP(pageInfo);
        void *image_data;


        // load tile from cache or harddrive
        if (!vtcIsPageInCacheLOCK(pageInfo))
        {
            snprintf(buf, 255, "%s%stiles_b%u_level%u%stile_%u_%u_%u.%s", 
                c.tileDir.c_str(), PATH_SEPERATOR, c.pageBorder, mip, PATH_SEPERATOR, mip, x_coord, vt.mipTranslation[mip] - y_coord, c.pageCodec.c_str()); // convert from lower left coordinates (opengl) to top left (tile store on disk)

            #if DEBUG_LOG > 0
                printf("Thread %llu: Caching page from disk: Mip:%u %u/%u\n", THREAD_ID, mip, x_coord, y_coord);
            #endif

            image_data = vtuDecompressImageFile(buf, &c.pageDimension);

            vtcInsertPageIntoCacheLOCK(pageInfo, image_data);
        }
    }
}
