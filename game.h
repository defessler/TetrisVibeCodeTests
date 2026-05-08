#pragma once
// Core game types shared between game logic and all renderers.

#include <array>

static constexpr int BOARD_W = 10;
static constexpr int BOARD_H = 20;

using Board = std::array<std::array<int, BOARD_W>, BOARD_H>;

// ── Tetromino shapes ──────────────────────────────────────────────────────────
// 7 pieces × 4 rotations × 4 rows × 4 cols
using Shape = std::array<std::array<std::array<int,4>,4>,4>;

extern const std::array<Shape, 7> PIECES;

// ── Actions ───────────────────────────────────────────────────────────────────
enum class Action { None, Left, Right, SoftDrop, Rotate, HardDrop, Quit };

// ── Full game state (passed to renderers each frame) ─────────────────────────
struct GameState {
    const Board& board;   // locked cells only (color ids 1-7, 0=empty)
    int curPiece;         // 0-6
    int curRot;
    int curX, curY;       // board coordinates of active piece
    int ghostY;           // row where the ghost piece sits
    int nextPiece;        // 0-6
    int score;
    int level;
    int totalLines;
};
