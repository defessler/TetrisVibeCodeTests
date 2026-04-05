CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
TARGET   := tetris
SRCS     := main.cpp game.cpp ansi_renderer.cpp

# ── Default: ANSI terminal build ──────────────────────────────────────────────
$(TARGET): $(SRCS) game.h game_logic.h renderer.h ansi_renderer.h
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

# ── SDL2 build (example; requires libsdl2-dev) ────────────────────────────────
# Uncomment and implement sdl2_renderer.cpp / sdl2_renderer.h to use:
#
# tetris-sdl2: main_sdl2.cpp game.cpp sdl2_renderer.cpp
#	$(CXX) $(CXXFLAGS) -o $@ $^ $(shell sdl2-config --cflags --libs)

clean:
	rm -f $(TARGET)

.PHONY: clean
