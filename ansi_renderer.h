#pragma once
// ANSI terminal renderer — no external dependencies beyond POSIX.
//
// Double-buffered: _front[r][c] tracks what is currently visible.
// Only dirty rows are written each frame; if nothing changed, no write
// is made at all. All output is batched into _buf and flushed with a
// single write() syscall wrapped in BSU/ESU.

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
    termios _saved{};

    // Front buffer: color id currently on screen (-1 = never drawn)
    std::array<std::array<int, BOARD_W>, BOARD_H> _front{};

    // Sidebar state
    int  _prevScore{-1}, _prevLevel{-1}, _prevLines{-1}, _prevNext{-1};
    bool _staticDrawn{false};

    // Output buffer — pre-reserved, reused each frame
    std::string _buf;

    static constexpr int BOARD_COL = 3;
    static constexpr int BOARD_ROW = 2;
    static constexpr int SIDE_COL  = BOARD_COL + BOARD_W * 2 + 3;

    void appendMoveTo(int row, int col);
    void drawStatic();

    [[nodiscard]] static const char* pieceColor(int id) noexcept;
};
