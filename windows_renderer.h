#pragma once
// Windows Console renderer.
// Uses Windows Console API for raw input + enables Virtual Terminal Processing
// so the same ANSI escape codes used by AnsiRenderer work unchanged.
// Requires Windows 10 v1511+ (build 10586) for VT support.

#include "renderer.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>

class WindowsRenderer : public Renderer {
public:
    void   init()                    override;
    void   shutdown()                override;
    void   draw(const GameState& s)  override;
    void   drawGameOver(int score)   override;
    Action pollInput()               override;

private:
    HANDLE _hIn{};
    HANDLE _hOut{};
    DWORD  _savedInMode{};
    DWORD  _savedOutMode{};
    bool   _firstDraw{true};

    static constexpr int BOARD_COL = 3;
    static constexpr int BOARD_ROW = 2;
    static constexpr int SIDE_COL  = BOARD_COL + BOARD_W * 2 + 3;

    void moveTo(int row, int col) const;
    void drawBorder()                                                const;
    void drawBoard(const Board& board)                               const;
    void drawPiece(int piece, int rot, int px, int py, int colorId)  const;
    void drawSidebar(int score, int level, int lines, int nextPiece) const;
    void drawCell(int termRow, int termCol, int colorId)             const;

    [[nodiscard]] static const char* pieceColor(int id) noexcept;
};
