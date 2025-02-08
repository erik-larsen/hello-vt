/*
 *  LibVT_Shaders.h
 */

// Comment header common to all VT shaders
static const char* vtShaderHeader = R"(   
//
//  LibVT Virtual Texturing Shaders
//  Based on Sean Barrett's public domain "Sparse Virtual Textures" demo shaders
//  Copyright (c) 2010 A. Julian Mayer
//
)";

// Prelude common to all VT shaders
static const char* vtShaderPreludeTemplate = R"(   
    precision mediump float;

    const float phys_tex_dimension_pages = %f;
    const float page_dimension = %f;
    const float page_dimension_log2 = %f;
    const float prepass_resolution_reduction_shift = %f;
    const float virt_tex_dimension_pages = %f;
    const float virt_tex_dimension = %f;
    const float border_width = %f;
    const float max_anisotropy = %f;

    #define USE_MIPCALC_TEXTURE %i
    #define PAGE_BORDER %i
    #define MIPPED_PHYSTEX %i
    #define FALLBACK_ENTRIES %i
    #define ANISOTROPY %i
    #define LONG_MIP_CHAIN %i
    #define TEXUNIT_MIPCALC TEXUNIT%i
    #define TEXUNIT_PHYSICAL TEXUNIT%i
    #define TEXUNIT_PAGETABLE TEXUNIT%i

)";

static const char* vtReadbackVertGLSL = R"( 
    //
    // vtReadbackVertGLSL
    //    
    uniform mat4 matViewProjection;
    attribute vec4 vertex;
    attribute vec2 texcoord0;
    varying vec2 texcoord;
    void main(void)
    {
        gl_Position = matViewProjection * vertex;
        texcoord    = texcoord0.xy;
    }
)";

static const char* vtReadbackFragGLSL = R"(
    //
    // vtReadbackFragGLSL
    //
    uniform sampler2D mipcalcTexture;
    uniform float mip_bias;
    varying vec2 texcoord;

    void main(void)
    {
        gl_FragColor = texture2D(mipcalcTexture, texcoord.xy, page_dimension_log2 - prepass_resolution_reduction_shift + mip_bias);
    }
)";

static const char* vtRenderVertGLSL = R"(
    //
    // vtRenderVertGLSL
    //
    uniform mat4 matViewProjection;
    attribute vec4 vertex;
    attribute vec2 texcoord0;
    varying vec2 texcoord;

    void main( void )
    {
        gl_Position = matViewProjection * vertex;
        texcoord    = texcoord0.xy;
    } 
)";

static const char* vtRenderFragGLSL = R"(
    //
    // vtRenderFragGLSL
    //
    uniform sampler2D pageTableTexture;
    uniform sampler2D physicalTexture;
    uniform float mip_bias;
    varying vec2 texcoord;

    // FALLBACK_ENTRIES
    vec2 calculateCoordinatesFromSample(vec4 pageTableEntry)
    {
        #if LONG_MIP_CHAIN
            float mipExp = exp2(pageTableEntry.a);
        #else
            float mipExp = pageTableEntry.a + 1.0;
        #endif

        vec2 pageCoord = pageTableEntry.bg;
        vec2 withinPageCoord = fract(texcoord.xy * mipExp);

        #if PAGE_BORDER
            withinPageCoord = withinPageCoord * (page_dimension - border_width * 2.0)/page_dimension + border_width/page_dimension;
        #endif

        vec2 result = ((pageCoord + withinPageCoord) / phys_tex_dimension_pages);
        return result;
    }

    vec2 calculateVirtualTextureCoordinates()
    {
        vec4 pageTableEntry = texture2D(pageTableTexture, texcoord.xy, page_dimension_log2 + mip_bias) * 255.0; // samplePagetableOnce
        return calculateCoordinatesFromSample(pageTableEntry);
    }

    void main(void)
    {
        vec2 coord = calculateVirtualTextureCoordinates();
        vec4 vtex = texture2D(physicalTexture, coord);
        gl_FragColor = vtex;
    }
)";
