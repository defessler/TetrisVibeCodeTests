# Tetris

A Tetris clone written in C++20 with no external dependencies. Runs in any
ANSI-capable terminal on Linux and in the Windows Console on Windows 10+.

## Features

- All 7 tetrominoes with SRS wall kicks and 7-bag randomiser
- Ghost piece, soft/hard drop, level progression
- Double-buffered renderer — only changed cells are redrawn each frame
- Synchronized output (`\033[?2026h/l`) for flicker-free display on
  supported terminals (kitty, iTerm2, GNOME Terminal, foot, WezTerm, …)
- Pluggable `Renderer` interface — swap backends without touching game logic

## Quick start

### Download a pre-built binary

| Platform | File |
|----------|------|
| Linux x86-64 | [`dist/tetris-linux-x86_64`](dist/tetris-linux-x86_64) |
| Windows x86-64 | [`dist/tetris-windows-x86_64.exe`](dist/tetris-windows-x86_64.exe) |

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
make          # builds ./tetris
./tetris
```

Cross-compile for Windows (requires `mingw-w64`):

```bash
make windows  # builds ./tetris.exe
```

## Controls

| Key | Action |
|-----|--------|
| `←` `→` | Move left / right |
| `↑` | Rotate (SRS) |
| `↓` | Soft drop (+1 pt/cell) |
| `Space` | Hard drop (+2 pts/cell) |
| `Q` | Quit |

## Project structure

```
├── game.h              Shared types: Board, GameState, Action
├── game.cpp            Game logic: pieces, collision, SRS kicks,
│                       line clearing, scoring, 7-bag randomiser
├── game_logic.h        Declarations for game.cpp free functions
├── renderer.h          Abstract Renderer interface
├── ansi_renderer.h/.cpp  POSIX/ANSI terminal renderer (Linux/macOS)
├── windows_renderer.h/.cpp  Windows Console API renderer
├── main.cpp            Game loop — injects renderer via unique_ptr<Renderer>
├── test_runner.h       Zero-dependency single-header test framework
├── tests.cpp           37 unit tests for game logic
├── Makefile            Build targets (see below)
└── dist/               Pre-built binaries
```

### Makefile targets

| Target | Description |
|--------|-------------|
| `make` | Build `./tetris` (Linux, debug-friendly `-O2`) |
| `make windows` | Cross-compile `./tetris.exe` via `mingw-w64` |
| `make test` | Build and run the unit test suite |
| `make clean` | Remove build artefacts |

## Architecture

The `Renderer` interface decouples all terminal/display code from the game
loop. `main.cpp` selects the renderer at compile time via `#ifdef _WIN32`
and injects it through a `unique_ptr<Renderer>`. To add a new backend
(SDL2, ncurses, web canvas via Emscripten, …):

1. Create `my_renderer.h/.cpp` implementing the five virtual methods:

   ```cpp
   void   init()                    override;
   void   shutdown()                override;
   void   draw(const GameState& s)  override;
   void   drawGameOver(int score)   override;
   Action pollInput()               override;
   ```

2. Replace `PlatformRenderer` in `main.cpp` with your new class (or add
   another `#ifdef` branch).

3. Add a build target in the Makefile.

## Running the tests

```bash
make test
```

The test suite covers `fits()`, `lockPiece()`, `clearLines()`, `ghostRow()`,
`lineScore()`, `tryRotate()`, and piece-table invariants — 37 tests, zero
external dependencies.

## Releasing

Pushing a `v*` tag triggers `.github/workflows/release.yml`, which builds
fresh Linux and Windows binaries on `ubuntu-latest` and publishes them as
GitHub Release assets.

```bash
git tag v1.x.x
git push origin v1.x.x
```
