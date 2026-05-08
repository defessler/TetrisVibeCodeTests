# Tetris

A Tetris clone written in C++20 with no external dependencies. Runs in any
ANSI-capable terminal on Linux and in the Windows Console on Windows 10+.
Also includes a true-colour pixel-art terminal renderer and a Vulkan GPU renderer.

## Features

- All 7 tetrominoes with SRS wall kicks and 7-bag randomiser
- Ghost piece, soft/hard drop, level progression
- Double-buffered renderer — only changed cells are redrawn each frame
- Pluggable `Renderer` interface — swap backends without touching game logic
- Three renderer backends: ANSI terminal, true-colour pixel-art, Vulkan GPU

## Quick start

### Download a pre-built binary

| Platform | File |
|----------|------|
| Linux x86-64 (ANSI) | [`dist/tetris-linux-x86_64`](dist/tetris-linux-x86_64) |
| Linux x86-64 (pixel-art) | [`dist/tetris-pixel-linux-x86_64`](dist/tetris-pixel-linux-x86_64) |
| Windows x86-64 (ANSI) | [`dist/tetris-windows-x86_64.exe`](dist/tetris-windows-x86_64.exe) |
| Windows x86-64 (pixel-art) | [`dist/tetris-pixel-windows-x86_64.exe`](dist/tetris-pixel-windows-x86_64.exe) |

```bash
# Linux
chmod +x dist/tetris-linux-x86_64
./dist/tetris-linux-x86_64

# Windows — double-click or run from any terminal
dist\tetris-windows-x86_64.exe
```

### Build from source

**Requirements:** GCC or Clang with C++20 support, GNU Make.

```bash
git clone https://github.com/defessler/Tetris-
cd Tetris-
make          # Linux ANSI terminal build
make pixel    # Linux true-colour pixel-art build
make windows  # Windows ANSI (requires mingw-w64)
make windows-pixel  # Windows pixel-art
make vulkan   # Vulkan GPU renderer (requires libvulkan-dev, libglfw3-dev, glslang-tools)
```

## Controls

| Key | Action |
|-----|--------|
| `←` `→` / `A` `D` | Move left / right |
| `↑` / `W` | Rotate (SRS) |
| `↓` / `S` | Soft drop (+1 pt/cell) |
| `Space` | Hard drop (+2 pts/cell) |
| `Q` / `Escape` | Quit |

## Project structure

```
├── game.h                    Shared types: Board, GameState, Action
├── game.cpp                  Game logic: pieces, collision, SRS kicks,
│                             line clearing, scoring, 7-bag randomiser
├── game_logic.h              Declarations for game.cpp free functions
├── renderer.h                Abstract Renderer interface
├── ansi_renderer.h/.cpp      POSIX/ANSI terminal renderer (Linux/macOS)
├── pixel_renderer.h/.cpp     True-colour pixel-art terminal renderer (Linux)
├── windows_renderer.h/.cpp   Windows Console API renderer
├── windows_pixel_renderer.h/.cpp  Windows pixel-art renderer
├── vulkan_renderer.h/.cpp    Vulkan + GLFW GPU renderer
├── shaders/                  GLSL shaders for the Vulkan renderer
├── main.cpp                  Game loop — renderer selected at compile time
├── test_runner.h             Zero-dependency single-header test framework
├── tests.cpp                 37 unit tests for game logic
├── Makefile                  Build targets
└── dist/                     Pre-built binaries
```

## Running the tests

```bash
make test
```

The test suite covers `fits()`, `lockPiece()`, `clearLines()`, `ghostRow()`,
`lineScore()`, `tryRotate()`, and piece-table invariants — 37 tests, zero
external dependencies.
