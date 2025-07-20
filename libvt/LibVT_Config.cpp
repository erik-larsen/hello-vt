#include "LibVT_Internal.h"

void vtInitConfig(const char *_tileDir, const char *_pageExtension, const uint8_t _pageBorder, const uint8_t _mipChainLength, const uint16_t _pageDimension)
{
    // validate configuration
    #if (FALLBACK_ENTRIES == 1) && (HIGHEST_MIP_LEVELS_TO_KEEP == 0)
        #error FALLBACK_ENTRIES requires HIGHEST_MIP_LEVELS_TO_KEEP >= 1
    #endif

    if (vt.cfg.tileDir != "") vt_fatal("Error: calling vtInit() twice ain't good!\n");

    assert((VT_MAG_FILTER == GL_NEAREST) || (VT_MAG_FILTER == GL_LINEAR));
    assert((VT_MIN_FILTER == GL_NEAREST) || (VT_MIN_FILTER == GL_LINEAR) || (VT_MIN_FILTER == GL_NEAREST_MIPMAP_NEAREST) || (VT_MIN_FILTER == GL_LINEAR_MIPMAP_NEAREST) || (VT_MIN_FILTER == GL_NEAREST_MIPMAP_LINEAR) || (VT_MIN_FILTER == GL_LINEAR_MIPMAP_LINEAR));
    assert(TEXUNIT_FOR_PHYSTEX !=  TEXUNIT_FOR_PAGETABLE);
    #if LONG_MIP_CHAIN
        assert((_mipChainLength >= 10) && (_mipChainLength <= 11));
    #else
        assert((_mipChainLength >= 2) && (_mipChainLength <= 9));
    #endif
    assert((_pageDimension == 64) || (_pageDimension == 128) || (_pageDimension == 256) || (_pageDimension == 512));
    assert((PREPASS_RESOLUTION_REDUCTION_SHIFT >= 0) && (PREPASS_RESOLUTION_REDUCTION_SHIFT <= 4));
    assert((MAX_RAMCACHE_MB >= 50));
    assert((HIGHEST_MIP_LEVELS_TO_KEEP >= 0) && (HIGHEST_MIP_LEVELS_TO_KEEP <= 5) && ((float)HIGHEST_MIP_LEVELS_TO_KEEP <= _mipChainLength));
    assert(!(READBACK_MODE_NONE && USE_PBO_READBACK));
    assert(!((MIPPED_PHYSTEX == 1) && (USE_PBO_PHYSTEX == 1))); // TODO: support these combinations

    #if GL_ES_VERSION_2_0
        assert(READBACK_MODE == kBackbufferReadPixels);
        assert(ANISOTROPY == 0);
        assert((VT_MIN_FILTER == GL_NEAREST) || (VT_MIN_FILTER == GL_LINEAR));
        assert(USE_PBO_READBACK == 0);
        assert(USE_PBO_PAGETABLE == 0);
        assert(USE_PBO_PHYSTEX == 0);
        assert(FALLBACK_ENTRIES == 1);
    #endif
    if (LONG_MIP_CHAIN && !USE_MIPCALC_TEXTURE)
        printf("Warning: expect artifacts when using LONG_MIP_CHAIN && !USE_MIPCALC_TEXTURE\n");

    #if VT_MAG_FILTER == GL_LINEAR || VT_MIN_FILTER == GL_LINEAR || VT_MIN_FILTER == GL_LINEAR_MIPMAP_NEAREST || VT_MIN_FILTER == GL_LINEAR_MIPMAP_LINEAR
        if (_pageBorder > 1 && _pageBorder > ANISOTROPY / 2)
            printf("Warning: PAGE_BORDER bigger than necessary for filtering with GL_LINEAR* and given ANISOTROPY\n");
        else if (_pageBorder < 1)
            printf("Warning: PAGE_BORDER must be at least 1 for filtering with GL_LINEAR*\n");
        else if (_pageBorder < float(ANISOTROPY) / 2.0)
            printf("Warning: PAGE_BORDER too small for given ANISOTROPY\n");
    #else
        if (_pageBorder > 0)
            printf("Warning: PAGE_BORDER not necessary for filtering with GL_NEAREST*\n");
    #endif

    // initialize and calculate configuration
    vt.cfg.tileDir = string(_tileDir);
    vt.cfg.pageCodec = string(_pageExtension);
    vt.cfg.pageDataFormat = GL_RGB;
    vt.cfg.pageDataType = GL_UNSIGNED_BYTE;
    vt.cfg.pageMemsize = (_pageDimension * _pageDimension * 3);
    vt.cfg.physTexDimensionPages = PHYS_TEX_DIMENSION / _pageDimension;
    assert(vt.cfg.physTexDimensionPages <= MAX_PHYS_TEX_DIMENSION_PAGES);

    vt.cfg.maxCachedPages = (int)((float)MAX_RAMCACHE_MB / ((float)vt.cfg.pageMemsize / (1024.0 * 1024.0)));
    for (uint8_t i = 0; i < float(HIGHEST_MIP_LEVELS_TO_KEEP); i++)
        vt.cfg.residentPages += (uint32_t) powf(4, i);

    vt.cfg.pageBorder = _pageBorder;
    vt.cfg.mipChainLength = _mipChainLength;
    vt.cfg.virtTexDimensionPages = 2 << (vt.cfg.mipChainLength - 2);
    vt.cfg.pageDimension = _pageDimension;
}