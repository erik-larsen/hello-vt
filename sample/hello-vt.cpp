/*

    hello-vt.cpp

    1. Use the python script "generateVirtualTexureTiles.py" to preprocess the full-resolution version of your texture atlas.
    This generates the virtual texture tile store.

    2. Adjust the values in "LibVT_Config.h" to match your generated virtual texture tile store, your application and its rendersettings.

    3. Now adjust your realtime application to use LibVT as documented (see renderVT() below for working example):
    - At startup call vtInit() with the path to your tile store, the border width, the mipchain length and the tilesize
    - Call vtGetShaderPrelude() to obtain the prelude to prepend to the shaders and load the readback and renderVT shaders.
    - When OpenGL is callable call vtPrepare() and pass it the shader objects.
    - Call vtReshape() now with the screen width, height, as well as fov, nearplane and farplane (only imporant in readback reduction mode).
        This call must also be made every time any of these values change, i.e. at viewport resize time.
    - Now in the renderloop:
        - call vtPrepareReadback()
        - render with the readback shader
        - call vtPerformReadback()
        - vtExtractNeededPages()
        - vtMapNewPages()
        - and then render with the renderVT shader.
        Additionally pass the result of vtGetMipBias() to both shaders as value for "mip_bias" each frame if you have the dynamic lod adjustment turned on.
    - At shutdown call vtShutdown()

*/

#include <initializer_list>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengles2.h>
#include "linmath.h"
#include "LibVT.h"

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    #include <emscripten/html5.h>
#endif

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    // Force ANGLE to use Metal backend on native macOS builds
    // Fixes black screen issue on older Intel Macs (e.g., Mac Pro 2013 with AMD FirePro running Sonoma)
    // where ANGLE defaults to OpenGL backend which doesn't render correctly
    #define FORCE_ANGLE_METAL_BACKEND_ON_MAC SDL_setenv("ANGLE_DEFAULT_PLATFORM", "metal", 1)
#else
    #define FORCE_ANGLE_METAL_BACKEND_ON_MAC
#endif

// Shared helper to setup vertex attributes for a quad (two triangles)
void setupQuadVertexAttributes(GLuint shader, const char* posAttribName, const char* texAttribName)
{
    GLint posAttrib = glGetAttribLocation(shader, posAttribName);
    GLint texAttrib = glGetAttribLocation(shader, texAttribName);

    glEnableVertexAttribArray(posAttrib);
    glEnableVertexAttribArray(texAttrib);

    // Stride is 5 floats (3 for pos, 2 for tex)
    const GLsizei stride = 5 * sizeof(float);
    // Position attribute starts at the beginning of the vertex data
    const void* posOffset = (void*)0;
    // Texture coordinate attribute starts after the 3 position floats
    const void* texOffset = (void*)(3 * sizeof(float));

    glVertexAttribPointer(posAttrib,            // attribute index
                          3,                    // size (x,y,z = 3)
                          GL_FLOAT,             // type
                          GL_FALSE,             // normalized?
                          stride,               // stride (skip 5 floats to get to next vertex)
                          posOffset);           // offset (position starts at beginning)
    glVertexAttribPointer(texAttrib,            // attribute index
                          2,                    // size (u,v = 2)
                          GL_FLOAT,             // type
                          GL_FALSE,             // normalized?
                          stride,               // stride (same as above)
                          texOffset);           // offset (skip 3 floats to get to texcoords)
}

//
// Virtual Texture (VT) rectangle rendering
//
GLuint vtReadbackShader = 0, vtRenderShader = 0;
GLuint vtVBO = 0, vtEBO = 0;

// Vertex data for a rectangle
const float vtVertices[] = {
    // position (x,y,z)    // texcoords (u,v)
    -0.5f, -0.5f, 0.0f,   0.0f, 1.0f,
     0.5f, -0.5f, 0.0f,   1.0f, 1.0f,
     0.5f,  0.5f, 0.0f,   1.0f, 0.0f,
    -0.5f,  0.5f, 0.0f,   0.0f, 0.0f
};

// Indices for two triangles forming a rectangle
const unsigned int vtIndices[] = {
    0, 1, 2,
    0, 2, 3
};

// Initialize geometry (two triangles forming a quad)
void initVTGeometry()
{
    glGenBuffers(1, &vtVBO);
    glGenBuffers(1, &vtEBO);

    glBindBuffer(GL_ARRAY_BUFFER, vtVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vtVertices), vtVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vtEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(vtIndices), vtIndices, GL_STATIC_DRAW);
}

void resizeViewportVT(int width, int height, float fov, float nearPlane, float farPlane)
{
    vtReshape(width, height, fov, nearPlane, farPlane);
}

void initVT()
{
    // Initialize LibVT
#ifdef __EMSCRIPTEN__
    // Tiles are fetched from the web server. The tile store lives two directories
    // above the page (page: <root>/bin/web/hello-vt.html, tiles: <root>/uv-test-8kx8k/),
    // so resolve an absolute URL relative to the page location. This works both for
    // a local dev server root and for subpath hosting like GitHub Pages project sites
    // (https://user.github.io/hello-vt/...).
    static char tileDir[1024];
    EM_ASM({
        stringToUTF8(new URL('../../uv-test-8kx8k', document.location.href).href, $0, $1);
    }, tileDir, sizeof(tileDir));
    printf("INFO: tile store URL: %s\n", tileDir);
    vtInit(tileDir, "png", 0, 6, 256);             // png tiles, no border, mipchain length 6, and 256x256 tiles
#else
    vtInit("./uv-test-8kx8k/", "png", 0, 6, 256);  // png tiles, no border, mipchain length 6, and 256x256 tiles
#endif

    // Load the VT shaders
    vtLoadShaders(&vtReadbackShader, &vtRenderShader);
    vtPrepare(vtReadbackShader, vtRenderShader);

    // Initialize geometry
    initVTGeometry();
}

void updateModelViewProjVT(mat4x4 modelViewMat, mat4x4 projMat)
{
    mat4x4 modelViewProjMat;
    mat4x4_identity(modelViewProjMat);
    mat4x4_mul(modelViewProjMat, projMat, modelViewMat);

    for (GLuint shader : {vtReadbackShader, vtRenderShader})
    {
        glUseProgram(shader);
        GLint modelviewLoc = glGetUniformLocation(shader, "matViewProjection");
        glUniformMatrix4fv(modelviewLoc, 1, GL_FALSE, (float*)modelViewProjMat);
    }
}

void renderVTGeometry(GLuint shader)
{
    glUseProgram(shader);

    glBindBuffer(GL_ARRAY_BUFFER, vtVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vtEBO);

    setupQuadVertexAttributes(shader, "vertex", "texcoord0");

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void renderVT()
{
    // Readback pass
    vtPrepareReadback();
    glUseProgram(vtReadbackShader);
    glUniform1f(glGetUniformLocation(vtReadbackShader, "mip_bias"), vtGetMipBias());
    renderVTGeometry(vtReadbackShader);
    glUseProgram(0);
    vtPerformReadback();

    // Update virtual texturing
    vtExtractNeededPages(NULL);
    vtMapNewPages();

    // Render pass
    glUseProgram(vtRenderShader);
    glUniform1f(glGetUniformLocation(vtRenderShader, "mip_bias"), vtGetMipBias());
    renderVTGeometry(vtRenderShader);
    glUseProgram(0);
}

void cleanupVT()
{
    vtShutdown();

    glDeleteBuffers(1, &vtVBO);
    glDeleteBuffers(1, &vtEBO);
    glDeleteProgram(vtReadbackShader);
    glDeleteProgram(vtRenderShader);
}

//
// TexRect rendering - basic, non-virtual textured rectangle to show mixing VT with other textures
//
GLuint texRectShader = 0;
GLuint texRectTexture = 0;
const char* texRectTexFilename = "texmap.png";
GLuint texRectTexUnit = GL_TEXTURE0;
GLuint texRectVBO = 0, texRectEBO = 0;

// Vertex data for a rectangle
const float texRectVertices[] = {
    // position (x,y,z)    // texcoords (u,v)
    -0.5f, -0.5f, -0.25f,   0.0f, 1.0f,
     0.5f, -0.5f, -0.25f,   1.0f, 1.0f,
     0.5f,  0.5f, -0.25f,   1.0f, 0.0f,
    -0.5f,  0.5f, -0.25f,   0.0f, 0.0f
};

// Indices for two triangles forming a rectangle
const unsigned int texRectIndices[] = {
    0, 1, 2,
    0, 2, 3
};

void checkShaderBuilt(const char* shader_name, GLenum status, GLuint shader)
{
    GLint success;
    glGetShaderiv(shader, status, &success);
    if (success)
        printf("INFO: %s id %d build OK\n", shader_name, shader);
    else
        printf("ERROR: %s id %d build FAILED!\n", shader_name, shader);
}

void initTexRectShader()
{
    // Texture shader
    const GLchar* vertex_source = R"(
        attribute vec4 position;
        attribute vec2 texcoord;
        uniform mat4 modelview;
        uniform mat4 projection;
        varying vec3 v_color;
        varying vec2 v_texcoord;
        void main()
        {
            gl_Position = projection * modelview * vec4(position.xyz, 1.0);
            v_color = position.xyz + vec3(0.5);
            v_texcoord = texcoord;
        }
    )";

    // Shader debugging options:
    //gl_FragColor = vec4 (v_color, 1.0); // rainbow
    //gl_FragColor = vec4 (1.0, 0.0, 0.0, 1.0); // red
    const GLchar* fragment_source = R"(
        precision mediump float;
        varying vec3 v_color;
        varying vec2 v_texcoord;
        uniform sampler2D texsampler;
        void main()
        {
            gl_FragColor = texture2D(texsampler, v_texcoord);
        }
    )";

    // Create and compile vertex shader
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_source, NULL);
    glCompileShader(vertex_shader);
    checkShaderBuilt("vertex_shader", GL_COMPILE_STATUS, vertex_shader);

    // Create and compile fragment shader
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_source, NULL);
    glCompileShader(fragment_shader);
    checkShaderBuilt("fragment_shader", GL_COMPILE_STATUS, fragment_shader);

    // Link vertex and fragment shader into shader program and start using it
    texRectShader = glCreateProgram();
    glAttachShader(texRectShader, vertex_shader);
    glAttachShader(texRectShader, fragment_shader);
    glLinkProgram(texRectShader);
    glUseProgram(texRectShader);

    // Bind the texture sampler to texture unit 0
    glUniform1i(glGetUniformLocation(texRectShader, "texsampler"), 0);
}

void updateModelViewProjTexRect(mat4x4 modelViewMat, mat4x4 projMat)
{
    glUseProgram(texRectShader);

    GLint modelviewLoc = glGetUniformLocation(texRectShader, "modelview");
    glUniformMatrix4fv(modelviewLoc, 1, GL_FALSE, (float*)modelViewMat);

    GLint projectionLoc = glGetUniformLocation(texRectShader, "projection");
    glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, (float*)projMat);
}

void initTexRectTexture()
{
    SDL_Surface *image = IMG_Load(texRectTexFilename);

    if (image)
    {
        int bitsPerPixel = image->format->BitsPerPixel;
        printf ("INFO: %s (%dx%d, %d bits) load OK\n",
            texRectTexFilename, image->w, image->h, bitsPerPixel);

        // Determine GL texture format
        GLint format = -1;
        if (bitsPerPixel == 24)
            format = GL_RGB;
        else if (bitsPerPixel == 32)
            format = GL_RGBA;

        if (format != -1)
        {
            // Generate a GL texture object
            glGenTextures(1, &texRectTexture);

            // Bind GL texture
            glActiveTexture(texRectTexUnit);
            glBindTexture(GL_TEXTURE_2D, texRectTexture);

            // Set the GL texture's wrapping and stretching properties
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            // Copy SDL surface image to GL texture
            glTexImage2D(GL_TEXTURE_2D, 0, format, image->w, image->h, 0,
                         format, GL_UNSIGNED_BYTE, image->pixels);
        }

        SDL_FreeSurface (image);
    }
    else
        printf("ERROR: Load %s failed, reason:%s\n", texRectTexFilename, IMG_GetError());

}

// Initialize TexRect geometry (two triangles forming a rectangle)
void initTexRectGeometry()
{
    glGenBuffers(1, &texRectVBO);
    glGenBuffers(1, &texRectEBO);

    glBindBuffer(GL_ARRAY_BUFFER, texRectVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(texRectVertices), texRectVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, texRectEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(texRectIndices), texRectIndices, GL_STATIC_DRAW);
}

// Init texRect shaders, texture, and geometry
void initTexRect()
{
    initTexRectShader();
    initTexRectTexture();
    initTexRectGeometry();
}

void renderTexRect()
{
    glActiveTexture(texRectTexUnit);
    glUseProgram(texRectShader);

    glBindBuffer(GL_ARRAY_BUFFER, texRectVBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, texRectEBO);

    setupQuadVertexAttributes(texRectShader, "position", "texcoord");

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glUseProgram(0);
}

void cleanupTexRect()
{
    glDeleteBuffers(1, &texRectVBO);
    glDeleteBuffers(1, &texRectEBO);
    glDeleteProgram(texRectShader);
    glDeleteTextures(1, &texRectTexture);
}

//
// GLES
//
void initGL()
{
    printf("INFO: GL vendor: %s\n", glGetString(GL_VENDOR));
    printf("INFO: GL renderer: %s\n", glGetString(GL_RENDERER));
    printf("INFO: GL version: %s\n", glGetString(GL_VERSION));
    //printf("INFO: GL extensions: %s\n", glGetString(GL_EXTENSIONS));

    glClearColor(0.0f, 0.0f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    initVT();
    initTexRect();
}

void renderFrameGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);  // Clear both color and depth
    renderVT();

    // All non-VT rendering must occur after renderVT()!
    renderTexRect();
}

void resizeViewportGL(int width, int height)
{
    printf("INFO: GL viewport resize = %dx%d\n", width, height);
    glViewport(0, 0, width, height);
}

void cleanupGL()
{
    cleanupVT();
    cleanupTexRect();
}


//
// Camera
//
mat4x4 camScaleMat;
mat4x4 camRotMat;
mat4x4 camTransMat;
mat4x4 camModelViewMat;
mat4x4 camProjMat;
// Smooth camera motion state, see updateCam()
float panVelX = 0.0f, panVelY = 0.0f;         // pan velocity from held arrow keys
float zoomLog = 0.0f, zoomLogTarget = 0.0f;   // log-space zoom, eased toward target
float orbVelX = 0.0f, orbVelY = 0.0f;         // orbit velocity (drag pixels/second)
float orbDragX = 0.0f, orbDragY = 0.0f;       // drag pixels accumulated this frame
bool  orbDragging = false;

void updateModelViewProjCam()
{
    mat4x4 viewMat;
    mat4x4_identity(viewMat);

    // Translate camera back a bit from the origin
    mat4x4_translate_in_place(viewMat, 0.0f, 0.0f, -4.0f);

    // Combine translation, rotation and scale
    mat4x4_mul(viewMat, viewMat, camRotMat);
    mat4x4_mul(viewMat, viewMat, camScaleMat);
    mat4x4_mul(viewMat, viewMat, camTransMat);
    mat4x4_dup(camModelViewMat, viewMat);

    // For debugging:
    // printf("modelview:\n");
    // mat4x4_print(camModelViewMat);
    // printf("projection:\n");
    // mat4x4_print(camProjMat);

    // Update shaders
    updateModelViewProjTexRect(camModelViewMat, camProjMat);
    updateModelViewProjVT(camModelViewMat, camProjMat);
}

void resetViewCam()
{
    mat4x4_identity(camScaleMat);
    mat4x4_identity(camRotMat);
    mat4x4_identity(camTransMat);
    panVelX = panVelY = 0.0f;
    zoomLog = zoomLogTarget = 0.0f;
    orbVelX = orbVelY = orbDragX = orbDragY = 0.0f;
    updateModelViewProjCam();
}

void initCam()
{
    mat4x4_identity(camProjMat);
    mat4x4_identity(camModelViewMat);
    mat4x4_identity(camScaleMat);
    mat4x4_identity(camRotMat);
    mat4x4_identity(camTransMat);
}

void resizeViewportCam(int width, int height)
{
    float aspect = (float)width / (float)height;
    printf("INFO: updated aspect to %f\n", aspect);

    const float fov = 45.0f, nearPlane = 0.01f, farPlane = 1000.0f;
    mat4x4_perspective(camProjMat,
        fov * M_PI / 180.0f,    // fovy in radians
        aspect,                 // aspect ratio
        nearPlane,              // near plane
        farPlane);              // far plane

    updateModelViewProjCam();

    resizeViewportGL(width, height);
    resizeViewportVT(width, height, fov, nearPlane, farPlane);
}

void zoomCam(float mouseWheelY)
{
    // Each wheel notch moves the zoom *target* multiplicatively; the actual
    // scale glides toward it in updateCam(), so wheel zoom is smooth and a
    // notch feels the same at any zoom level.
    const float zoomStep = 0.18f; // ln(scale) per wheel notch, ~1.2x
    zoomLogTarget = clamp(zoomLogTarget + mouseWheelY * zoomStep, logf(0.01f), logf(100.0f));
}

void rotateCam(float xrel, float yrel)
{
    const float sensitivity = 0.01f;

    vec2 dragVec = {
        xrel * sensitivity,
        -yrel * sensitivity
    };

    mat4x4 identityMat;
    mat4x4_identity(identityMat);

    mat4x4 deltaRot;
    mat4x4_arcball(deltaRot, identityMat, vec2{0, 0}, dragVec, 1.0f);
    mat4x4_mul(camRotMat, camRotMat, deltaRot);

    // printf("INFO: dragVec = %f, %f\n", dragVec[0], dragVec[1]);
}

// Smooth continuous camera motion, applied once per frame:
// - pan (held arrow keys): velocity eases toward the key direction — quick
//   ramp-up, soft stop — and is divided by zoom so on-screen speed is
//   constant at any zoom level.
// - zoom (,/. keys and wheel): both move a log-space *target* that the
//   actual scale glides toward, so key zoom ramps and stops softly and
//   each wheel notch lands smoothly instead of stepping.
// - orbit (left drag): 1:1 while dragging, with a velocity EMA tracking
//   the drag; releasing mid-swipe keeps orbiting and decays to a stop.
// Everything is dt-scaled so the feel is framerate-independent.
void updateCam()
{
    static Uint32 lastTicks = 0;
    Uint32 now = SDL_GetTicks();
    float dt = (lastTicks == 0) ? 0.0f : (now - lastTicks) / 1000.0f;
    lastTicks = now;
    if (dt <= 0.0f)
        return;
    if (dt > 0.05f)
        dt = 0.05f; // clamp hitches (tab switch, load stall) so the view can't jump

    const Uint8* keyStates = SDL_GetKeyboardState(NULL);
    float targetX = (float)(keyStates[SDL_SCANCODE_RIGHT]  - keyStates[SDL_SCANCODE_LEFT]);
    float targetY = (float)(keyStates[SDL_SCANCODE_UP]     - keyStates[SDL_SCANCODE_DOWN]);
    float targetZ = (float)(keyStates[SDL_SCANCODE_PERIOD] - keyStates[SDL_SCANCODE_COMMA]); // '.' in, ',' out

    // Pan velocity eases toward the held-key direction
    const float easeTime = 0.08f; // seconds to reach ~63% of the target speed
    float ease = 1.0f - expf(-dt / easeTime);
    panVelX += (targetX - panVelX) * ease;
    panVelY += (targetY - panVelY) * ease;
    bool panActive = !(targetX == 0.0f && targetY == 0.0f &&
                       fabsf(panVelX) < 0.001f && fabsf(panVelY) < 0.001f);
    if (!panActive)
        panVelX = panVelY = 0.0f;

    // Held zoom keys move the target continuously; wheel bumps it in zoomCam()
    const float zoomRate = 1.2f; // ln(scale) per second, ~3.3x per second held
    if (targetZ != 0.0f)
        zoomLogTarget = clamp(zoomLogTarget + targetZ * zoomRate * dt, logf(0.01f), logf(100.0f));

    // Actual zoom glides toward the target
    const float zoomEase = 1.0f - expf(-dt / 0.12f);
    float zoomDiff = zoomLogTarget - zoomLog;
    bool zoomActive = fabsf(zoomDiff) > 0.0005f;
    if (zoomActive)
        zoomLog += zoomDiff * zoomEase;
    else
        zoomLog = zoomLogTarget;

    // Orbit: apply drag 1:1 and track its velocity (50/50 EMA); after release
    // keep orbiting with that velocity, decaying to a soft stop
    bool orbActive = false;
    if (orbDragging)
    {
        orbVelX = orbVelX * 0.5f + (orbDragX / dt) * 0.5f;
        orbVelY = orbVelY * 0.5f + (orbDragY / dt) * 0.5f;
        if (orbDragX != 0.0f || orbDragY != 0.0f)
        {
            rotateCam(orbDragX, orbDragY);
            orbActive = true;
        }
        orbDragX = orbDragY = 0.0f;
    }
    else if (fabsf(orbVelX) > 2.0f || fabsf(orbVelY) > 2.0f)
    {
        rotateCam(orbVelX * dt, orbVelY * dt);
        float decay = expf(-dt / 0.25f);
        orbVelX *= decay;
        orbVelY *= decay;
        orbActive = true;
    }
    else
        orbVelX = orbVelY = 0.0f;

    if (!panActive && !zoomActive && !orbActive)
        return; // fully settled: skip the matrix churn

    float zoomScale = expf(zoomLog);
    mat4x4_identity(camScaleMat);
    mat4x4_scale_iso(camScaleMat, camScaleMat, zoomScale);

    const float panSpeed = 1.5f; // world units per second at zoom 1
    float panX = -panVelX * panSpeed * dt / zoomScale;
    float panY = -panVelY * panSpeed * dt / zoomScale;
    mat4x4_translate_in_place(camTransMat, panX, panY, 0.0f);

    updateModelViewProjCam();
}

//
// SDL with OpenGLES context
//
SDL_Window* sdlWindow = nullptr;
SDL_GLContext sdlGLContext;

void initSDL(int vpWidth, int vpHeight)
{
    // Init SDL
    FORCE_ANGLE_METAL_BACKEND_ON_MAC;
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    SDL_version version;
    SDL_GetVersion(&version);
    printf("INFO: SDL version: %d.%d.%d\n", version.major, version.minor, version.patch);

    // Init OpenGLES driver and context
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetSwapInterval(1); // 1 = sync framerate to refresh rate (no screen tearing)

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Explicitly set channel depths, otherwise we might get some < 8
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

#ifdef __EMSCRIPTEN__
    // Make sure the WebGL context is created without antialiasing: the prepass
    // encodes page IDs in pixel colors and MSAA resolve would corrupt them.
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
#endif

    sdlWindow = SDL_CreateWindow(
        "hello-vt",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        vpWidth, vpHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    sdlGLContext = SDL_GL_CreateContext(sdlWindow);
}

void resizeViewportSDL()
{
    // Handle high DPI: get the window's drawable size (actual pixels)
    int vpWidth, vpHeight;
    SDL_GL_GetDrawableSize(sdlWindow, &vpWidth, &vpHeight);
    resizeViewportCam(vpWidth, vpHeight);
}

bool processEventsSDL()
{
    bool running = true;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_KEYDOWN:
                {
                    const Uint8* keyStates = SDL_GetKeyboardState(NULL);

                    // Quit
                    if (keyStates[SDL_SCANCODE_ESCAPE])
                        running = false;

                    // Reset view
                    else if (keyStates[SDL_SCANCODE_R])
                        resetViewCam();

                    // Pan, zoom, and orbit are applied per frame in
                    // updateCam(), not from discrete input events.
                }
                break;

            case SDL_WINDOWEVENT:
                // SIZE_CHANGED covers all size changes, including SDL's Emscripten
                // backend syncing the canvas to CSS layout; RESIZED only fires for
                // external resizes and never comes from that sync.
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                    resizeViewportSDL();
                break;

            case SDL_MOUSEWHEEL:
                // Zoom in/out with mousewheel (moves the smooth-zoom target)
                zoomCam(event.wheel.preciseY);
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    orbDragging = true;
                    orbVelX = orbVelY = 0.0f; // grabbing stops any orbit glide
                    orbDragX = orbDragY = 0.0f;
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT)
                    orbDragging = false;
                break;

            case SDL_MOUSEMOTION:
                // Orbit drag pixels are accumulated here and applied (with
                // velocity tracking for the release glide) in updateCam()
                if (event.motion.state & SDL_BUTTON_LMASK)
                {
                    orbDragX += (float)event.motion.xrel;
                    orbDragY += (float)event.motion.yrel;
                }
                break;
        }
    }

    return running;
}

#ifdef __EMSCRIPTEN__
void mainLoopIterationEM()
{
    processEventsSDL(); // quitting is not applicable in the browser; ignore return value

    // The canvas is sized by CSS layout, which can settle after SDL window
    // creation — the drawable is 0x0 until the first SIZE_CHANGED arrives.
    int width, height;
    SDL_GL_GetDrawableSize(sdlWindow, &width, &height);
    if (width <= 0 || height <= 0)
    {
        // SDL only re-reads the canvas CSS size from a browser *window* resize
        // event, so if layout settled after SDL_CreateWindow nothing would ever
        // deliver the real size. Push it through SDL ourselves; SDL emits
        // SIZE_CHANGED and the normal resize path takes over.
        double cssW = 0, cssH = 0;
        emscripten_get_element_css_size("#canvas", &cssW, &cssH);
        if (cssW > 0 && cssH > 0)
            SDL_SetWindowSize(sdlWindow, (int)cssW, (int)cssH);
        return; // nothing sensible to render yet
    }

    updateCam();
    renderFrameGL();
    SDL_GL_SwapWindow(sdlWindow);
}

void mainLoopSDL()
{
    // The browser drives the frame loop via requestAnimationFrame (fps=0),
    // which also caps the framerate to the display refresh rate.
    emscripten_set_main_loop(mainLoopIterationEM, 0, 1);
}
#else
void mainLoopSDL()
{
    bool running = true;
    while (running) {
        Uint32 frameStart = SDL_GetTicks(); // Get the start time of the frame

        running = processEventsSDL();
        updateCam();
        renderFrameGL();
        SDL_GL_SwapWindow(sdlWindow);

        // Don't exceed maximum framerate of 60 fps
        const int maxFPS = 60;
        const int minFrameTime = 1000 / maxFPS; // Minimum frame duration in ms
        Uint32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < minFrameTime)
            SDL_Delay(minFrameTime - frameTime); // Make frame take minFrameTime
    }
}
#endif

void cleanupSDL()
{
    SDL_GL_DeleteContext(sdlGLContext);
    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
}

int main(int argc, char* argv[])
{
    // Init
    int vpWidth = 800, vpHeight = 600;
    initSDL(vpWidth, vpHeight);
    initGL();
    initCam();

    // Set initial viewport
    resizeViewportSDL();

    // Event handling & frame rendering loop
    mainLoopSDL();

    // Cleanup
    cleanupGL();
    cleanupSDL();

    return 0;
}
