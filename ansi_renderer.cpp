// ANSI terminal renderer — double-buffered implementation

#include "ansi_renderer.h"
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

static constexpr std::string_view RESET = "\033[0m";

// Write all bytes to fd, retrying on partial writes.
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

    writeAll(STDOUT_FILENO,
        "\033[?1049h"   // switch to alternate screen buffer (dedicated surface)
        "\033[?25l"     // hide cursor
        "\033[2J"       // clear alternate screen
        "\033[H",       // cursor to top-left
        20);
}

void AnsiRenderer::shutdown() {
    tcsetattr(STDIN_FILENO, TCSANOW, &_saved);
    writeAll(STDOUT_FILENO,
        "\033[?25h"    // show cursor
        "\033[?1049l"  // restore normal screen buffer
        , 12);
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

    // ── Diff: row-based scan ─────────────────────────────────────────────────
    // For each row that has at least one changed cell, write the whole row with
    // a single moveTo then sequential cell output — no cursor jumping within a
    // row, and far fewer escape sequences than one moveTo-per-cell.
    for (int r = 0; r < BOARD_H; ++r) {
        // Quick dirty-check: skip rows with no changes
        bool dirty = false;
        for (int c = 0; c < BOARD_W && !dirty; ++c)
            dirty = (back[r][c] != _front[r][c]);
        if (!dirty) continue;

        appendMoveTo(BOARD_ROW + r, BOARD_COL);

        // Color-run compression: only emit a new color escape when the color
        // actually changes between adjacent cells.
        int curColor = -1;
        for (int c = 0; c < BOARD_W; ++c) {
            const int id = back[r][c];
            if (id != curColor) {
                _buf += (id == 0) ? RESET : std::string_view{pieceColor(id)};
                curColor = id;
            }
            _buf += (id == 0) ? "  " : "██";
            _front[r][c] = id;
        }
        if (curColor != 0) _buf += RESET;  // leave terminal in default color
    }

    flushSidebar(s.score, s.level, s.totalLines, s.nextPiece);

    // Park cursor at (1,1) — above all game content — so the NEXT frame's
    // writes always travel strictly top-to-bottom with no backward jumps.
    // A backward cursor movement triggers an immediate repaint on some
    // terminals even inside a BSU/ESU block.
    _buf += "\033[1;1H";

    if (_buf == "\033[1;1H") return;  // only the park code — nothing changed

    // Synchronized output: tell the terminal to hold rendering until ESU.
    // Supported by kitty, iTerm2, VTE (GNOME Terminal, Tilix), foot, etc.
    // Terminals that don't support it ignore the sequences harmlessly.
    static constexpr std::string_view BSU = "\033[?2026h";  // Begin Sync Update
    static constexpr std::string_view ESU = "\033[?2026l";  // End   Sync Update

    // Single writeAll — atomic delivery to PTY, avoids stdio buffering layers
    const std::string frame = std::string(BSU) + _buf + std::string(ESU);
    writeAll(STDOUT_FILENO, frame.data(), frame.size());
}

// ── Game over ─────────────────────────────────────────────────────────────────

void AnsiRenderer::drawGameOver(int score) {
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
