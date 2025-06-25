# Build all source files in current dir
SRC_DIR = .
SRC = $(wildcard $(SRC_DIR)/*.cpp)
HDRS = $(wildcard $(SRC_DIR)/*.h)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BIN_DIR)/%.o,$(SRC))

# Set bin and package paths based on OS and hardware
OS := $(shell uname -s)
HW := $(shell uname -m)

# Mac
ifeq ($(OS),Darwin)
	OS = mac
    ifeq ($(HW),x86_64)
        PKG_PATH = /usr/local
    else ifeq ($(HW),arm64)
        PKG_PATH = /opt/homebrew
    endif
	OS_DEFS =
	GLES_LIBS_PATH = ../deps/lib-$(OS)
	LD_FLAGS = -Wl,-rpath,@executable_path/../../$(GLES_LIBS_PATH)
	GLES_LIB_FILES = libGLESv2.dylib libEGL.dylib
define GLES_INSTALL
	for lib in $(GLES_LIB_FILES); \
		do install_name_tool -change ./$$lib @rpath/$$lib $@; \
	done;
endef

# Linux
else ifeq ($(OS),Linux)
	OS = linux
	PKG_PATH = /usr
	OS_DEFS = -Dlinux
	GLES_LIBS_PATH = ../deps/lib-$(OS)
	LD_FLAGS =
	GLES_LIB_FILES = libGLESv2.so libEGL.so
define GLES_INSTALL
	for lib in $(GLES_LIB_FILES); \
		do cp $(GLES_LIBS_PATH)/$$lib $(BIN_DIR)/$$lib; \
	done;
endef

# Windows
else
	OS = win
	HW = x86_64
    PKG_PATH = /c/msys64/clang64
	OS_DEFS = -DWIN32
	GLES_LIBS_PATH = ../deps/lib-$(OS)
	LD_FLAGS = -Wl,/subsystem:console
	GLES_LIB_FILES = libGLESv2.dll libEGL.dll
define GLES_INSTALL
	for lib in $(GLES_LIB_FILES); \
		do cp $(GLES_LIBS_PATH)/$$lib $(BIN_DIR)/$$lib; \
	done;
endef
endif

BIN_DIR = ./bin/$(OS)-$(HW)

# Include paths
PKG_INC = -I$(PKG_PATH)/include
SDL_INC = `sdl2-config --cflags`
GLES_INC = -I../deps/include/OpenGLES
VT_INC = -I../libvt -I../deps/include -I../deps/source 

# Libraries
PKG_LIB_PATH = -L$(PKG_PATH)/lib
SDL_LIBS = `sdl2-config --libs` -l SDL2_image
GLES_LIBS = -l GLESv2 -l EGL 
VT_LIB = ../libvt/$(BIN_DIR)/libvt.a

# Compiler flags
CLANG = clang++ 
WARN = -Wall
DEFS = 
OPT_DEBUG = -O0 -g
OPT_NDEBUG = -DNDEBUG -O3
OPT = $(OPT_DEBUG)
CFLAGS = -std=c++11 -pthread $(WARN) $(DEFS) $(OPT)
LD_FLAGS += -pthread

# Target executable
ifeq ($(OS),mac)
    SAMPLE=$(BIN_DIR)/hello-vt
else ifeq ($(OS),linux)
    SAMPLE=$(BIN_DIR)/hello-vt
else
    SAMPLE=$(BIN_DIR)/hello-vt.exe
endif

# Build targets
.PHONY: all clean

all: $(SAMPLE)

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/%.o: $(SRC_DIR)/%.cpp $(HDRS) | $(BIN_DIR)
	$(CLANG) $(CFLAGS) $(PKG_INC) $(SDL_INC) $(GLES_INC) $(VT_INC) -c $< -o $@

$(SAMPLE): $(OBJS) $(VT_LIB) | $(BIN_DIR)
	$(CLANG) $(LD_FLAGS) $(OBJS) $(PKG_LIB_PATH) -L$(GLES_LIBS_PATH) $(SDL_LIBS) $(GLES_LIBS) $(VT_LIB) -o $@
	$(call GLES_INSTALL)
	@echo
	@echo BUILT: $@
	@echo
	
clean:
	rm -f $(OBJS) $(SAMPLE)
