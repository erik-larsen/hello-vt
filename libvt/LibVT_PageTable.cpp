#include "LibVT_Internal.h"
#include "LibVT.h"

//void debugEraseCachedPages();
//#define DEBUG_ERASE_CACHED_PAGES_EVERY_FRAME

#if LONG_MIP_CHAIN
    #define MIP_INFO(mip)           (vt.cfg.mipChainLength - 1 - mip)
#else
    #define MIP_INFO(mip)           (vt.mipTranslation[mip])
#endif

#define TOUCH_MIP_ROW(mip, row)     {vt.mipLevelTouched[mip] = true; \
                                     vt.mipLevelMinrow[mip] = (vt.mipLevelMinrow[mip] < row) ? vt.mipLevelMinrow[mip] : row; \
                                     vt.mipLevelMaxrow[mip] = (vt.mipLevelMaxrow[mip] > row) ? vt.mipLevelMaxrow[mip] : row; }

void vtInitPageTable()
{
    // init translation tables, offsets and allocate page table
    uint32_t offsetCounter = 0;
    for (uint8_t i = 0; i < vt.cfg.mipChainLength; i++)
    {
        vt.mipTranslation[i] = (uint16_t) ((vt.cfg.virtTexDimensionPages >> i) - 1); // we do -1 here so we can add +1 in the shader to allow for a mip chain length 9 which results in the translation being 255/256, this is not ideal performance wise...
        vt.pageTableMipOffsets[i] = offsetCounter;
        offsetCounter += (vt.cfg.virtTexDimensionPages >> i) * (vt.cfg.virtTexDimensionPages >> i);
    }

    vt.pageTables = (uint32_t **) malloc(sizeof(uint32_t *) * vt.cfg.mipChainLength);
    assert(vt.pageTables);

    uint32_t *pageTableBuffer = (uint32_t *) calloc(1, 4 * offsetCounter);
    assert(pageTableBuffer);

    for (uint8_t i = 0; i < vt.cfg.mipChainLength; i++)
        vt.pageTables[i] = (uint32_t *)(pageTableBuffer + vt.pageTableMipOffsets[i]);
}

uint32_t * vtDownsampleImageRGB(const uint32_t *_tex)
{
    uint8_t *tex = (uint8_t *) _tex;
    uint8_t *smallTex = (uint8_t *)malloc((vt.cfg.pageDimension * vt.cfg.pageDimension * 3) / 4);
    assert(smallTex);

    for (uint16_t x = 0; x < vt.cfg.pageDimension / 2; x++)
    {
        for (uint16_t y = 0; y < vt.cfg.pageDimension / 2; y++)
        {
#ifdef COLOR_CODE_MIPPED_PHYSTEX
            smallTex[y * (vt.cfg.pageDimension / 2) * 3 + (x*3)] = 200;
            smallTex[y * (vt.cfg.pageDimension / 2) * 3 + (x*3) + 1] = 10;
            smallTex[y * (vt.cfg.pageDimension / 2) * 3 + (x*3) + 2] = 70;
#else
            uint8_t pix1 = tex[(y*2) * vt.cfg.pageDimension * 3 + (x*2*3)];
            uint8_t pix2 = tex[(y*2+1) * vt.cfg.pageDimension * 3 + (x*2*3)];
            uint8_t pix3 = tex[(y*2) * vt.cfg.pageDimension * 3 + (x*2*3+3)];
            uint8_t pix4 = tex[(y*2+1) * vt.cfg.pageDimension * 3 + (x*2*3+3)];

            smallTex[y * (vt.cfg.pageDimension / 2) * 3 + (x*3)] = (pix1 + pix2 + pix3 + pix4) / 4;

            pix1 = tex[(y*2) * vt.cfg.pageDimension * 3 + (x*2*3) + 1];
            pix2 = tex[(y*2+1) * vt.cfg.pageDimension * 3 + (x*2*3) + 1];
            pix3 = tex[(y*2) * vt.cfg.pageDimension * 3 + (x*2*3+3) + 1];
            pix4 = tex[(y*2+1) * vt.cfg.pageDimension * 3 + (x*2*3+3) + 1];

            smallTex[y * (vt.cfg.pageDimension / 2) * 3 + (x*3) + 1] = (pix1 + pix2 + pix3 + pix4) / 4;

            pix1 = tex[(y*2) * vt.cfg.pageDimension * 3 + (x*2*3) + 2];
            pix2 = tex[(y*2+1) * vt.cfg.pageDimension * 3 + (x*2*3) + 2];
            pix3 = tex[(y*2) * vt.cfg.pageDimension * 3 + (x*2*3+3) + 2];
            pix4 = tex[(y*2+1) * vt.cfg.pageDimension * 3 + (x*2*3+3) + 2];

            smallTex[y * (vt.cfg.pageDimension / 2) * 3 + (x*3) + 2] = (pix1 + pix2 + pix3 + pix4) / 4;
#endif
        }
    }

    return (uint32_t *)smallTex;
}

void mapPageFallbackEntries(int m, int x_coord, int y_coord, int mip, int x, int y) // TODO: test long mip chain
{
    const uint32_t pageEntry = PAGE_TABLE(m, x_coord, y_coord);

    if ((uint8_t) pageEntry != kTableMapped)
    {
        PAGE_TABLE(m, x_coord, y_coord) = (MIP_INFO(mip) << 24) + (x << 16) + (y << 8) + ((uint8_t) pageEntry);
        TOUCH_MIP_ROW(m, y_coord);

        if (m >= 1)
        {
            mapPageFallbackEntries(m - 1, x_coord * 2, y_coord * 2, mip, x, y);
            mapPageFallbackEntries(m - 1, x_coord * 2, y_coord * 2 + 1, mip, x, y);
            mapPageFallbackEntries(m - 1, x_coord * 2 + 1, y_coord * 2, mip, x, y);
            mapPageFallbackEntries(m - 1, x_coord * 2 + 1, y_coord * 2 + 1, mip, x, y);
        }
    }
}

void unmapPageFallbackEntries(int m, int x_coord, int y_coord, int x_search, int y_search, int mip_repl, int x_repl, int y_repl)
{
    const uint32_t pageEntry = PAGE_TABLE(m, x_coord, y_coord);

    if ((BYTE3(pageEntry) == x_search) && (BYTE2(pageEntry) == y_search))
    {
        PAGE_TABLE(m, x_coord, y_coord) = (mip_repl << 24) + (x_repl << 16) + (y_repl << 8) + ((uint8_t) pageEntry);
        TOUCH_MIP_ROW(m, y_coord);

        if (m >= 1)
        {
            unmapPageFallbackEntries(m - 1, x_coord * 2, y_coord * 2, x_search, y_search, mip_repl, x_repl, y_repl);
            unmapPageFallbackEntries(m - 1, x_coord * 2, y_coord * 2 + 1, x_search, y_search, mip_repl, x_repl, y_repl);
            unmapPageFallbackEntries(m - 1, x_coord * 2 + 1, y_coord * 2, x_search, y_search, mip_repl, x_repl, y_repl);
            unmapPageFallbackEntries(m - 1, x_coord * 2 + 1, y_coord * 2 + 1, x_search, y_search, mip_repl, x_repl, y_repl);
        }
    }
}

void vtUnmapPage(int mipmap_level, int x_coord, int y_coord, int x_storage_location, int y_storage_location)
{
    if (FALLBACK_ENTRIES)
    {
        const uint32_t pageEntry = PAGE_TABLE(mipmap_level + 1, x_coord / 2, y_coord / 2);
        *((uint8_t *)&PAGE_TABLE(mipmap_level, x_coord, y_coord)) = kTableFree;

        unmapPageFallbackEntries(mipmap_level, x_coord, y_coord, x_storage_location, y_storage_location, BYTE4(pageEntry), BYTE3(pageEntry), BYTE2(pageEntry));
    }
    else
    {
        PAGE_TABLE(mipmap_level, x_coord, y_coord) = kTableFree;
        TOUCH_MIP_ROW(mipmap_level, y_coord);
    }
}

void vtMapNewPages()
{
    queue<uint32_t>    newPages, zero;

#if !ENABLE_MT
    vtLoadNeededPages();
#endif
    {    // lock
        LOCK(vt.newPagesMutex)

        vt.missingPageCount = vt.neededPages.size(); // just stats keeping

        if (USE_PBO_PHYSTEX)
        {
            uint8_t i = 0;
            vt.newPageCount = 0;
            while (i < PBO_PHYSTEX_PAGES && !vt.newPages.empty())
            {
                newPages.push(vt.newPages.front());vt.newPages.pop();
                i++;
                vt.newPageCount++;  // just stats keeping
            }
        }
        else
        {
            newPages = vt.newPages;
            vt.newPageCount = newPages.size();  // just stats keeping
            vt.newPages = zero;
        }
    }    // unlock

    // we do this here instead of in vtLoadNeededPages() when new pages are actually mapped so it runs on the mainThread and the cachedPagesAccessTimes structure doesn't need to be locked
    vtReduceCacheIfNecessaryLOCK(vt.thisFrameClock);

    if (!newPages.empty())
    {
        bool foundSlot = true;
        const void *image_data;

        for (uint8_t i = 0; i < vt.cfg.mipChainLength; i++)
        {
#ifdef DEBUG_ERASE_CACHED_PAGES_EVERY_FRAME
            vt.mipLevelTouched[i] = true;
            vt.mipLevelMinrow[i] = 0;
            vt.mipLevelMaxrow[i] = (vt.cfg.virtTexDimensionPages >> i) - 1;
#else
            vt.mipLevelTouched[i] = false;
            vt.mipLevelMinrow[i] = (uint16_t) vt.cfg.virtTexDimensionPages >> i;
            vt.mipLevelMaxrow[i] = 0;
#endif
        }

#if USE_PBO_PHYSTEX
        uint8_t xCoordinatesForPageMapping[PBO_PHYSTEX_PAGES];
        uint8_t yCoordinatesForPageMapping[PBO_PHYSTEX_PAGES];
        uint8_t newPageCount = 0;

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, vt.pboPhystex);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, vt.cfg.pageMemsize * PBO_PHYSTEX_PAGES, 0, GL_STREAM_DRAW);

        uint8_t *phys_buffer = (uint8_t *)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
        assert(phys_buffer);
#endif
        glActiveTexture(GL_TEXTURE0 + TEXUNIT_FOR_PHYSTEX);

        while (!newPages.empty() && foundSlot)
        {
            const uint32_t pageInfo = newPages.front();newPages.pop();
            const uint16_t y_coord = EXTRACT_Y(pageInfo), x_coord = EXTRACT_X(pageInfo);
            const uint8_t mip = EXTRACT_MIP(pageInfo);

            image_data = vtRetrieveCachedPageLOCK(pageInfo);

            // find slot
            bool foundFree = false;
            uint8_t x, y, storedX = 0, storedY = 0;
            clock_t lowestClock = vt.thisFrameClock;

            // find least recently used or free page
            foundSlot = false;
            for (x = 0; x < vt.cfg.physTexDimensionPages; x++)
            {
                for (y = 0; y < vt.cfg.physTexDimensionPages; y++)
                {
                    if ((vt.textureStorageInfo[x][y].clockUsed < lowestClock) && (vt.textureStorageInfo[x][y].mip < vt.cfg.mipChainLength - HIGHEST_MIP_LEVELS_TO_KEEP))
                    {
                        lowestClock = vt.textureStorageInfo[x][y].clockUsed;
                        storedX = x;
                        storedY = y;
                        foundSlot = true;

                        if (lowestClock == 0)
                        {
                            foundFree = true;
                            break;
                        }
                    }
                }
                if (foundFree)
                    break;
            }

            if (foundSlot)
            {
                x = storedX;
                y = storedY;

                if (!foundFree)
                {
                    // unmap page
                    #if DEBUG_LOG > 0
                        printf("Thread %llu: Unloading page from VRAM: Mip:%u %u/%u from %u/%u lastUsed: %llu\n",
                            THREAD_ID, vt.textureStorageInfo[x][y].mip, vt.textureStorageInfo[x][y].x, vt.textureStorageInfo[x][y].y, x, y, (long long unsigned int)lowestClock);
                    #endif

                    vtUnmapPage(vt.textureStorageInfo[x][y].mip, vt.textureStorageInfo[x][y].x, vt.textureStorageInfo[x][y].y, x, y); // dont need complete version cause we map a new page at the same location
                }

                assert((x < vt.cfg.physTexDimensionPages) && (y < vt.cfg.physTexDimensionPages));

                // map page
                //vt.textureStorageInfo[x][y].active = true;
                vt.textureStorageInfo[x][y].x = x_coord;
                vt.textureStorageInfo[x][y].y = y_coord;
                vt.textureStorageInfo[x][y].mip = mip;
                vt.textureStorageInfo[x][y].clockUsed = vt.thisFrameClock;

                PAGE_TABLE(mip, x_coord, y_coord) = (MIP_INFO(mip) << 24) + (x << 16) + (y << 8) + kTableMapped;

                TOUCH_MIP_ROW(mip, y_coord);

                if (FALLBACK_ENTRIES)
                {
                    if (mip >= 1)
                    {
                        mapPageFallbackEntries(mip - 1, x_coord * 2, y_coord * 2, mip, x, y);
                        mapPageFallbackEntries(mip - 1, x_coord * 2, y_coord * 2 + 1, mip, x, y);
                        mapPageFallbackEntries(mip - 1, x_coord * 2 + 1, y_coord * 2, mip, x, y);
                        mapPageFallbackEntries(mip - 1, x_coord * 2 + 1, y_coord * 2 + 1, mip, x, y);
                    }
                }

#if USE_PBO_PHYSTEX
                memcpy(phys_buffer + vt.cfg.pageMemsize * newPageCount, image_data, vt.cfg.pageMemsize);
                xCoordinatesForPageMapping[newPageCount] = x;
                yCoordinatesForPageMapping[newPageCount] = y;

                newPageCount ++;
#else
                glTexSubImage2D(GL_TEXTURE_2D, 0, x * vt.cfg.pageDimension, y * vt.cfg.pageDimension, vt.cfg.pageDimension, vt.cfg.pageDimension, vt.cfg.pageDataFormat, vt.cfg.pageDataType, image_data);

                #if MIPPED_PHYSTEX
                    uint32_t *mippedData = vtDownsampleImageRGB((const uint32_t *)image_data);
                    glTexSubImage2D(GL_TEXTURE_2D, 1, x * (vt.cfg.pageDimension / 2), y * (vt.cfg.pageDimension / 2), (vt.cfg.pageDimension / 2), (vt.cfg.pageDimension / 2), vt.cfg.pageDataFormat, vt.cfg.pageDataType, mippedData);
                    free(mippedData);
                #endif
                #if DEBUG_LOG > 0
                    printf("Thread %llu: Loading page to VRAM: Mip:%u %u/%u to %u/%u\n", THREAD_ID, mip, x_coord, y_coord, x, y);
                #endif
#endif
            }
            else
            {    // lock
                LOCK(vt.newPagesMutex)

                printf("WARNING: skipping page loading because there are no free slots %i %i \n", vt.necessaryPageCount, vt.cfg.physTexDimensionPages * vt.cfg.physTexDimensionPages);
                vt.newPages.push(pageInfo);
                while (!newPages.empty())
                {
                    vt.newPages.push(newPages.front());newPages.pop();
                }
            }    // unlock
        }

#if USE_PBO_PHYSTEX
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

        for (uint8_t i = 0; i < newPageCount; i++)
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, xCoordinatesForPageMapping[i] * vt.cfg.pageDimension, yCoordinatesForPageMapping[i] * vt.cfg.pageDimension, vt.cfg.pageDimension, vt.cfg.pageDimension, vt.cfg.pageDataFormat, vt.cfg.pageDataType,  (uint8_t *) NULL + (i * vt.cfg.pageMemsize));
        }

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif

        glActiveTexture(GL_TEXTURE0 + TEXUNIT_FOR_PAGETABLE);

#if USE_PBO_PAGETABLE
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, vt.pboPagetable);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, (vt.pageTableMipOffsets[vt.cfg.mipChainLength - 1] + 1) * 4, 0, GL_STREAM_DRAW);

        uint32_t *table_buffer = (uint32_t *)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
        assert(table_buffer);
        memcpy(table_buffer, vt.pageTables[0], (vt.pageTableMipOffsets[vt.cfg.mipChainLength - 1] + 1) * 4);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
#endif

        // done, upload pageTable
        for (uint8_t i = 0; i < vt.cfg.mipChainLength; i++)
        {
            if (vt.mipLevelTouched[i] == true) // the whole touched, minrow, maxrow mess is there so we update only between the lowest and highest modified row, or nothing at all if no pixels are touched. ideally the page table updates should be much more finely grained than that.
            {
#if USE_PBO_PAGETABLE
                glTexSubImage2D(GL_TEXTURE_2D, i, 0, vt.mipLevelMinrow[i], vt.cfg.virtTexDimensionPages >> i, vt.mipLevelMaxrow[i] + 1 - vt.mipLevelMinrow[i], GL_RGBA, GL_UNSIGNED_BYTE, (uint32_t *) NULL + (vt.pageTableMipOffsets[i] + (vt.cfg.virtTexDimensionPages >> i) * vt.mipLevelMinrow[i]));
#else
                glTexSubImage2D(GL_TEXTURE_2D, i, 0, vt.mipLevelMinrow[i], vt.cfg.virtTexDimensionPages >> i, vt.mipLevelMaxrow[i] + 1 - vt.mipLevelMinrow[i], GL_RGBA, GL_UNSIGNED_BYTE, vt.pageTables[i] + (vt.cfg.virtTexDimensionPages >> i) * vt.mipLevelMinrow[i]);
#endif
            }
        }

#if USE_PBO_PAGETABLE
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif

        glActiveTexture(GL_TEXTURE0);
    }

    if (DYNAMIC_LOD_ADJUSTMENT)
    {    // automatic LoD bias adjustment
        // float std; // TODO: doing this here is a BUG

        // if (MIPPED_PHYSTEX)
        //     std = -0.5;
        // else
        //     std = 0.;

        int pageOverflow =    vt.necessaryPageCount + vt.cfg.residentPages - vt.cfg.physTexDimensionPages * vt.cfg.physTexDimensionPages;

        if (pageOverflow > -2.0f)
            vt.bias += 0.1f;

        if (pageOverflow < -7.0f && vt.bias > 0.0f)
            vt.bias -= 0.1f;
    }

#ifdef DEBUG_ERASE_CACHED_PAGES_EVERY_FRAME
    debugEraseCachedPages();
#endif

//    // testcode for performing quality tests. it spews out a list of loaded pages every frame. this can be compared against a reference list with pixel coverage information (produced by commented code in vtExtractNeededPages()). make sure the simulation runs at 60FPS and is at a specific walthrough position each frame.
//    for (int x = 0; x < vt.cfg.physTexDimensionPages; x++)
//    {
//        for (int y = 0; y < vt.cfg.physTexDimensionPages; y++)
//        {
//            if ((vt.textureStorageInfo[x][y].clockUsed == vt.thisFrameClock))
//            {
//                printf("PAGE: %i ", MAKE_PAGE_INFO(vt.textureStorageInfo[x][y].mip, vt.textureStorageInfo[x][y].x, vt.textureStorageInfo[x][y].y));
//            }
//        }
//    }
//    printf("\n\nNEWFRAME\n\n");
}

void debugEraseCachedPages()
{
#ifdef DEBUG_ERASE_CACHED_PAGES_EVERY_FRAME
    for (uint8_t i = 0; i < vt.cfg.mipChainLength; i++)
        for (uint16_t x = 0; x < (vt.cfg.virtTexDimensionPages >> i); x++)
            for (uint16_t y = 0; y < (vt.cfg.virtTexDimensionPages >> i); y++)
                PAGE_TABLE(i, x, y) = kTableFree;

    for (int x = 0; x < vt.cfg.physTexDimensionPages; x++)
    {
        for (int y = 0; y < vt.cfg.physTexDimensionPages; y++)
        {
            vt.textureStorageInfo[x][y].x = 0;
            vt.textureStorageInfo[x][y].y = 0;
            vt.textureStorageInfo[x][y].mip = 0;
            vt.textureStorageInfo[x][y].clockUsed = 0;
        }
    }
#endif
}
