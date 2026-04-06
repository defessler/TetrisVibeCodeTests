#pragma once
// ANSI terminal renderer — no external dependencies beyond POSIX.
// Writes the full board every frame inside BSU…ESU via a single write() call.

#include "renderer.h"
#include <array>
#include <string>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

class AnsiRenderer : public Renderer {
public:
    void   init()                    override;
    void   shutdown()                override;
    void   draw(const GameState& s)  override;
    void   drawGameOver(int score)   override;
    Action pollInput()               override;

private:
    termios     _saved{};
    bool        _staticDrawn{false};
    int         _prevNext{-1};
    std::string _buf;

    static constexpr int BOARD_COL = 3;
    static constexpr int BOARD_ROW = 2;
    static constexpr int SIDE_COL  = BOARD_COL + BOARD_W * 2 + 3;

    void appendMoveTo(int row, int col);
    void drawStatic();

    [[nodiscard]] static const char* pieceColor(int id) noexcept;
};
