// ANSI terminal renderer

#include "ansi_renderer.h"
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

static constexpr std::string_view RESET = "\033[0m";

static void writeAll(int fd, const char* p, std::size_t n) {
    while (n > 0) {
        ssize_t r = write(fd, p, n);
        if (r <= 0) return;
        p += r;
        n -= static_cast<std::size_t>(r);
    }
}

const char* AnsiRenderer::pieceColor(int id) noexcept {
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

void AnsiRenderer::init() {
    tcgetattr(STDIN_FILENO, &_saved);
    termios raw = _saved;
    raw.c_lflag &= ~static_cast<unsigned>(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    for (auto& row : _front) row.fill(-1);
    _buf.reserve(8192);

    std::string s;
    s += "\033[?25l";  // hide cursor
    s += "\033[2J";    // clear screen
    writeAll(STDOUT_FILENO, s.data(), s.size());
}

void AnsiRenderer::shutdown() {
    std::string s;
    s += "\033[?25h";  // show cursor
    s += "\033[2J";
    s += "\033[H";
    writeAll(STDOUT_FILENO, s.data(), s.size());
    tcsetattr(STDIN_FILENO, TCSANOW, &_saved);
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
        if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[')
            if (read(STDIN_FILENO, &seq[1], 1) == 1)
                switch (seq[1]) {
                    case 'A': return Action::Rotate;
                    case 'B': return Action::SoftDrop;
                    case 'C': return Action::Right;
                    case 'D': return Action::Left;
                    default: break;
                }
    }
    return Action::None;
}

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

void AnsiRenderer::drawBorder() {
    _buf += pieceColor(9);
    appendMoveTo(BOARD_ROW - 1, BOARD_COL - 1);
    _buf += "┌";
    for (int c = 0; c < BOARD_W; ++c) _buf += "──";
    _buf += "┐";
    for (int r = 0; r < BOARD_H; ++r) {
        appendMoveTo(BOARD_ROW + r, BOARD_COL - 1);      _buf += "│";
        appendMoveTo(BOARD_ROW + r, BOARD_COL + BOARD_W * 2); _buf += "│";
    }
    appendMoveTo(BOARD_ROW + BOARD_H, BOARD_COL - 1);
    _buf += "└";
    for (int c = 0; c < BOARD_W; ++c) _buf += "──";
    _buf += "┘";
    _buf += RESET;
}

void AnsiRenderer::flushSidebar(int score, int level, int lines, int nextPiece) {
    const auto pr = [&](int r, const std::string& s) {
        appendMoveTo(r, SIDE_COL);
        _buf += "\033[K";
        _buf += s;
    };

    if (!_borderDrawn) {
        pr(BOARD_ROW,      "┌─────────────┐");
        pr(BOARD_ROW +  1, "│    TETRIS   │");
        pr(BOARD_ROW +  2, "└─────────────┘");
        pr(BOARD_ROW +  4, "  SCORE");
        pr(BOARD_ROW +  7, "  LEVEL");
        pr(BOARD_ROW + 10, "  LINES");
        pr(BOARD_ROW + 13, "  NEXT");
        pr(BOARD_ROW + 20, "  CONTROLS");
        pr(BOARD_ROW + 21, "  ←→  move");
        pr(BOARD_ROW + 22, "  ↑   rotate");
        pr(BOARD_ROW + 23, "  ↓   soft drop");
        pr(BOARD_ROW + 24, "  SPC hard drop");
        pr(BOARD_ROW + 25, "  Q   quit");
    }

    if (score != _prevScore) { pr(BOARD_ROW + 5,  "  " + std::to_string(score) + "      "); _prevScore = score; }
    if (level != _prevLevel) { pr(BOARD_ROW + 8,  "  " + std::to_string(level));             _prevLevel = level; }
    if (lines != _prevLines) { pr(BOARD_ROW + 11, "  " + std::to_string(lines));             _prevLines = lines; }

    if (nextPiece != _prevNext) {
        for (int r = 0; r < 4; ++r) {
            appendMoveTo(BOARD_ROW + 15 + r, SIDE_COL);
            _buf += "        ";
        }
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

void AnsiRenderer::draw(const GameState& s) {
    _buf.clear();

    if (!_borderDrawn) {
        drawBorder();
        _borderDrawn = true;
    }

    Board back = s.board;
    const auto& shape = PIECES[s.curPiece][s.curRot];

    if (s.ghostY != s.curY)
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (shape[r][c]) {
                    const int br = s.ghostY + r, bc = s.curX + c;
                    if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W && !back[br][bc])
                        back[br][bc] = 8;
                }

    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (shape[r][c]) {
                const int br = s.curY + r, bc = s.curX + c;
                if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W)
                    back[br][bc] = s.curPiece + 1;
            }

    for (int r = 0; r < BOARD_H; ++r)
        for (int c = 0; c < BOARD_W; ++c)
            if (back[r][c] != _front[r][c]) {
                appendMoveTo(BOARD_ROW + r, BOARD_COL + c * 2);
                appendCell(back[r][c]);
                _front[r][c] = back[r][c];
            }

    flushSidebar(s.score, s.level, s.totalLines, s.nextPiece);

    if (!_buf.empty())
        writeAll(STDOUT_FILENO, _buf.data(), _buf.size());
}

void AnsiRenderer::drawGameOver(int score) {
    _buf.clear();
    appendMoveTo(BOARD_ROW + BOARD_H / 2 - 1, BOARD_COL - 1);
    _buf += "\033[41m\033[97m  GAME  OVER   \033[0m";
    appendMoveTo(BOARD_ROW + BOARD_H / 2,     BOARD_COL - 1);
    _buf += "\033[41m\033[97m  Score: " + std::to_string(score) + "     \033[0m";
    appendMoveTo(BOARD_ROW + BOARD_H / 2 + 1, BOARD_COL - 1);
    _buf += "\033[41m\033[97m  Press any key \033[0m";
    writeAll(STDOUT_FILENO, _buf.data(), _buf.size());
    _buf.clear();

    while (pollInput() == Action::None)
        std::this_thread::sleep_for(50ms);
}
