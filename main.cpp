// main.cpp — game loop; renderer is injected via the Renderer interface.
// To use a different backend, swap AnsiRenderer for your own Renderer subclass.

#include "game.h"
#include "game_logic.h"
#include "renderer.h"
#include "ansi_renderer.h"   // ← change this include to switch renderers

#include <chrono>
#include <memory>
#include <unistd.h>

// Drop-interval table indexed by level (ms)
static long dropInterval(int level) {
    static const long table[] = {800,720,630,550,470,380,300,220,130,100,80};
    int idx = (level < 10) ? level : 10;
    return table[idx];
}

int main() {
    // ── Renderer selection ────────────────────────────────────────────────────
    // Swap this line (or make it a factory / command-line arg) to change backend
    std::unique_ptr<Renderer> renderer = std::make_unique<AnsiRenderer>();

    renderer->init();

    // ── Game state ────────────────────────────────────────────────────────────
    Board board{};

    int curPiece  = nextBag();
    int curRot    = 0;
    int curX      = 3, curY = -1;
    int nextPiece = nextBag();

    int score = 0, level = 0, totalLines = 0;
    bool gameOver  = false;
    int  lockDelay = 0;

    using clock = std::chrono::steady_clock;
    using ms    = std::chrono::milliseconds;
    auto dropTime = clock::now();

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (!gameOver) {
        // Input
        Action act = renderer->pollInput();
        [[maybe_unused]] bool moved = false;

        switch (act) {
            case Action::Quit: goto done;

            case Action::Left:
                if (fits(board, curPiece, curRot, curX - 1, curY)) { --curX; moved = true; }
                break;

            case Action::Right:
                if (fits(board, curPiece, curRot, curX + 1, curY)) { ++curX; moved = true; }
                break;

            case Action::SoftDrop:
                if (fits(board, curPiece, curRot, curX, curY + 1)) {
                    ++curY; score += 1; moved = true;
                    dropTime = clock::now();
                }
                break;

            case Action::Rotate:
                if (tryRotate(board, curPiece, curRot, curX, curY)) moved = true;
                break;

            case Action::HardDrop: {
                int gy = ghostRow(board, curPiece, curRot, curX, curY);
                score += 2 * (gy - curY);
                curY = gy;
                lockPiece(board, curPiece, curRot, curX, curY);
                int cleared = clearLines(board);
                totalLines += cleared;
                score      += lineScore(cleared, level);
                level       = totalLines / 10;

                curPiece = nextPiece;
                nextPiece = nextBag();
                curRot = 0; curX = 3; curY = -1;
                dropTime  = clock::now();
                lockDelay = 0;

                if (!fits(board, curPiece, curRot, curX, curY)) gameOver = true;
                moved = true;
                break;
            }

            default: break;
        }

        // Gravity
        long elapsed = std::chrono::duration_cast<ms>(clock::now() - dropTime).count();
        if (elapsed >= dropInterval(level)) {
            if (fits(board, curPiece, curRot, curX, curY + 1)) {
                ++curY;
                lockDelay = 0;
            } else {
                ++lockDelay;
                if (lockDelay >= 1) {
                    lockPiece(board, curPiece, curRot, curX, curY);
                    int cleared = clearLines(board);
                    totalLines += cleared;
                    score      += lineScore(cleared, level);
                    level       = totalLines / 10;

                    curPiece = nextPiece;
                    nextPiece = nextBag();
                    curRot = 0; curX = 3; curY = -1;
                    lockDelay = 0;

                    if (!fits(board, curPiece, curRot, curX, curY)) gameOver = true;
                    moved = true;
                }
            }
            dropTime = clock::now();
        }

        // Render
        GameState state{
            board,
            curPiece, curRot, curX, curY,
            ghostRow(board, curPiece, curRot, curX, curY),
            nextPiece,
            score, level, totalLines
        };
        renderer->draw(state);

        usleep(16000);  // ~60 fps cap
    }

done:
    if (gameOver)
        renderer->drawGameOver(score);

    renderer->shutdown();
    return 0;
}
