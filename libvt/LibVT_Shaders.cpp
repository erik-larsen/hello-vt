#include "LibVT_Internal.h"
#include "LibVT.h"

// Prelude common to all VT shaders
static const char* vtShaderPreludeTemplate = R"(   
    //  LibVT Virtual Texturing Shaders
    //  Based on Sean Barrett's public domain "Sparse Virtual Textures" demo shaders
    
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

GLuint vtCompileShaderWithPrelude(const char* prelude, const char* shaderSrc, GLenum type) 
{
    std::string fullShader;

    // prelude for all VT shaders
    fullShader.append(prelude);

    // finally the actual shader source
    fullShader.append(shaderSrc);

    const char* fullShaderStr = fullShader.c_str();
    
    GLuint shader = glCreateShader(type);

    #if DEBUG_LOG > 2
        printf("Thread %llu: VT shader id: %d source:\n%s\n", THREAD_ID, shader, fullShaderStr);
    #endif

    glShaderSource(shader, 1, &fullShaderStr, nullptr);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        vt_fatal("Shader compilation failed:\n%s\n%s\n", fullShaderStr, infoLog);
        return 0;
    }

    return shader;
}

GLuint vtLoadShadersWithPrelude(const char* prelude, const char* vertSrc, const char* fragSrc)
{
    GLuint vertexShader = vtCompileShaderWithPrelude(prelude, vertSrc, GL_VERTEX_SHADER);
    GLuint fragmentShader = vtCompileShaderWithPrelude(prelude, fragSrc, GL_FRAGMENT_SHADER);
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        vt_fatal("Shader program linking failed: %s\n", infoLog);
        return 0;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return program;
}

char * vtGetShaderPrelude()
{
    setlocale( LC_ALL, "C" );

    char *buf = (char *) calloc(1, 2048);
    snprintf(buf, 2048, vtShaderPreludeTemplate,
        (float)vt.cfg.physTexDimensionPages, (float)vt.cfg.pageDimension, log2f(vt.cfg.pageDimension), (float)PREPASS_RESOLUTION_REDUCTION_SHIFT,
        (float)vt.cfg.virtTexDimensionPages, (float)(vt.cfg.virtTexDimensionPages * vt.cfg.pageDimension), (float)vt.cfg.pageBorder, float(ANISOTROPY),
        USE_MIPCALC_TEXTURE, vt.cfg.pageBorder, MIPPED_PHYSTEX, FALLBACK_ENTRIES, ANISOTROPY, LONG_MIP_CHAIN, TEXUNIT_FOR_MIPCALC, TEXUNIT_FOR_PHYSTEX, TEXUNIT_FOR_PAGETABLE);
    return buf;
}

void vtLoadShaders(GLuint* readbackShader, GLuint* renderVTShader)
{
    char* prelude = vtGetShaderPrelude();

      *readbackShader = vtLoadShadersWithPrelude(prelude, vtReadbackVertGLSL, vtReadbackFragGLSL);
      *renderVTShader = vtLoadShadersWithPrelude(prelude, vtRenderVertGLSL, vtRenderFragGLSL);
 
    free(prelude);
}