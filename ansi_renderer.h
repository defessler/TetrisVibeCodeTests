#pragma once
// ANSI terminal renderer — no external dependencies beyond POSIX.
// Uses escape codes for colour + cursor positioning, termios for raw input.

#include "renderer.h"
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>

class AnsiRenderer : public Renderer {
public:
    void   init()                    override;
    void   shutdown()                override;
    void   draw(const GameState& s)  override;
    void   drawGameOver(int score)   override;
    Action pollInput()               override;

private:
    termios _saved{};
    bool    _firstDraw{true};

    // Terminal layout constants
    static constexpr int BOARD_COL = 3;
    static constexpr int BOARD_ROW = 2;
    static constexpr int SIDE_COL  = BOARD_COL + BOARD_W * 2 + 3;

    void moveTo(int row, int col) const;
    void drawBorder()                                                 const;
    void drawBoard(const Board& board)                                const;
    void drawPiece(int piece, int rot, int px, int py, int colorId)   const;
    void drawSidebar(int score, int level, int lines, int nextPiece)  const;
    void drawCell(int termRow, int termCol, int colorId)              const;

    [[nodiscard]] static const char* pieceColor(int id) noexcept;
};
