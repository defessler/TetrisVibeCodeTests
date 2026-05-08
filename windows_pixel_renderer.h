#pragma once
// Windows Console pixel-art renderer.
// Identical visual output to PixelRenderer (Linux) — 3-col ▌█▐ bevel blocks
// with 24-bit true colour — using the Windows Console API for I/O.
// Requires Windows 10 v1511+ for ENABLE_VIRTUAL_TERMINAL_PROCESSING.

#include "renderer.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <string>

class WindowsPixelRenderer : public Renderer {
public:
    void init()                   override;
    void shutdown()               override;
    void draw(const GameState& s) override;
    void drawGameOver(int score)  override;
    Action pollInput()            override;

private:
    HANDLE _hIn{};
    HANDLE _hOut{};
    DWORD  _savedInMode{};
    DWORD  _savedOutMode{};

    std::array<std::array<int, BOARD_W>, BOARD_H> _front{};
    int  _prevScore{-1}, _prevLevel{-1}, _prevLines{-1}, _prevNext{-1};
    bool _staticDrawn{false};
    std::string _buf;

    static constexpr int BOARD_ROW = 2;
    static constexpr int BOARD_COL = 3;
    static constexpr int CELL_W    = 3;
    static constexpr int SIDE_COL  = BOARD_COL + BOARD_W * CELL_W + 2;

    void flush();
    void moveTo(int row, int col);
    void appendCell(int id);
    void drawStatic();
    void flushSidebar(int score, int level, int lines, int next);
};
