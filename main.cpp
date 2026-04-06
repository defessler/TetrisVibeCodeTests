// main.cpp — game loop; renderer is injected via the Renderer interface.
// Renderer is selected at compile time via preprocessor.

#include "game.h"
#include "game_logic.h"
#include "renderer.h"
#if defined(_WIN32) && defined(PIXEL_ART)
#  include "windows_pixel_renderer.h"
   using PlatformRenderer = WindowsPixelRenderer;
#elif defined(_WIN32)
#  include "windows_renderer.h"
   using PlatformRenderer = WindowsRenderer;
#elif defined(PIXEL_ART)
#  include "pixel_renderer.h"
   using PlatformRenderer = PixelRenderer;
#else
#  include "ansi_renderer.h"
   using PlatformRenderer = AnsiRenderer;
#endif

#include <chrono>
#include <memory>
#include <thread>

using namespace std::chrono_literals;
using SteadyClock = std::chrono::steady_clock;

// Drop-interval per level (Tetris guideline approximation)
static constexpr std::chrono::milliseconds dropInterval(int level) noexcept {
    constexpr std::chrono::milliseconds table[] = {
        800ms,720ms,630ms,550ms,470ms,380ms,300ms,220ms,130ms,100ms,80ms
    };
    return table[(level < 10) ? level : 10];
}

int main() {
    // ── Renderer selection ────────────────────────────────────────────────────
    // Swap this line (or make it a factory / command-line arg) to change backend
    std::unique_ptr<Renderer> renderer = std::make_unique<PlatformRenderer>();
    renderer->init();

    // ── Game state ────────────────────────────────────────────────────────────
    Board board{};

    int curPiece  = nextBag();
    int curRot    = 0;
    int curX      = 3;
    int curY      = -1;
    int nextPiece = nextBag();
    int score     = 0;
    int level     = 0;
    int totalLines = 0;
    int lockDelay  = 0;
    bool gameOver  = false;

    auto dropTime = SteadyClock::now();

    // Helper: lock active piece, clear lines, spawn next
    auto spawnNext = [&]() -> bool {
        lockPiece(board, curPiece, curRot, curX, curY);
        const int cleared = clearLines(board);
        totalLines += cleared;
        score      += lineScore(cleared, level);
        level       = totalLines / 10;

        curPiece = nextPiece;
        nextPiece = nextBag();
        curRot = 0;
        curX   = 3;
        curY   = -1;
        lockDelay = 0;
        dropTime  = SteadyClock::now();

        return fits(board, curPiece, curRot, curX, curY);  // false → game over
    };

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!gameOver) {
        // Input
        switch (renderer->pollInput()) {
            case Action::Quit:
                gameOver = true;
                break;

            case Action::Left:
                if (fits(board, curPiece, curRot, curX - 1, curY)) --curX;
                break;

            case Action::Right:
                if (fits(board, curPiece, curRot, curX + 1, curY)) ++curX;
                break;

            case Action::SoftDrop:
                if (fits(board, curPiece, curRot, curX, curY + 1)) {
                    ++curY;
                    ++score;
                    dropTime = SteadyClock::now();
                }
                break;

            case Action::Rotate:
                tryRotate(board, curPiece, curRot, curX, curY);
                break;

            case Action::HardDrop: {
                const int gy = ghostRow(board, curPiece, curRot, curX, curY);
                score += 2 * (gy - curY);
                curY = gy;
                if (!spawnNext()) gameOver = true;
                break;
            }

            default: break;
        }

        // Gravity
        if (SteadyClock::now() - dropTime >= dropInterval(level)) {
            if (fits(board, curPiece, curRot, curX, curY + 1)) {
                ++curY;
                lockDelay = 0;
            } else if (++lockDelay >= 1) {
                if (!spawnNext()) gameOver = true;
            }
            dropTime = SteadyClock::now();
        }

        // Render
        const GameState state{
            board,
            curPiece, curRot, curX, curY,
            ghostRow(board, curPiece, curRot, curX, curY),
            nextPiece,
            score, level, totalLines
        };
        renderer->draw(state);

        std::this_thread::sleep_for(16ms);  // ~60 fps cap
    }

    if (gameOver) renderer->drawGameOver(score);

    renderer->shutdown();
    return 0;
}
