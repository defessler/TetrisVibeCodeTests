#pragma once
// Free functions used by game.cpp and main.cpp
#include "game.h"

bool fits(const Board& board, int piece, int rot, int px, int py);
void lockPiece(Board& board, int piece, int rot, int px, int py);
int  clearLines(Board& board);
int  ghostRow(const Board& board, int piece, int rot, int px, int py);
int  lineScore(int lines, int level);
int  nextBag();
bool tryRotate(const Board& board, int piece, int& rot, int& px, int& py);
