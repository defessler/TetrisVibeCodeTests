#pragma once
// ANSI terminal renderer — no external dependencies beyond POSIX.
// Uses escape codes for colour + cursor positioning, termios for raw input.
// Double-buffered: only terminal cells that changed are rewritten each frame.

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

    // ── Double buffer ─────────────────────────────────────────────────────────
    // Stores the color id currently visible on screen for every board cell.
    // -1 = never drawn (forces a write on the first frame).
    std::array<std::array<int, BOARD_W>, BOARD_H> _front{};

    // Sidebar values last written to the terminal (-1 = never written)
    int _prevScore{-1}, _prevLevel{-1}, _prevLines{-1}, _prevNext{-1};
    bool _borderDrawn{false};

    // ── Output batching ───────────────────────────────────────────────────────
    // All draw calls append to _buf; flushed once per frame in draw().
    std::string _buf;

    // Terminal layout constants
    static constexpr int BOARD_COL = 3;
    static constexpr int BOARD_ROW = 2;
    static constexpr int SIDE_COL  = BOARD_COL + BOARD_W * 2 + 3;

    void appendMoveTo(int row, int col);
    void drawBorder();
    void flushSidebar(int score, int level, int lines, int nextPiece);

    [[nodiscard]] static const char* pieceColor(int id) noexcept;
};
