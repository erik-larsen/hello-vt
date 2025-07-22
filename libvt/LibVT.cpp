#include "LibVT_Internal.h"
#include "LibVT.h"

vtData vt;

void vtInit(const char *_tileDir, const char *_pageExtension, const uint8_t _pageBorder, const uint8_t _mipChainLength, const uint16_t _pageDimension)
{
    vtInitConfig(_tileDir, _pageExtension, _pageBorder, _mipChainLength, _pageDimension);
    vtInitPageTable();
    vtInitPageLoader(_tileDir);
}

void vtPrepare(const GLuint readbackShader, const GLuint renderVTShader)
{
    GLint max_texture_size, max_texture_units;

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS_ARB, &max_texture_units);
    assert((PHYS_TEX_DIMENSION >= 2048) && (PHYS_TEX_DIMENSION <= max_texture_size) && (vt.cfg.physTexDimensionPages * vt.cfg.pageDimension == PHYS_TEX_DIMENSION));
    assert((TEXUNIT_FOR_PAGETABLE >= 0) && (TEXUNIT_FOR_PAGETABLE < max_texture_units));
    assert((TEXUNIT_FOR_PHYSTEX >= 0) && (TEXUNIT_FOR_PHYSTEX < max_texture_units));

    if (renderVTShader)
    {
        glUseProgram(renderVTShader);
            glUniform1i(glGetUniformLocation(renderVTShader, "pageTableTexture"), TEXUNIT_FOR_PAGETABLE);
            glUniform1i(glGetUniformLocation(renderVTShader, "physicalTexture"), TEXUNIT_FOR_PHYSTEX);
        glUseProgram(0);
    }
    if (readbackShader)
    {
        glUseProgram(readbackShader);
            glUniform1i(glGetUniformLocation(readbackShader, "mipcalcTexture"), TEXUNIT_FOR_MIPCALC);
        glUseProgram(0);
    }

    glGenTextures(1, &vt.physicalTexture);
    glActiveTexture(GL_TEXTURE0 + TEXUNIT_FOR_PHYSTEX);
    glBindTexture(GL_TEXTURE_2D, vt.physicalTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, VT_MAG_FILTER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, VT_MIN_FILTER);

#if ANISOTROPY
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, ANISOTROPY);
#endif

    glTexImage2D(GL_TEXTURE_2D, 0, vt.cfg.pageDataFormat == GL_RGB ? GL_RGB : GL_RGBA, PHYS_TEX_DIMENSION, PHYS_TEX_DIMENSION, 0, vt.cfg.pageDataFormat, vt.cfg.pageDataType, NULL);

    if (MIPPED_PHYSTEX)
    {
#if !GL_ES_VERSION_2_0
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
#endif
        glTexImage2D(GL_TEXTURE_2D, 1, vt.cfg.pageDataFormat == GL_RGB ? GL_RGB : GL_RGBA, PHYS_TEX_DIMENSION / 2, PHYS_TEX_DIMENSION / 2, 0, vt.cfg.pageDataFormat, vt.cfg.pageDataType, NULL);
    }

    glGenTextures(1, &vt.pageTableTexture);
    glActiveTexture(GL_TEXTURE0 + TEXUNIT_FOR_PAGETABLE);
    glBindTexture(GL_TEXTURE_2D, vt.pageTableTexture);
#if !GL_ES_VERSION_2_0
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, vt.cfg.mipChainLength - 1);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    for (uint8_t i = 0; i < vt.cfg.mipChainLength; i++)
        glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA, vt.cfg.virtTexDimensionPages >> i, vt.cfg.virtTexDimensionPages >> i, 0, GL_RGBA, GL_UNSIGNED_BYTE, vt.pageTables[i]); // {GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV} doesn't seem to be faster

    if (USE_MIPCALC_TEXTURE)
    {
        glGenTextures(1, &vt.mipcalcTexture);
        glActiveTexture(GL_TEXTURE0 + TEXUNIT_FOR_MIPCALC);
        glBindTexture(GL_TEXTURE_2D, vt.mipcalcTexture);
#if !GL_ES_VERSION_2_0
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, vt.cfg.mipChainLength - 1);
#endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vt.cfg.virtTexDimensionPages, vt.cfg.virtTexDimensionPages, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); // {GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV} doesn't seem to be faster

        uint32_t **mipcalcTables = (uint32_t **) malloc(sizeof(uint32_t *) * vt.cfg.mipChainLength);
        for (uint8_t i = 0; i < vt.cfg.mipChainLength; i++)
        {
            mipcalcTables[i] = (uint32_t *) malloc(4 * (vt.cfg.virtTexDimensionPages >> i) * (vt.cfg.virtTexDimensionPages >> i));

            for (uint16_t x = 0; x < (vt.cfg.virtTexDimensionPages >> i); x++)
            {
                for (uint16_t y = 0; y < (vt.cfg.virtTexDimensionPages >> i); y++)
                {
                    if (LONG_MIP_CHAIN)
                        (mipcalcTables[i][y * (vt.cfg.virtTexDimensionPages >> i) + x]) = (0xFF << 24) + ((i + ((x & 0xFF00) >> 4) + ((y & 0xFF00) >> 2)) << 16) + (((uint8_t) y) << 8) + ((uint8_t) x); // format: ABGR
                    else
                        (mipcalcTables[i][y * (vt.cfg.virtTexDimensionPages >> i) + x]) = (0xFF << 24) + (i << 16) + (y << 8) + x; // format: ABGR
                }
            }

            glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA, vt.cfg.virtTexDimensionPages >> i, vt.cfg.virtTexDimensionPages >> i, 0, GL_RGBA, GL_UNSIGNED_BYTE, mipcalcTables[i]);
            free(mipcalcTables[i]);
        }
        free(mipcalcTables);
    }

    if (READBACK_MODE_FBO || READBACK_MODE == kBackbufferGetTexImage)
    {
        glGenTextures(1, &vt.fboColorTexture);

        if (READBACK_MODE_FBO)
        {
            glGenTextures(1, &vt.fboDepthTexture);

            glGenFramebuffersEXT(1, &vt.fbo);
        }
    }

    glActiveTexture(GL_TEXTURE0);

#if !GL_ES_VERSION_2_0
    if (USE_PBO_PAGETABLE)
    {
        glGenBuffers(1, &vt.pboPagetable);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, vt.pboPagetable);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, (vt.pageTableMipOffsets[vt.cfg.mipChainLength - 1] + 1) * 4, 0, GL_STREAM_DRAW);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    if (USE_PBO_PHYSTEX)
    {
        glGenBuffers(1, &vt.pboPhystex);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, vt.pboPhystex);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, vt.cfg.pageMemsize * PBO_PHYSTEX_PAGES, 0, GL_STREAM_DRAW);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    if (USE_PBO_READBACK)
    {
        glGenBuffers(1, &vt.pboReadback);
    }
#endif
}

void vtShutdown()
{
#if ENABLE_MT
    // Signal threads to shut down
    vt.shutdownThreads = true;
    vt.neededPagesAvailableCondition.notify_all();
    #if ENABLE_MT > 1
        vt.compressedPagesAvailableCondition.notify_all();
    #endif

    // Wait for threads to finish
    if (vt.loaderThread.joinable()) {
        vt.loaderThread.join();
    }
    #if ENABLE_MT > 1
        if (vt.decompressorThread.joinable()) {
            vt.decompressorThread.join();
        }
    #endif
#endif

    free(vt.pageTables[0]);
    free(vt.pageTables);

    if (!USE_PBO_READBACK)
        free(vt.readbackBuffer);

    glDeleteTextures(1, &vt.physicalTexture);
    glDeleteTextures(1, &vt.pageTableTexture);
    if (USE_MIPCALC_TEXTURE)
        glDeleteTextures(1, &vt.mipcalcTexture);

    if (USE_PBO_READBACK)
        glDeleteBuffers(1, &vt.pboReadback);
    if (USE_PBO_PAGETABLE)
        glDeleteBuffers(1, &vt.pboPagetable);
    if (USE_PBO_PHYSTEX)
        glDeleteBuffers(1, &vt.pboPhystex);

    if (READBACK_MODE_FBO || READBACK_MODE == kBackbufferGetTexImage)
    {
        glDeleteTextures(1, &vt.fboDepthTexture);

        if (READBACK_MODE_FBO)
        {
            glDeleteTextures(1, &vt.fboColorTexture);
            glDeleteFramebuffersEXT(1, &vt.fbo);
        }
    }
}

void updatePerspectiveMatrix(double m[4][4], double fovy, double aspect, double zNear, double zFar)
{
    double sine, cotangent, deltaZ;
    double radians = fovy / 2.0 * 3.14159265358979323846 / 180.0;

    deltaZ = zFar - zNear;
    sine = sin(radians);

    if ((deltaZ == 0) || (sine == 0) || (aspect == 0))
        vt_fatal("Error: perspectve matrix is degenerate");

    cotangent = cos(radians) / sine;

    m[0][0] = cotangent / aspect;
    m[1][1] = cotangent;
    m[2][2] = -(zFar + zNear) / deltaZ;
    m[2][3] = -1.0;
    m[3][2] = -2.0 * zNear * zFar / deltaZ;
}

void vtReshape(const uint16_t _w, const uint16_t _h, const float fovInDegrees, const float nearPlane, const float farPlane)
{
    vt.real_w = _w;
    vt.real_h = _h;
    vt.w = _w >> PREPASS_RESOLUTION_REDUCTION_SHIFT;
    vt.h = _h >> PREPASS_RESOLUTION_REDUCTION_SHIFT;
    vt.fovInDegrees = fovInDegrees;

#if !GL_ES_VERSION_2_0
    if (READBACK_MODE_FBO || READBACK_MODE == kBackbufferGetTexImage)
    {
        glEnable(GL_TEXTURE_RECTANGLE_ARB);

        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, vt.fboColorTexture);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, vt.w, vt.h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

        if (READBACK_MODE_FBO)
        {
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB, vt.fboDepthTexture);    // TODO: use renderbuffer instead of texture?
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE );
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_COMPARE_MODE, GL_NONE );
            glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_DEPTH_COMPONENT24, vt.w, vt.h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL);

            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, vt.fbo);

            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_RECTANGLE_ARB, vt.fboColorTexture, 0);
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_RECTANGLE_ARB, vt.fboDepthTexture, 0);

            if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
                vt_fatal("Error: couldn't setup FBO %04x\n", (unsigned int)glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT));

            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        }

        glDisable(GL_TEXTURE_RECTANGLE_ARB);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
    }

    if (USE_PBO_READBACK)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, vt.pboReadback);
        glBufferData(GL_PIXEL_PACK_BUFFER, vt.w * vt.h * 4, 0, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
#endif

    if (!USE_PBO_READBACK && !READBACK_MODE_NONE)
    {
        if (vt.readbackBuffer)
            free(vt.readbackBuffer);
        vt.readbackBuffer = (uint32_t *) malloc(vt.w * vt.h * 4);
        assert(vt.readbackBuffer);
    }

    if (PREPASS_RESOLUTION_REDUCTION_SHIFT && fovInDegrees > 0.0)
        updatePerspectiveMatrix(vt.projectionMatrix, fovInDegrees, (float)vt.w / (float)vt.h, nearPlane, farPlane);
}

float vtGetBias()
{
    if (MIPPED_PHYSTEX)
        return vt.bias - 0.5f;
    else
        return vt.bias;
}
