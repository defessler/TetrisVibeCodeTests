// ANSI terminal renderer
// Writes the full board every frame inside a single BSU…ESU block,
// delivered via one write() syscall. No partial-state risk.

#include "ansi_renderer.h"
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

static constexpr std::string_view RESET  = "\033[0m";
static constexpr std::string_view BSU    = "\033[?2026h";  // Begin Sync Update
static constexpr std::string_view ESU    = "\033[?2026l";  // End   Sync Update
static constexpr std::string_view ALTSCR = "\033[?1049h";  // enter alternate screen
static constexpr std::string_view NORMSCR= "\033[?1049l";  // leave alternate screen
static constexpr std::string_view HIDE   = "\033[?25l";    // hide cursor
static constexpr std::string_view SHOW   = "\033[?25h";    // show cursor
static constexpr std::string_view CLR    = "\033[2J";      // clear screen
static constexpr std::string_view HOME   = "\033[H";       // cursor home

// ── Helpers ───────────────────────────────────────────────────────────────────

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

    _buf.reserve(8192);

    // Enter alternate screen, hide cursor, clear, go home
    std::string init;
    init += ALTSCR;
    init += HIDE;
    init += CLR;
    init += HOME;
    writeAll(STDOUT_FILENO, init.data(), init.size());
}

void AnsiRenderer::shutdown() {
    std::string quit;
    quit += SHOW;
    quit += NORMSCR;
    writeAll(STDOUT_FILENO, quit.data(), quit.size());
    tcsetattr(STDIN_FILENO, TCSANOW, &_saved);
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
        if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[')
            if (read(STDIN_FILENO, &seq[1], 1) == 1)
                switch (seq[1]) {
                    case 'A': return Action::Rotate;
                    case 'B': return Action::SoftDrop;
                    case 'C': return Action::Right;
                    case 'D': return Action::Left;
                    default:  break;
                }
    }
    return Action::None;
}

// ── Buffer helpers ────────────────────────────────────────────────────────────

void AnsiRenderer::appendMoveTo(int row, int col) {
    _buf += "\033[";
    _buf += std::to_string(row);
    _buf += ';';
    _buf += std::to_string(col);
    _buf += 'H';
}

// Append a fixed-width field: value string left-aligned, padded to `width`
// with spaces. Avoids \033[K (erase-to-EOL) which causes a visible blank flash.
static void appendPadded(std::string& buf, std::string_view val, int width) {
    buf += val;
    for (int i = static_cast<int>(val.size()); i < width; ++i) buf += ' ';
}

// ── Static frame (border + sidebar labels) — written once ────────────────────

void AnsiRenderer::drawStatic() {
    // Border
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

    // Sidebar labels
    const auto sl = [&](int r, std::string_view s) {
        appendMoveTo(r, SIDE_COL);
        _buf += s;
    };
    sl(BOARD_ROW,     "┌─────────────┐");
    sl(BOARD_ROW + 1, "│    TETRIS   │");
    sl(BOARD_ROW + 2, "└─────────────┘");
    sl(BOARD_ROW + 4,  "  SCORE");
    sl(BOARD_ROW + 7,  "  LEVEL");
    sl(BOARD_ROW + 10, "  LINES");
    sl(BOARD_ROW + 13, "  NEXT");
    sl(BOARD_ROW + 20, "  CONTROLS");
    sl(BOARD_ROW + 21, "  ←→  move");
    sl(BOARD_ROW + 22, "  ↑   rotate");
    sl(BOARD_ROW + 23, "  ↓   soft drop");
    sl(BOARD_ROW + 24, "  SPC hard drop");
    sl(BOARD_ROW + 25, "  Q   quit");
}

// ── Main draw ─────────────────────────────────────────────────────────────────

void AnsiRenderer::draw(const GameState& s) {
    _buf.clear();
    _buf += BSU;  // begin synchronized update — terminal holds paint until ESU

    // Static content (border + labels) on first frame only
    if (!_staticDrawn) {
        drawStatic();
        _staticDrawn = true;
    }

    // ── Build composite board ────────────────────────────────────────────────
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

    // ── Write every board row ────────────────────────────────────────────────
    // Full board every frame: no stale-state risk, cursor moves only forward.
    // Color-run compression avoids redundant escape codes within a row.
    for (int r = 0; r < BOARD_H; ++r) {
        appendMoveTo(BOARD_ROW + r, BOARD_COL);
        int cur = -1;
        for (int c = 0; c < BOARD_W; ++c) {
            const int id = back[r][c];
            if (id != cur) {
                _buf += (id == 0) ? RESET : std::string_view{pieceColor(id)};
                cur = id;
            }
            _buf += (id == 0) ? "  " : "██";
        }
        if (cur != 0) _buf += RESET;
    }

    // ── Sidebar dynamic values ───────────────────────────────────────────────
    // Written every frame at fixed width — no \033[K needed, no blank flash.
    appendMoveTo(BOARD_ROW + 5,  SIDE_COL);
    appendPadded(_buf, "  " + std::to_string(s.score), 12);

    appendMoveTo(BOARD_ROW + 8,  SIDE_COL);
    appendPadded(_buf, "  " + std::to_string(s.level), 6);

    appendMoveTo(BOARD_ROW + 11, SIDE_COL);
    appendPadded(_buf, "  " + std::to_string(s.totalLines), 6);

    // Next-piece preview — only redraw when it changes
    if (s.nextPiece != _prevNext) {
        for (int r = 0; r < 4; ++r) {
            appendMoveTo(BOARD_ROW + 15 + r, SIDE_COL);
            _buf += "        ";  // clear 4 cells (8 chars)
        }
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (PIECES[s.nextPiece][0][r][c]) {
                    appendMoveTo(BOARD_ROW + 15 + r, SIDE_COL + c * 2);
                    _buf += pieceColor(s.nextPiece + 1);
                    _buf += "██";
                    _buf += RESET;
                }
        _prevNext = s.nextPiece;
    }

    // Park cursor at top-left — next frame's writes are always top→bottom
    _buf += HOME;
    _buf += ESU;  // end synchronized update — terminal paints the complete frame

    writeAll(STDOUT_FILENO, _buf.data(), _buf.size());
}

// ── Game over ─────────────────────────────────────────────────────────────────

void AnsiRenderer::drawGameOver(int score) {
    _buf.clear();
    _buf += BSU;
    appendMoveTo(BOARD_ROW + BOARD_H / 2 - 1, BOARD_COL - 1);
    _buf += "\033[41m\033[97m  GAME  OVER   \033[0m";
    appendMoveTo(BOARD_ROW + BOARD_H / 2,     BOARD_COL - 1);
    _buf += "\033[41m\033[97m  Score: ";
    _buf += std::to_string(score);
    _buf += "        \033[0m";
    appendMoveTo(BOARD_ROW + BOARD_H / 2 + 1, BOARD_COL - 1);
    _buf += "\033[41m\033[97m  Press any key \033[0m";
    _buf += ESU;
    writeAll(STDOUT_FILENO, _buf.data(), _buf.size());
    _buf.clear();

    while (pollInput() == Action::None)
        std::this_thread::sleep_for(50ms);
}
