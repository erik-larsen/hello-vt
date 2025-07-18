#include "LibVT_Internal.h"
#include "LibVT.h"

void vtRemoveCachedPage(uint32_t pageInfo)
{
    void *data = vt.cachedPages[pageInfo];
    free(data);
    vt.cachedPages.erase(pageInfo);
    vt.cachedPagesAccessTimes.erase(pageInfo);
}

void vtRemoveCachedPageLOCK(uint32_t pageInfo)
{
    LOCK(vt.cachedPagesMutex)
    return vtRemoveCachedPage(pageInfo);
}

void vtTouchCachedPage(uint32_t pageInfo)
{
    vt.cachedPagesAccessTimes[pageInfo] = vt.thisFrameClock;
}

void vtSplitPagelistIntoCachedAndNoncachedLOCK(queue<uint32_t> *s, queue<uint32_t> *cached, queue<uint32_t> *nonCached)
{
    LOCK(vt.cachedPagesMutex)

    while (!s->empty())
    {
        uint32_t page = s->front();

        if (vt.cachedPages.count(page))
            cached->push(page);
        else
            nonCached->push(page);

        s->pop();
    }
}

bool vtIsPageInCacheLOCK(uint32_t pageInfo)
{
    LOCK(vt.cachedPagesMutex)
    return (vt.cachedPages.count(pageInfo) > 0);
}

void vtInsertPageIntoCacheLOCK(uint32_t pageInfo, void * image_data)
{
    LOCK(vt.cachedPagesMutex)
    vt.cachedPages.insert(pair<uint32_t, void *>(pageInfo, image_data));
}

void * vtRetrieveCachedPageLOCK(uint32_t pageInfo)
{
    LOCK(vt.cachedPagesMutex)
    assert(vt.cachedPages.count(pageInfo));
    return vt.cachedPages.find(pageInfo)->second;
}

void vtReduceCacheIfNecessaryLOCK(clock_t currentTime)
{
    LOCK(vt.cachedPagesMutex)

    uint32_t size = (uint32_t)vt.cachedPages.size();

    if (size > vt.cfg.maxCachedPages)
    {
        uint32_t pagesToErase = (size - vt.cfg.maxCachedPages) * 4;
        if (pagesToErase > (vt.cfg.maxCachedPages / 10)) pagesToErase = vt.cfg.maxCachedPages / 10;
        multimap<clock_t, uint32_t> oldestPages;

        #if DEBUG_LOG > 0
            printf("Thread %llu: RAM-cache has %i too many pages - erasing the %i least recently touched pages!\n", THREAD_ID, (size - vt.cfg.maxCachedPages), pagesToErase);
        #endif

        for (uint32_t i = 0; i < pagesToErase; i++)
            oldestPages.insert(pair<clock_t, uint32_t>(currentTime+1+i, i));

        map<uint32_t, clock_t>::iterator cachedIter;
        for (cachedIter = vt.cachedPagesAccessTimes.begin(); cachedIter != vt.cachedPagesAccessTimes.end(); ++cachedIter)
        {
            if (cachedIter->second < oldestPages.rbegin()->first)
            {
                oldestPages.insert(pair<clock_t, uint32_t>(cachedIter->second, cachedIter->first));

                oldestPages.erase(--oldestPages.rbegin().base()); // this really is the easiest way to just erase the last element - C++ sucks
            }
        }

        assert(oldestPages.size() == pagesToErase);

        multimap<clock_t, uint32_t>::iterator oldestIter;
        for (oldestIter = oldestPages.begin(); oldestIter != oldestPages.end(); ++oldestIter)
        {
            uint32_t pageInfo = oldestIter->second;
            vtRemoveCachedPage(pageInfo);

            #if DEBUG_LOG > 1
                printf("Thread %llu: Un-loading page from RAM-cache: Mip:%u %u/%u\n", THREAD_ID, EXTRACT_MIP(pageInfo), EXTRACT_X(pageInfo), EXTRACT_Y(pageInfo));
            #endif
        }
    }
}
