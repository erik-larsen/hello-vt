# Hello Virtual Texturing

![VT sample app](media/hello-vt.png)

A minimal yet complete implementation of virtual texturing that is cross-platform and provides a reference sample app.

This work is an OpenGLES-specific and modernized fork of [LibVT](https://github.com/core-code/LibVT?tab=readme-ov-file#readme) plus a cross-platform sample app.  Special thanks to *Julian Mayer* for his original implementation and thorough documentation.

Goals:

- Provide a minimal example of virtual texturing to help in learning the algorithm and to serve as a building block for other projects.

- Enable cross platform native builds on Mac, Windows, and Linux, and a future WebGL build via Emscripten.  To this end, C++ and OpenGLES were chosen from the start.

- Supply a reference sample that demonstrates the LibVT calls necessary to load and render a virtual texture, while utilizing SDL for cross-platform windowing and event handling.

NOTE: This code is work in progress.  More code minimization needs to be done and an Emscripten build is pending.  But at least an interactive demo is now available on Mac, Windows, and Linux.


## Sample

The sample shows rendering of a test virtual texture to a simple quad.  A traditional static texture is also drawn underneath to show how to mix regular rendering with LibVT rendering.

Controls:
```
Zoom:     Mouse wheel
Pan:      Arrow keys
Orbit:    Left mouse button drag
Reset:    'R' key
Quit:     'ESC' key
```

## Building and Running

Install dependencies for your platform.

### Mac:

Install [Homebrew](https://brew.sh/) if you don't have it, then SDL2 and SDL2_image:
```bash
brew install SDL2
brew install SDL2_image
```

Build and run:
```bash
cd hello-vt
make
cd Sample
./bin-mac-x86_64/vtdemo
```
or
```bash
./bin-mac-arm64/vtdemo
```
depending on your Mac.

### Windows:

Setup clang compilation on Windows.  First, install MSYS from cmd.exe:
```bash
winget install MSYS2.MSYS2
setx PATH "%PATH%C:\msys64\clang64\bin"
```
Then run from MSYS2 CLANG64 shell:
```bash
pacman -Syu
pacman -S base-devel mingw-w64-clang-x86_64-toolchain
pacman -S mingw-w64-clang-x86_64-SDL2
pacman -S mingw-w64-clang-x86_64-SDL2_image
```

Then to build and run:
```bash
cd hello-vt
make
cd Sample
./bin-win-x86_64/vtdemo.exe
```

### Linux:

This was tested on Debian 11.3 and Ubuntu 24.04.  Install required build tools and libraries:

```bash
sudo apt update
sudo apt install build-essential clang
sudo apt install libsdl2-dev libsdl2-image-dev
sudo apt install chromium
cp /usr/lib/chromium/*.so hello-vt/deps/lib-linux
```

Then to build and run:
```bash
cd hello-vt
make
cd Sample
./bin-linux-x86_64/vtdemo
```


## Implementation

Many virtual texturing implementations have been available in C, C++ and JS, using OpenGL, Direct3D, and WebGL.  However, for the purposes of this project they are either not well-documented, not minimal, not cross-platform, or not straightforward to build due to age (most date from 2010 or earlier).  

Of note, [OpenSeaDragon](https://openseadragon.github.io/) is an excellent implementation of virtual texturing but is not a good fit for this project because it is not C++ and not minimal.

Instead, LibVT was chosen for its C++ implementation, OpenGLES code path, and decent documentation.  Fixes were made to LibVT to get it running again 15 years later, and to remove code not on the OpenGLES 2 code path.  Further, an SDL-based sample app with a pre-processed test image is provided to demonstrate LibVT. (Note: At one time LibVT provided its own [pre-built sample](https://bintray.com/artifact/download/corecode/LibVT/libvt_demo_binaries_win32_mac.zip), but the link is dead and not saved on archive.org).


## Work in progress

### libvt-webgl1

* Code minimized to OpenGLES2 / WebGL1
* Sync framebuffer readback
* Emscripten build
* Decompress png/jpg files only, using stb
* No GPU texture compression
* Visualize virtual and physical textures for debugging
* Auto configure LibVT based on input image


## Future work

### libvt-webgl2

* Add OpenGLES3 / WebGL2 code path
* Async + double-buffered PBO readback
* Emscripten build
* Faster decompression using libjpeg-turbo and libpng
* ETC2/EAC GPU texture compression


## Authors

- Erik Larsen (LibVT fork and sample app)
- Julian Mayer (original LibVT author)
