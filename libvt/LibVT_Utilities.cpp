#include "LibVT_Internal.h"
#include "LibVT.h"

extern vtConfig c;

uint32_t * vtuDownsampleImageRGB(const uint32_t *_tex)
{
    uint8_t *tex = (uint8_t *) _tex;
    uint8_t *smallTex = (uint8_t *)malloc((c.pageDimension * c.pageDimension * 3) / 4);
    assert(smallTex);

    for (uint16_t x = 0; x < c.pageDimension / 2; x++)
    {
        for (uint16_t y = 0; y < c.pageDimension / 2; y++)
        {
#ifdef COLOR_CODE_MIPPED_PHYSTEX
            smallTex[y * (c.pageDimension / 2) * 3 + (x*3)] = 200;
            smallTex[y * (c.pageDimension / 2) * 3 + (x*3) + 1] = 10;
            smallTex[y * (c.pageDimension / 2) * 3 + (x*3) + 2] = 70;
#else
            uint8_t pix1 = tex[(y*2) * c.pageDimension * 3 + (x*2*3)];
            uint8_t pix2 = tex[(y*2+1) * c.pageDimension * 3 + (x*2*3)];
            uint8_t pix3 = tex[(y*2) * c.pageDimension * 3 + (x*2*3+3)];
            uint8_t pix4 = tex[(y*2+1) * c.pageDimension * 3 + (x*2*3+3)];

            smallTex[y * (c.pageDimension / 2) * 3 + (x*3)] = (pix1 + pix2 + pix3 + pix4) / 4;

            pix1 = tex[(y*2) * c.pageDimension * 3 + (x*2*3) + 1];
            pix2 = tex[(y*2+1) * c.pageDimension * 3 + (x*2*3) + 1];
            pix3 = tex[(y*2) * c.pageDimension * 3 + (x*2*3+3) + 1];
            pix4 = tex[(y*2+1) * c.pageDimension * 3 + (x*2*3+3) + 1];

            smallTex[y * (c.pageDimension / 2) * 3 + (x*3) + 1] = (pix1 + pix2 + pix3 + pix4) / 4;

            pix1 = tex[(y*2) * c.pageDimension * 3 + (x*2*3) + 2];
            pix2 = tex[(y*2+1) * c.pageDimension * 3 + (x*2*3) + 2];
            pix3 = tex[(y*2) * c.pageDimension * 3 + (x*2*3+3) + 2];
            pix4 = tex[(y*2+1) * c.pageDimension * 3 + (x*2*3+3) + 2];

            smallTex[y * (c.pageDimension / 2) * 3 + (x*3) + 2] = (pix1 + pix2 + pix3 + pix4) / 4;
#endif
        }
    }

    return (uint32_t *)smallTex;
}

void vtuPerspective(double m[4][4], double fovy, double aspect,    double zNear, double zFar)
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

char vtuFileExists(char *path)
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

void * vtuLoadFile(const char *filePath, const uint32_t offset, uint32_t *file_size)
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

    size_t result;

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

    result = fread(fileData, 1, *fsp, f);
    assert(result == *fsp);

    fclose (f);

    return fileData;
}

GLuint vtuCompileShaderWithPrelude(const char* prelude, const char* shaderSrc, GLenum type) 
{
    std::string fullShader;

    // header for all VT shaders
    fullShader.append(vtShaderHeader);

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

GLuint vtuLoadShadersWithPrelude(const char* prelude, const char* vertSrc, const char* fragSrc)
{
    GLuint vertexShader = vtuCompileShaderWithPrelude(prelude, vertSrc, GL_VERTEX_SHADER);
    GLuint fragmentShader = vtuCompileShaderWithPrelude(prelude, fragSrc, GL_FRAGMENT_SHADER);
    
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