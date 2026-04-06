// ANSI terminal renderer — double-buffered, write-on-change only
//
// Strategy:
//   - Maintain _front[r][c]: the color id currently visible on screen
//   - Each frame: build composite back buffer (board + ghost + piece)
//   - Diff back vs _front row-by-row; only write rows that changed
//   - Sidebar values tracked individually; only written when they change
//   - If nothing changed, return without any write at all
//   - All output batched into _buf, wrapped in BSU/ESU, flushed in one
//     write() syscall when a change is present

#include "ansi_renderer.h"
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

static constexpr std::string_view RESET  = "\033[0m";
static constexpr std::string_view BSU    = "\033[?2026h";
static constexpr std::string_view ESU    = "\033[?2026l";

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

    // -1 forces every cell to be written on the first frame
    for (auto& row : _front) row.fill(-1);
    _buf.reserve(8192);

    // Enter alternate screen, hide cursor, clear, go home — built as string
    // to avoid hardcoded byte-count bugs.
    std::string s;
    s += "\033[?1049h";  // alternate screen buffer
    s += "\033[?25l";    // hide cursor
    s += "\033[2J";      // clear screen
    s += "\033[H";       // cursor home
    writeAll(STDOUT_FILENO, s.data(), s.size());
}

void AnsiRenderer::shutdown() {
    std::string s;
    s += "\033[?25h";    // show cursor
    s += "\033[?1049l";  // restore normal screen buffer
    writeAll(STDOUT_FILENO, s.data(), s.size());
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
                    default: break;
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

// ── Static content (border + labels) — written once on first frame ────────────

void AnsiRenderer::drawStatic() {
    // Border
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

    // Sidebar labels
    const auto sl = [&](int r, std::string_view sv) {
        appendMoveTo(r, SIDE_COL);
        _buf += sv;
    };
    sl(BOARD_ROW,      "┌─────────────┐");
    sl(BOARD_ROW +  1, "│    TETRIS   │");
    sl(BOARD_ROW +  2, "└─────────────┘");
    sl(BOARD_ROW +  4, "  SCORE");
    sl(BOARD_ROW +  7, "  LEVEL");
    sl(BOARD_ROW + 10, "  LINES");
    sl(BOARD_ROW + 13, "  NEXT");
    sl(BOARD_ROW + 20, "  CONTROLS");
    sl(BOARD_ROW + 21, "  \xe2\x86\x90\xe2\x86\x92  move");
    sl(BOARD_ROW + 22, "  \xe2\x86\x91   rotate");
    sl(BOARD_ROW + 23, "  \xe2\x86\x93   soft drop");
    sl(BOARD_ROW + 24, "  SPC hard drop");
    sl(BOARD_ROW + 25, "  Q   quit");
}

// ── Main draw ─────────────────────────────────────────────────────────────────

void AnsiRenderer::draw(const GameState& s) {
    // ── Build composite back buffer ──────────────────────────────────────────
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

    // ── Start building output — BSU first ────────────────────────────────────
    _buf.clear();
    _buf += BSU;
    const std::size_t bsu_end = _buf.size();  // mark: content starts here

    // Static content on first frame only
    if (!_staticDrawn) {
        drawStatic();
        _staticDrawn = true;
    }

    // ── Diff: row-based scan ─────────────────────────────────────────────────
    // Skip rows with no changes; write the full row (with color-run
    // compression) for any row that has at least one changed cell.
    // This gives one moveTo per dirty row and strictly top→bottom cursor travel.
    for (int r = 0; r < BOARD_H; ++r) {
        bool dirty = false;
        for (int c = 0; c < BOARD_W && !dirty; ++c)
            dirty = (back[r][c] != _front[r][c]);
        if (!dirty) continue;

        appendMoveTo(BOARD_ROW + r, BOARD_COL);
        int cur = -1;
        for (int c = 0; c < BOARD_W; ++c) {
            const int id = back[r][c];
            if (id != cur) {
                _buf += (id == 0) ? RESET : std::string_view{pieceColor(id)};
                cur = id;
            }
            _buf += (id == 0) ? "  " : "██";
            _front[r][c] = id;
        }
        if (cur != 0) _buf += RESET;
    }

    // ── Sidebar — only changed values, no \033[K, fixed-width overwrite ──────
    // Fixed width avoids the erase-then-write flash that \033[K produces.
    const auto sval = [&](int row, const std::string& v, int width) {
        appendMoveTo(row, SIDE_COL);
        _buf += v;
        for (int i = static_cast<int>(v.size()); i < width; ++i) _buf += ' ';
    };

    if (s.score != _prevScore) {
        sval(BOARD_ROW + 5, "  " + std::to_string(s.score), 12);
        _prevScore = s.score;
    }
    if (s.level != _prevLevel) {
        sval(BOARD_ROW + 8, "  " + std::to_string(s.level), 6);
        _prevLevel = s.level;
    }
    if (s.totalLines != _prevLines) {
        sval(BOARD_ROW + 11, "  " + std::to_string(s.totalLines), 6);
        _prevLines = s.totalLines;
    }
    if (s.nextPiece != _prevNext) {
        for (int r = 0; r < 4; ++r) {
            appendMoveTo(BOARD_ROW + 15 + r, SIDE_COL);
            _buf += "        ";
        }
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (PIECES[s.nextPiece][0][r][c]) {
                    appendMoveTo(BOARD_ROW + 15 + r, SIDE_COL + c * 2);
                    _buf += pieceColor(s.nextPiece + 1);
                    _buf += "\xe2\x96\x88\xe2\x96\x88";  // ██
                    _buf += RESET;
                }
        _prevNext = s.nextPiece;
    }

    // ── Nothing changed — skip the write entirely ─────────────────────────────
    if (_buf.size() == bsu_end) {
        _buf.clear();
        return;
    }

    // Park cursor at top-left so next frame's cursor path is always top→bottom
    _buf += "\033[H";
    _buf += ESU;

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
    _buf += "\033[H";
    _buf += ESU;
    writeAll(STDOUT_FILENO, _buf.data(), _buf.size());
    _buf.clear();

    while (pollInput() == Action::None)
        std::this_thread::sleep_for(50ms);
}
