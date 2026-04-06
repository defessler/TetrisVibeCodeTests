CXX      := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
TARGET   := tetris
SRCS     := main.cpp game.cpp ansi_renderer.cpp

PIXEL_TARGET := tetris-pixel
PIXEL_SRCS   := main.cpp game.cpp pixel_renderer.cpp

WIN_CXX      := x86_64-w64-mingw32-g++
WIN_CXXFLAGS := -std=c++20 -O3 -DNDEBUG -s -static
WIN_TARGET   := tetris.exe
WIN_SRCS     := main.cpp game.cpp windows_renderer.cpp

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

# ── Tests (Linux only) ────────────────────────────────────────────────────────
tests: tests.cpp game.cpp test_runner.h game.h game_logic.h
	$(CXX) $(CXXFLAGS) -o $@ tests.cpp game.cpp

test: tests
	./tests

clean:
	rm -f $(TARGET) $(PIXEL_TARGET) $(WIN_TARGET) tests

.PHONY: pixel windows test clean
