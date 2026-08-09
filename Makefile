SUBDIRS = libvt sample

# emsdk location used to auto-activate emcc for the web build when it isn't
# already on PATH. Override with: make EMSDK=/path/to/emsdk
EMSDK ?= $(HOME)/Github/emsdk

.PHONY: all web clean $(SUBDIRS)

all: $(SUBDIRS) web

$(SUBDIRS):
	$(MAKE) -C $@

web:
	@if command -v emcc >/dev/null 2>&1; then \
		$(MAKE) -f Makefile.emscripten; \
	elif [ -f "$(EMSDK)/emsdk_env.sh" ]; then \
		echo "web: activating emsdk from $(EMSDK)"; \
		. "$(EMSDK)/emsdk_env.sh" >/dev/null 2>&1 && $(MAKE) -f Makefile.emscripten; \
	else \
		echo "web: SKIPPED - emcc not on PATH and no emsdk at $(EMSDK)"; \
		echo "web: install emsdk (https://emscripten.org) and 'source emsdk_env.sh', or pass EMSDK=/path/to/emsdk"; \
	fi

clean:
	for dir in $(SUBDIRS); do $(MAKE) -C $$dir clean; done
	$(MAKE) -f Makefile.emscripten clean
