# Build all source files in current dir
SRC_DIR = .
SRC = $(wildcard $(SRC_DIR)/*.cpp)
HDRS = $(wildcard $(SRC_DIR)/*.h)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(LIB_DIR)/%.o,$(SRC))

# Set bin and package paths based on os and hardware
OS := $(shell uname -s)
HW := $(shell uname -m)

ifeq ($(OS),Darwin)
	OS = mac
    ifeq ($(HW),x86_64)
        PKG_PATH = /usr/local
    else ifeq ($(HW),arm64)
        PKG_PATH = /opt/homebrew
    endif
	OS_DEFS =
else ifeq ($(OS),Linux)
	OS = linux
	HW = x86_64
	PKG_PATH = /usr
	OS_DEFS = -Dlinux -D_REENTRANT
else
	OS = win
	HW = x86_64
    PKG_PATH = /c/msys64/clang64
	OS_DEFS = -DWIN32
endif

LIB_DIR = ./bin/$(OS)-$(HW)

# Include paths
PKG_INC = -I$(PKG_PATH)/include
GLES_INC = -I../deps/include/OpenGLES
VT_INC = -I../deps/include -I../deps/source 

# Compiler flags
CLANG = clang++ 
WARN = -Wall
DEFS = $(OS_DEFS)
OPT_DEBUG = -O0 -g
OPT_NDEBUG = -DNDEBUG -O3
OPT = $(OPT_DEBUG)
CFLAGS = -std=c++11 -pthread $(WARN) $(DEFS) $(OPT)
AR = ar rvsc

# Target library
LIB=$(LIB_DIR)/libvt.a

# Build targets
.PHONY: all clean

all: $(LIB)

$(LIB_DIR):
	mkdir -p $@

$(LIB_DIR)/%.o: $(SRC_DIR)/%.cpp $(HDRS) | $(LIB_DIR)
	$(CLANG) $(CFLAGS) $(PKG_INC) $(GLES_INC) $(VT_INC) -c $< -o $@ 

$(LIB): $(OBJS) | $(LIB_DIR)
	$(AR) $@ $(OBJS)
	@echo
	@echo BUILT: $@
	@echo

clean:
	rm -f $(OBJS) $(LIB)
