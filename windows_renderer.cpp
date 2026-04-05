// Windows Console renderer implementation

#include "windows_renderer.h"
#include <iostream>
#include <string>
#include <string_view>

static constexpr std::string_view RESET = "\033[0m";

const char* WindowsRenderer::pieceColor(int id) noexcept {
    switch (id) {
        case 1: return "\033[96m";
        case 2: return "\033[93m";
        case 3: return "\033[95m";
        case 4: return "\033[92m";
        case 5: return "\033[91m";
        case 6: return "\033[34m";
        case 7: return "\033[33m";
        case 8: return "\033[90m";
        case 9: return "\033[37m";
        default: return "\033[0m";
    }
}

void WindowsRenderer::init() {
    _hIn  = GetStdHandle(STD_INPUT_HANDLE);
    _hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    GetConsoleMode(_hIn,  &_savedInMode);
    GetConsoleMode(_hOut, &_savedOutMode);

    // Raw input: disable line buffering and echo
    SetConsoleMode(_hIn, 0);

    // Enable ANSI/VT escape code processing on the output
    SetConsoleMode(_hOut,
        _savedOutMode
        | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        | DISABLE_NEWLINE_AUTO_RETURN);

    // Set UTF-8 so the block characters render correctly
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "\033[?25l"  // hide cursor
              << "\033[2J";   // clear screen
    std::cout.flush();
}

void WindowsRenderer::shutdown() {
    SetConsoleMode(_hIn,  _savedInMode);
    SetConsoleMode(_hOut, _savedOutMode);
    std::cout << "\033[?25h\033[2J\033[H";
    std::cout.flush();
}

Action WindowsRenderer::pollInput() {
    // Peek without blocking
    DWORD count = 0;
    GetNumberOfConsoleInputEvents(_hIn, &count);
    if (count == 0) return Action::None;

    INPUT_RECORD rec{};
    DWORD read = 0;
    ReadConsoleInput(_hIn, &rec, 1, &read);

    if (read == 0 || rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
        return Action::None;

    const WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
    const char ch = static_cast<char>(rec.Event.KeyEvent.uChar.AsciiChar);

    if (ch == 'q' || ch == 'Q') return Action::Quit;
    if (ch == ' ')               return Action::HardDrop;

    switch (vk) {
        case VK_UP:    return Action::Rotate;
        case VK_DOWN:  return Action::SoftDrop;
        case VK_LEFT:  return Action::Left;
        case VK_RIGHT: return Action::Right;
        default:       return Action::None;
    }
}

void WindowsRenderer::moveTo(int row, int col) const {
    std::cout << "\033[" << row << ";" << col << "H";
}

void WindowsRenderer::drawCell(int termRow, int termCol, int colorId) const {
    moveTo(termRow, termCol);
    if (colorId == 0)
        std::cout << "  ";
    else
        std::cout << pieceColor(colorId) << "\xE2\x96\x88\xE2\x96\x88" << RESET; // ██ in UTF-8
}

void WindowsRenderer::drawBorder() const {
    moveTo(BOARD_ROW - 1, BOARD_COL - 1);
    std::cout << pieceColor(9)
              << "\xE2\x94\x8C";  // ┌
    for (int c = 0; c < BOARD_W; ++c) std::cout << "\xE2\x94\x80\xE2\x94\x80"; // ──
    std::cout << "\xE2\x94\x90" << RESET; // ┐

    for (int r = 0; r < BOARD_H; ++r) {
        moveTo(BOARD_ROW + r, BOARD_COL - 1);
        std::cout << pieceColor(9) << "\xE2\x94\x82" << RESET; // │
        moveTo(BOARD_ROW + r, BOARD_COL + BOARD_W * 2);
        std::cout << pieceColor(9) << "\xE2\x94\x82" << RESET;
    }

    moveTo(BOARD_ROW + BOARD_H, BOARD_COL - 1);
    std::cout << pieceColor(9)
              << "\xE2\x94\x94";  // └
    for (int c = 0; c < BOARD_W; ++c) std::cout << "\xE2\x94\x80\xE2\x94\x80";
    std::cout << "\xE2\x94\x98" << RESET; // ┘
}

void WindowsRenderer::drawBoard(const Board& board) const {
    for (int r = 0; r < BOARD_H; ++r)
        for (int c = 0; c < BOARD_W; ++c)
            drawCell(BOARD_ROW + r, BOARD_COL + c * 2, board[r][c]);
}

void WindowsRenderer::drawPiece(int piece, int rot, int px, int py, int colorId) const {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (PIECES[piece][rot][r][c]) {
                const int br = py + r, bc = px + c;
                if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W)
                    drawCell(BOARD_ROW + br, BOARD_COL + bc * 2, colorId);
            }
}

void WindowsRenderer::drawSidebar(int score, int level, int lines, int nextPiece) const {
    const auto pr = [&](int r, std::string_view s) {
        moveTo(r, SIDE_COL);
        std::cout << "\033[K" << s;
    };

    pr(BOARD_ROW,     "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90"); // ┌─────────────┐
    pr(BOARD_ROW + 1, "\xE2\x94\x82    TETRIS   \xE2\x94\x82"); // │    TETRIS   │
    pr(BOARD_ROW + 2, "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98"); // └─────────────┘

    pr(BOARD_ROW + 4,  "  SCORE");
    pr(BOARD_ROW + 5,  "  " + std::to_string(score));
    pr(BOARD_ROW + 7,  "  LEVEL");
    pr(BOARD_ROW + 8,  "  " + std::to_string(level));
    pr(BOARD_ROW + 10, "  LINES");
    pr(BOARD_ROW + 11, "  " + std::to_string(lines));
    pr(BOARD_ROW + 13, "  NEXT");

    for (int r = 0; r < 4; ++r) {
        moveTo(BOARD_ROW + 15 + r, SIDE_COL);
        std::cout << "        ";
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (PIECES[nextPiece][0][r][c]) {
                moveTo(BOARD_ROW + 15 + r, SIDE_COL + c * 2);
                std::cout << pieceColor(nextPiece + 1)
                          << "\xE2\x96\x88\xE2\x96\x88" << RESET;
            }

    pr(BOARD_ROW + 20, "  CONTROLS");
    pr(BOARD_ROW + 21, "  \xE2\x86\x90\xE2\x86\x92  move");       // ←→
    pr(BOARD_ROW + 22, "  \xE2\x86\x91   rotate");                  // ↑
    pr(BOARD_ROW + 23, "  \xE2\x86\x93   soft drop");               // ↓
    pr(BOARD_ROW + 24, "  SPC hard drop");
    pr(BOARD_ROW + 25, "  Q   quit");
}

void WindowsRenderer::draw(const GameState& s) {
    if (_firstDraw) {
        drawBorder();
        _firstDraw = false;
    }
    drawBoard(s.board);
    if (s.ghostY != s.curY)
        drawPiece(s.curPiece, s.curRot, s.curX, s.ghostY, 8);
    drawPiece(s.curPiece, s.curRot, s.curX, s.curY, s.curPiece + 1);
    drawSidebar(s.score, s.level, s.totalLines, s.nextPiece);
    std::cout.flush();
}

void WindowsRenderer::drawGameOver(int score) {
    moveTo(BOARD_ROW + BOARD_H / 2 - 1, BOARD_COL - 1);
    std::cout << "\033[41m\033[97m  GAME  OVER   \033[0m";
    moveTo(BOARD_ROW + BOARD_H / 2,     BOARD_COL - 1);
    std::cout << "\033[41m\033[97m  Score: " << score << "     \033[0m";
    moveTo(BOARD_ROW + BOARD_H / 2 + 1, BOARD_COL - 1);
    std::cout << "\033[41m\033[97m  Press any key \033[0m";
    std::cout.flush();
    while (pollInput() == Action::None)
        Sleep(50);
}
