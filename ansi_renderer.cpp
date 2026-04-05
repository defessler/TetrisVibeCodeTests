// ANSI terminal renderer implementation

#include "ansi_renderer.h"
#include <iostream>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

static constexpr std::string_view RESET = "\033[0m";

const char* AnsiRenderer::pieceColor(int id) noexcept {
    switch (id) {
        case 1: return "\033[96m";  // I – cyan
        case 2: return "\033[93m";  // O – yellow
        case 3: return "\033[95m";  // T – magenta
        case 4: return "\033[92m";  // S – green
        case 5: return "\033[91m";  // Z – red
        case 6: return "\033[34m";  // J – blue
        case 7: return "\033[33m";  // L – orange
        case 8: return "\033[90m";  // ghost
        case 9: return "\033[37m";  // border
        default: return "\033[0m";
    }
}

void AnsiRenderer::moveTo(int row, int col) const {
    std::cout << "\033[" << row << ";" << col << "H";
}

void AnsiRenderer::init() {
    tcgetattr(STDIN_FILENO, &_saved);
    termios raw = _saved;
    raw.c_lflag &= ~static_cast<unsigned>(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    std::cout << "\033[?25l"  // hide cursor
              << "\033[2J";   // clear screen
    std::cout.flush();
}

void AnsiRenderer::shutdown() {
    tcsetattr(STDIN_FILENO, TCSANOW, &_saved);
    std::cout << "\033[?25h\033[2J\033[H";
    std::cout.flush();
}

Action AnsiRenderer::pollInput() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv{0, 0};
    if (select(1, &fds, nullptr, nullptr, &tv) <= 0) return Action::None;

    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return Action::None;

    if (c == 'q' || c == 'Q') return Action::Quit;
    if (c == ' ')              return Action::HardDrop;

    if (c == '\033') {
        unsigned char seq[2] = {0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
            if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                switch (seq[1]) {
                    case 'A': return Action::Rotate;
                    case 'B': return Action::SoftDrop;
                    case 'C': return Action::Right;
                    case 'D': return Action::Left;
                    default:  break;
                }
            }
        }
    }
    return Action::None;
}

void AnsiRenderer::drawCell(int termRow, int termCol, int colorId) const {
    moveTo(termRow, termCol);
    if (colorId == 0) {
        std::cout << "  ";
    } else {
        std::cout << pieceColor(colorId) << "██" << RESET;
    }
}

void AnsiRenderer::drawBorder() const {
    moveTo(BOARD_ROW - 1, BOARD_COL - 1);
    std::cout << pieceColor(9) << "┌";
    for (int c = 0; c < BOARD_W; ++c) std::cout << "──";
    std::cout << "┐" << RESET;

    for (int r = 0; r < BOARD_H; ++r) {
        moveTo(BOARD_ROW + r, BOARD_COL - 1);
        std::cout << pieceColor(9) << "│" << RESET;
        moveTo(BOARD_ROW + r, BOARD_COL + BOARD_W * 2);
        std::cout << pieceColor(9) << "│" << RESET;
    }

    moveTo(BOARD_ROW + BOARD_H, BOARD_COL - 1);
    std::cout << pieceColor(9) << "└";
    for (int c = 0; c < BOARD_W; ++c) std::cout << "──";
    std::cout << "┘" << RESET;
}

void AnsiRenderer::drawBoard(const Board& board) const {
    for (int r = 0; r < BOARD_H; ++r)
        for (int c = 0; c < BOARD_W; ++c)
            drawCell(BOARD_ROW + r, BOARD_COL + c * 2, board[r][c]);
}

void AnsiRenderer::drawPiece(int piece, int rot, int px, int py, int colorId) const {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (PIECES[piece][rot][r][c]) {
                const int br = py + r, bc = px + c;
                if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W)
                    drawCell(BOARD_ROW + br, BOARD_COL + bc * 2, colorId);
            }
}

void AnsiRenderer::drawSidebar(int score, int level, int lines, int nextPiece) const {
    // Print a line at row r, aligned to SIDE_COL; erase to end of line first
    const auto pr = [&](int r, std::string_view s) {
        moveTo(r, SIDE_COL);
        std::cout << "\033[K" << s;
    };

    pr(BOARD_ROW,     "┌─────────────┐");
    pr(BOARD_ROW + 1, "│    TETRIS   │");
    pr(BOARD_ROW + 2, "└─────────────┘");

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
                std::cout << pieceColor(nextPiece + 1) << "██" << RESET;
            }

    pr(BOARD_ROW + 20, "  CONTROLS");
    pr(BOARD_ROW + 21, "  ←→  move");
    pr(BOARD_ROW + 22, "  ↑   rotate");
    pr(BOARD_ROW + 23, "  ↓   soft drop");
    pr(BOARD_ROW + 24, "  SPC hard drop");
    pr(BOARD_ROW + 25, "  Q   quit");
}

void AnsiRenderer::draw(const GameState& s) {
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

void AnsiRenderer::drawGameOver(int score) {
    moveTo(BOARD_ROW + BOARD_H / 2 - 1, BOARD_COL - 1);
    std::cout << "\033[41m\033[97m  GAME  OVER   \033[0m";
    moveTo(BOARD_ROW + BOARD_H / 2,     BOARD_COL - 1);
    std::cout << "\033[41m\033[97m  Score: " << score << "     \033[0m";
    moveTo(BOARD_ROW + BOARD_H / 2 + 1, BOARD_COL - 1);
    std::cout << "\033[41m\033[97m  Press any key \033[0m";
    std::cout.flush();

    while (pollInput() == Action::None)
        std::this_thread::sleep_for(50ms);
}
