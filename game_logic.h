#pragma once
// Free functions used by game.cpp and main.cpp
#include "game.h"

[[nodiscard]] bool fits(const Board& board, int piece, int rot, int px, int py) noexcept;
void              lockPiece(Board& board, int piece, int rot, int px, int py) noexcept;
[[nodiscard]] int clearLines(Board& board) noexcept;
[[nodiscard]] int ghostRow(const Board& board, int piece, int rot, int px, int py) noexcept;
[[nodiscard]] int lineScore(int lines, int level) noexcept;
[[nodiscard]] int nextBag();
bool tryRotate(const Board& board, int piece, int& rot, int& px, int& py) noexcept;
