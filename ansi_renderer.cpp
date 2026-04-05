// ANSI terminal renderer — double-buffered implementation

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

// ── Init / shutdown ───────────────────────────────────────────────────────────

void AnsiRenderer::init() {
    tcgetattr(STDIN_FILENO, &_saved);
    termios raw = _saved;
    raw.c_lflag &= ~static_cast<unsigned>(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    // Mark every cell as not-yet-drawn so the first frame writes everything
    for (auto& row : _front) row.fill(-1);
    _buf.reserve(8192);

    std::cout << "\033[?25l\033[2J";  // hide cursor, clear screen
    std::cout.flush();
}

void AnsiRenderer::shutdown() {
    tcsetattr(STDIN_FILENO, TCSANOW, &_saved);
    std::cout << "\033[?25h\033[2J\033[H";
    std::cout.flush();
}

// ── Input ─────────────────────────────────────────────────────────────────────

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

// ── Output helpers (append to _buf, never write directly) ────────────────────

void AnsiRenderer::appendMoveTo(int row, int col) {
    _buf += "\033[";
    _buf += std::to_string(row);
    _buf += ';';
    _buf += std::to_string(col);
    _buf += 'H';
}

void AnsiRenderer::appendCell(int colorId) {
    if (colorId == 0) {
        _buf += "  ";
    } else {
        _buf += pieceColor(colorId);
        _buf += "██";
        _buf += RESET;
    }
}

// ── Border (drawn once) ───────────────────────────────────────────────────────

void AnsiRenderer::drawBorder() {
    _buf += pieceColor(9);

    appendMoveTo(BOARD_ROW - 1, BOARD_COL - 1);
    _buf += "┌";
    for (int c = 0; c < BOARD_W; ++c) _buf += "──";
    _buf += "┐";

    for (int r = 0; r < BOARD_H; ++r) {
        appendMoveTo(BOARD_ROW + r, BOARD_COL - 1);
        _buf += "│";
        appendMoveTo(BOARD_ROW + r, BOARD_COL + BOARD_W * 2);
        _buf += "│";
    }

    appendMoveTo(BOARD_ROW + BOARD_H, BOARD_COL - 1);
    _buf += "└";
    for (int c = 0; c < BOARD_W; ++c) _buf += "──";
    _buf += "┘";

    _buf += RESET;
}

// ── Sidebar (only changed values are rewritten) ───────────────────────────────

void AnsiRenderer::flushSidebar(int score, int level, int lines, int nextPiece) {
    const auto pr = [&](int r, const std::string& s) {
        appendMoveTo(r, SIDE_COL);
        _buf += "\033[K";
        _buf += s;
    };

    // Static header — written once alongside the border
    if (!_borderDrawn) {
        pr(BOARD_ROW,     "┌─────────────┐");
        pr(BOARD_ROW + 1, "│    TETRIS   │");
        pr(BOARD_ROW + 2, "└─────────────┘");
        pr(BOARD_ROW + 4,  "  SCORE");
        pr(BOARD_ROW + 7,  "  LEVEL");
        pr(BOARD_ROW + 10, "  LINES");
        pr(BOARD_ROW + 13, "  NEXT");
        pr(BOARD_ROW + 20, "  CONTROLS");
        pr(BOARD_ROW + 21, "  ←→  move");
        pr(BOARD_ROW + 22, "  ↑   rotate");
        pr(BOARD_ROW + 23, "  ↓   soft drop");
        pr(BOARD_ROW + 24, "  SPC hard drop");
        pr(BOARD_ROW + 25, "  Q   quit");
    }

    if (score != _prevScore) {
        pr(BOARD_ROW + 5, "  " + std::to_string(score) + "      ");
        _prevScore = score;
    }
    if (level != _prevLevel) {
        pr(BOARD_ROW + 8, "  " + std::to_string(level));
        _prevLevel = level;
    }
    if (lines != _prevLines) {
        pr(BOARD_ROW + 11, "  " + std::to_string(lines));
        _prevLines = lines;
    }
    if (nextPiece != _prevNext) {
        // Clear previous next-piece preview
        for (int r = 0; r < 4; ++r) {
            appendMoveTo(BOARD_ROW + 15 + r, SIDE_COL);
            _buf += "        ";
        }
        // Draw new one
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (PIECES[nextPiece][0][r][c]) {
                    appendMoveTo(BOARD_ROW + 15 + r, SIDE_COL + c * 2);
                    _buf += pieceColor(nextPiece + 1);
                    _buf += "██";
                    _buf += RESET;
                }
        _prevNext = nextPiece;
    }
}

// ── Main draw — double-buffered board diff ────────────────────────────────────

void AnsiRenderer::draw(const GameState& s) {
    _buf.clear();

    if (!_borderDrawn) {
        drawBorder();
        _borderDrawn = true;
    }

    // ── Build back buffer ────────────────────────────────────────────────────
    // Start with locked cells, then overlay ghost, then active piece.
    Board back = s.board;

    const auto& shape = PIECES[s.curPiece][s.curRot];

    if (s.ghostY != s.curY) {
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (shape[r][c]) {
                    const int br = s.ghostY + r, bc = s.curX + c;
                    if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W && !back[br][bc])
                        back[br][bc] = 8;
                }
    }

    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (shape[r][c]) {
                const int br = s.curY + r, bc = s.curX + c;
                if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W)
                    back[br][bc] = s.curPiece + 1;
            }

    // ── Diff front vs back, emit only changed cells ──────────────────────────
    for (int r = 0; r < BOARD_H; ++r)
        for (int c = 0; c < BOARD_W; ++c)
            if (back[r][c] != _front[r][c]) {
                appendMoveTo(BOARD_ROW + r, BOARD_COL + c * 2);
                appendCell(back[r][c]);
                _front[r][c] = back[r][c];
            }

    flushSidebar(s.score, s.level, s.totalLines, s.nextPiece);

    // Single write syscall for the whole frame
    std::cout << _buf;
    std::cout.flush();
}

// ── Game over ─────────────────────────────────────────────────────────────────

void AnsiRenderer::drawGameOver(int score) {
    appendMoveTo(BOARD_ROW + BOARD_H / 2 - 1, BOARD_COL - 1);
    _buf += "\033[41m\033[97m  GAME  OVER   \033[0m";
    appendMoveTo(BOARD_ROW + BOARD_H / 2,     BOARD_COL - 1);
    _buf += "\033[41m\033[97m  Score: " + std::to_string(score) + "     \033[0m";
    appendMoveTo(BOARD_ROW + BOARD_H / 2 + 1, BOARD_COL - 1);
    _buf += "\033[41m\033[97m  Press any key \033[0m";
    std::cout << _buf;
    std::cout.flush();
    _buf.clear();

    while (pollInput() == Action::None)
        std::this_thread::sleep_for(50ms);
}
