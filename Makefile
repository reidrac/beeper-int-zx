# CONFIG
IMGUI_DIR = $(HOME)/src/imgui
IMGUI_FILE_DIALOG_DIR = $(HOME)/src/ImGuiFileDialog
# END OF CONFIG

TAG := $(shell git describe --abbrev=0 --tags ${TAG_COMMIT} 2>/dev/null || true)
COMMIT := $(shell git rev-parse --short HEAD)
DATE := $(shell git log -1 --format=%cd --date=format:"%Y%m%d")
VERSION := $(TAG)

ifeq ($(VERSION),)
    VERSION := dev-$(COMMIT) ($(DATE))
else
    VERSION += ($(DATE))
endif

CFLAGS = -O2 -s -Wall `sdl2-config --cflags`
CXXFLAGS = -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(IMGUI_FILE_DIALOG_DIR)
CXXFLAGS += -O2 -s -Wall `sdl2-config --cflags` -DAPP_VERSION="\"$(VERSION)\""
LIBS = -lGL -ldl `sdl2-config --libs`

# cross-build for windows
ifeq ($(CROSS_BUILD), Win) #LINUX
    CC = i686-w64-mingw32-gcc
    CXX = i686-w64-mingw32-g++
    CFLAGS += -D__USE_MINGW_ANSI_STDIO
    LIBS = -lgdi32 -lopengl32 -limm32 `sdl2-config --libs --static-libs` -static
endif

SOURCES = main.cpp sfx.c
SOURCES += zymosis.c
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_sdl.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
SOURCES += $(IMGUI_FILE_DIALOG_DIR)/ImGuiFileDialog.cpp

SOURCES += $(IMGUI_DIR)/examples/libs/gl3w/GL/gl3w.c
CXXFLAGS += -I$(IMGUI_DIR)/examples/libs/gl3w -DIMGUI_IMPL_OPENGL_LOADER_GL3W

BIN = sfxed
OBJS = $(addsuffix .o, $(basename $(notdir $(SOURCES))))

all: $(BIN)

%.o: %.c
	$(CC) $(CFLAGS) $< -c -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $< -c -o $@

%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_FILE_DIALOG_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/examples/libs/gl3w/GL/%.c
	$(CC) $(CXXFLAGS) -c -o $@ $<

%.o:$(IMGUI_DIR)/examples/libs/glad/src/%.c
	$(CC) $(CXXFLAGS) -c -o $@ $<

sfxed: $(OBJS)
	$(CXX) $(OBJS) $(CXXFLAGS) $(LIBS) -o $@

.PHONY: clean
clean:
	make -C player clean
	rm -f $(BIN) $(OBJS) Makefile.deps

Makefile.deps:
	$(CXX) $(CXXFLAGS) -MM $(SOURCES) > Makefile.deps

include Makefile.deps
