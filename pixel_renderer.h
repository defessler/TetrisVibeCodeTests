#pragma once
// Pixel-art terminal renderer.
//
// Each Tetris cell is 3 terminal columns wide, rendered with Unicode
// half-block characters and 24-bit true-colour ANSI codes:
//
//   ▌  LEFT HALF BLOCK   fg=highlight / bg=main  →  left edge is bright
//   █  FULL BLOCK        fg=main                 →  solid main colour
//   ▐  RIGHT HALF BLOCK  fg=shadow   / bg=main   →  right edge is dark
//
// This gives every block a clean left-lit 3-D bevel effect.

#include "renderer.h"
#include <array>
#include <string>
#include <termios.h>
#include <unistd.h>

class PixelRenderer : public Renderer {
public:
    void init()                   override;
    void shutdown()               override;
    void draw(const GameState& s) override;
    void drawGameOver(int score)  override;
    Action pollInput()            override;

private:
    termios _saved{};

    // Front buffer: cell id currently on screen (-1 = never drawn)
    std::array<std::array<int, BOARD_W>, BOARD_H> _front{};

    int  _prevScore{-1}, _prevLevel{-1}, _prevLines{-1}, _prevNext{-1};
    bool _staticDrawn{false};

    std::string _buf;   // batched output; flushed once per frame

    // Layout (1-indexed terminal rows / columns)
    static constexpr int BOARD_ROW = 2;
    static constexpr int BOARD_COL = 3;
    static constexpr int CELL_W    = 3;   // terminal columns per Tetris cell
    // right border is at BOARD_COL + BOARD_W*CELL_W, leave 1 gap → sidebar at +2
    static constexpr int SIDE_COL  = BOARD_COL + BOARD_W * CELL_W + 2;

    void moveTo(int row, int col);
    void appendCell(int id);
    void drawStatic();
    void flushSidebar(int score, int level, int lines, int next);
};
