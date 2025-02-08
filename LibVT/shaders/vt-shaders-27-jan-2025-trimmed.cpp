//
//  LibVT Virtual Texturing Shaders
//  Based on Sean Barrett's public domain "Sparse Virtual Textures" demo shaders
//  Copyright (c) 2010 A. Julian Mayer
//
   
    precision mediump float;

    const float phys_tex_dimension_pages = 32.000000;
    const float page_dimension = 256.000000;
    const float page_dimension_log2 = 8.000000;
    const float prepass_resolution_reduction_shift = 2.000000;
    const float virt_tex_dimension_pages = 32.000000;
    const float virt_tex_dimension = 8192.000000;
    const float border_width = 0.000000;
    const float max_anisotropy = 0.000000;

    #define USE_MIPCALC_TEXTURE 1
    #define PAGE_BORDER 0
    #define MIPPED_PHYSTEX 0
    #define FALLBACK_ENTRIES 1
    #define ANISOTROPY 0
    #define LONG_MIP_CHAIN 0
    #define TEXUNIT_MIPCALC TEXUNIT3
    #define TEXUNIT_PHYSICAL TEXUNIT2
    #define TEXUNIT_PAGETABLE TEXUNIT1

 
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

VT shader id: 2 source:
   
//
//  LibVT Virtual Texturing Shaders
//  Based on Sean Barrett's public domain "Sparse Virtual Textures" demo shaders
//  Copyright (c) 2010 A. Julian Mayer
//
   
    precision mediump float;

    const float phys_tex_dimension_pages = 32.000000;
    const float page_dimension = 256.000000;
    const float page_dimension_log2 = 8.000000;
    const float prepass_resolution_reduction_shift = 2.000000;
    const float virt_tex_dimension_pages = 32.000000;
    const float virt_tex_dimension = 8192.000000;
    const float border_width = 0.000000;
    const float max_anisotropy = 0.000000;

    #define USE_MIPCALC_TEXTURE 1
    #define PAGE_BORDER 0
    #define MIPPED_PHYSTEX 0
    #define FALLBACK_ENTRIES 1
    #define ANISOTROPY 0
    #define LONG_MIP_CHAIN 0
    #define TEXUNIT_MIPCALC TEXUNIT3
    #define TEXUNIT_PHYSICAL TEXUNIT2
    #define TEXUNIT_PAGETABLE TEXUNIT1


    //
    // vtReadbackFragGLSL
    //
    uniform sampler2D mipcalcTexture;
    uniform float mip_bias;
    varying vec2 texcoord;

    vec4 calculatePageRequest(vec2 uv)
    {
        vec4 result;
        result = texture2D(mipcalcTexture, texcoord.xy, page_dimension_log2 - prepass_resolution_reduction_shift + mip_bias);
        return result;
    }

    void main(void)
    {
        gl_FragColor = calculatePageRequest(texcoord.xy);
    }

VT shader id: 4 source:
   
//
//  LibVT Virtual Texturing Shaders
//  Based on Sean Barrett's public domain "Sparse Virtual Textures" demo shaders
//  Copyright (c) 2010 A. Julian Mayer
//
   
    precision mediump float;

    const float phys_tex_dimension_pages = 32.000000;
    const float page_dimension = 256.000000;
    const float page_dimension_log2 = 8.000000;
    const float prepass_resolution_reduction_shift = 2.000000;
    const float virt_tex_dimension_pages = 32.000000;
    const float virt_tex_dimension = 8192.000000;
    const float border_width = 0.000000;
    const float max_anisotropy = 0.000000;

    #define USE_MIPCALC_TEXTURE 1
    #define PAGE_BORDER 0
    #define MIPPED_PHYSTEX 0
    #define FALLBACK_ENTRIES 1
    #define ANISOTROPY 0
    #define LONG_MIP_CHAIN 0
    #define TEXUNIT_MIPCALC TEXUNIT3
    #define TEXUNIT_PHYSICAL TEXUNIT2
    #define TEXUNIT_PAGETABLE TEXUNIT1


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

VT shader id: 5 source:
   
//
//  LibVT Virtual Texturing Shaders
//  Based on Sean Barrett's public domain "Sparse Virtual Textures" demo shaders
//  Copyright (c) 2010 A. Julian Mayer
//
   
    precision mediump float;

    const float phys_tex_dimension_pages = 32.000000;
    const float page_dimension = 256.000000;
    const float page_dimension_log2 = 8.000000;
    const float prepass_resolution_reduction_shift = 2.000000;
    const float virt_tex_dimension_pages = 32.000000;
    const float virt_tex_dimension = 8192.000000;
    const float border_width = 0.000000;
    const float max_anisotropy = 0.000000;

    #define USE_MIPCALC_TEXTURE 1
    #define PAGE_BORDER 0
    #define MIPPED_PHYSTEX 0
    #define FALLBACK_ENTRIES 1
    #define ANISOTROPY 0
    #define LONG_MIP_CHAIN 0
    #define TEXUNIT_MIPCALC TEXUNIT3
    #define TEXUNIT_PHYSICAL TEXUNIT2
    #define TEXUNIT_PAGETABLE TEXUNIT1


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

        vec2 result;
        result = ((pageCoord + withinPageCoord) / phys_tex_dimension_pages);
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