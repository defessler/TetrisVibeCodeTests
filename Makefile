CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
TARGET   := tetris
SRCS     := main.cpp game.cpp ansi_renderer.cpp

PIXEL_TARGET := tetris-pixel
PIXEL_SRCS   := main.cpp game.cpp pixel_renderer.cpp

VULKAN_TARGET   := tetris-vulkan
VULKAN_SRCS     := main.cpp game.cpp vulkan_renderer.cpp
VULKAN_LIBS     := -lvulkan -lglfw -lpthread -ldl
GLSL            := glslangValidator
SHADERS_DIR     := shaders
SPIRV_FILES     := $(SHADERS_DIR)/cell.vert.spv $(SHADERS_DIR)/cell.frag.spv
SPIRV_HDR       := $(SHADERS_DIR)/spirv_embed.h

WIN_CXX         := x86_64-w64-mingw32-g++
WIN_CXXFLAGS    := -std=c++20 -O3 -DNDEBUG -s -static
WIN_TARGET      := tetris.exe
WIN_SRCS        := main.cpp game.cpp windows_renderer.cpp
WIN_PIXEL_TARGET := tetris-pixel.exe
WIN_PIXEL_SRCS  := main.cpp game.cpp windows_pixel_renderer.cpp

# ── Default: Linux ANSI terminal build ───────────────────────────────────────
$(TARGET): $(SRCS) game.h game_logic.h renderer.h ansi_renderer.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

# ── Pixel-art renderer build ─────────────────────────────────────────────────
pixel: $(PIXEL_SRCS) game.h game_logic.h renderer.h pixel_renderer.h
	$(CXX) $(CXXFLAGS) -DPIXEL_ART -o $(PIXEL_TARGET) $(PIXEL_SRCS)

# ── Windows cross-compile (requires mingw-w64) ────────────────────────────────
windows: $(WIN_TARGET)
$(WIN_TARGET): $(WIN_SRCS) game.h game_logic.h renderer.h windows_renderer.h
	$(WIN_CXX) $(WIN_CXXFLAGS) -o $@ $(WIN_SRCS)

# ── Windows pixel-art build ───────────────────────────────────────────────────
windows-pixel: $(WIN_PIXEL_TARGET)
$(WIN_PIXEL_TARGET): $(WIN_PIXEL_SRCS) game.h game_logic.h renderer.h windows_pixel_renderer.h
	$(WIN_CXX) $(WIN_CXXFLAGS) -DPIXEL_ART -o $@ $(WIN_PIXEL_SRCS)

# ── Vulkan sprite renderer build (Linux; requires libvulkan-dev, glfw3, glslang-tools) ──
$(SHADERS_DIR)/%.vert.spv: $(SHADERS_DIR)/%.vert
	$(GLSL) -V $< -o $@

$(SHADERS_DIR)/%.frag.spv: $(SHADERS_DIR)/%.frag
	$(GLSL) -V $< -o $@

$(SPIRV_HDR): $(SPIRV_FILES) embed-spv.sh
	./embed-spv.sh $@ CELL_VERT_SPV $(SHADERS_DIR)/cell.vert.spv \
	                   CELL_FRAG_SPV $(SHADERS_DIR)/cell.frag.spv

vulkan: $(VULKAN_TARGET)
$(VULKAN_TARGET): $(VULKAN_SRCS) game.h game_logic.h renderer.h vulkan_renderer.h $(SPIRV_HDR)
	$(CXX) $(CXXFLAGS) -DVULKAN_ART -o $@ $(VULKAN_SRCS) $(VULKAN_LIBS)

# ── Tests (Linux only) ────────────────────────────────────────────────────────
tests: tests.cpp game.cpp test_runner.h game.h game_logic.h
	$(CXX) $(CXXFLAGS) -o $@ tests.cpp game.cpp

test: tests
	./tests

clean:
	rm -f $(TARGET) $(PIXEL_TARGET) $(VULKAN_TARGET) $(WIN_TARGET) $(WIN_PIXEL_TARGET) tests
	rm -f $(SPIRV_FILES) $(SPIRV_HDR)

.PHONY: pixel vulkan windows windows-pixel test clean
