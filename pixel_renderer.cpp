// pixel_renderer.cpp — vivid arcade pixel-art terminal renderer
//
// Board: 3 cols × 1 row per cell with ▌█▐ bevel blocks and true colour.
// Sidebar: rainbow "TETRIS" logo, score/level/lines, next-piece preview.
// Only dirty cells are written each frame (diff from front buffer).

#include "pixel_renderer.h"
#include <cstdio>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

// ── I/O helper ────────────────────────────────────────────────────────────────

static void writeAll(int fd, const char* p, std::size_t n) {
    while (n > 0) {
        ssize_t r = write(fd, p, n);
        if (r <= 0) return;
        p += r;
        n -= static_cast<std::size_t>(r);
    }
}

// ── Colour palette ────────────────────────────────────────────────────────────

struct RGB { uint8_t r, g, b; };

struct Theme {
    RGB hi;   // ▌ foreground  – left-edge highlight
    RGB mid;  // ▌ background + █ foreground + ▐ background  – main body
    RGB lo;   // ▐ foreground  – right-edge shadow
};

// cell id → visual theme  (0 = empty, 1-7 = tetrominoes, 8 = ghost handled separately)
static constexpr Theme THEMES[8] = {
    {{ 14, 16, 24}, {  9, 11, 17}, {  6,  7, 12}},  // 0 empty  (near-black grid)
    {{150,255,255}, {  0,200,220}, {  0,110,145}},   // 1 I  electric cyan
    {{255,245, 80}, {230,165,  0}, {150,100,  0}},   // 2 O  golden yellow
    {{240, 90,255}, {185, 15,230}, {110,  0,160}},   // 3 T  vivid violet
    {{ 95,255,115}, { 10,195, 50}, {  0,120, 25}},   // 4 S  lime green
    {{255, 90, 75}, {220, 10, 10}, {145,  0,  0}},   // 5 Z  hot red
    {{ 80,135,255}, { 10, 50,220}, {  0, 10,145}},   // 6 J  royal blue
    {{255,195, 55}, {220,110,  0}, {145, 60,  0}},   // 7 L  warm amber
};

// "TETRIS" rainbow – one piece colour per letter
static constexpr RGB TITLE_COLS[6] = {
    {185, 15,230},  // T – T violet
    { 10,195, 50},  // E – S lime
    {  0,200,220},  // T – I cyan
    {220,110,  0},  // R – L amber
    {220, 10, 10},  // I – Z red
    { 10, 50,220},  // S – J blue
};

static constexpr RGB BORDER  = { 50, 140, 220};  // box-drawing lines
static constexpr RGB LABEL   = {140, 160, 200};  // sidebar label text
static constexpr RGB GHOST_C = { 55,  65,  95};  // ghost cell outline
static constexpr RGB EMPTY_C = { 22,  26,  40};  // empty cell dot

// ── Escape-sequence helpers ───────────────────────────────────────────────────

static void aFg(std::string& b, RGB c) {
    char t[24];
    snprintf(t, sizeof(t), "\033[38;2;%u;%u;%um", c.r, c.g, c.b);
    b += t;
}
static void aBg(std::string& b, RGB c) {
    char t[24];
    snprintf(t, sizeof(t), "\033[48;2;%u;%u;%um", c.r, c.g, c.b);
    b += t;
}

// Repeat a UTF-8 string n times
static std::string rep(const char* s, int n) {
    std::string r;
    for (int i = 0; i < n; ++i) r += s;
    return r;
}

// ── PixelRenderer — core ──────────────────────────────────────────────────────

void PixelRenderer::init() {
    tcgetattr(STDIN_FILENO, &_saved);
    termios raw = _saved;
    raw.c_lflag &= ~static_cast<unsigned>(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    for (auto& row : _front) row.fill(-1);
    _buf.reserve(16384);

    std::string s;
    s += "\033[?25l";  // hide cursor
    s += "\033[2J";    // clear screen
    writeAll(STDOUT_FILENO, s.data(), s.size());
}

void PixelRenderer::shutdown() {
    std::string s;
    s += "\033[?25h";
    s += "\033[2J";
    s += "\033[H";
    writeAll(STDOUT_FILENO, s.data(), s.size());
    tcsetattr(STDIN_FILENO, TCSANOW, &_saved);
}

Action PixelRenderer::pollInput() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv{0, 0};
    if (select(1, &fds, nullptr, nullptr, &tv) <= 0) return Action::None;

    unsigned char c = 0;
    if (read(STDIN_FILENO, &c, 1) != 1) return Action::None;

    if (c == 'q' || c == 'Q' || c == 27) return Action::Quit;
    if (c == ' ')              return Action::HardDrop;
    if (c == 'a' || c == 'A') return Action::Left;
    if (c == 'd' || c == 'D') return Action::Right;
    if (c == 's' || c == 'S') return Action::SoftDrop;
    if (c == 'w' || c == 'W') return Action::Rotate;

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

// ── Cell rendering ────────────────────────────────────────────────────────────

void PixelRenderer::moveTo(int row, int col) {
    _buf += "\033[";
    _buf += std::to_string(row);
    _buf += ';';
    _buf += std::to_string(col);
    _buf += 'H';
}

void PixelRenderer::appendCell(int id) {
    const RGB& ebg = THEMES[0].mid;   // empty-cell background

    if (id == 0) {
        // Empty: dark background with a subtle middle-dot grid marker
        aFg(_buf, EMPTY_C);
        aBg(_buf, ebg);
        _buf += " \xc2\xb7 \033[0m";   // " · "   U+00B7 MIDDLE DOT
        return;
    }

    if (id == 8) {
        // Ghost: hollow outline — ▌ space ▐ in dim grey on empty background
        aFg(_buf, GHOST_C);
        aBg(_buf, ebg);
        _buf += "\xe2\x96\x8c \xe2\x96\x90\033[0m";   // ▌ · ▐
        return;
    }

    const Theme& t = THEMES[id];
    //  ▌  left-edge highlight:  fg=hi, bg=mid  → left half bright, right half main
    aFg(_buf, t.hi);  aBg(_buf, t.mid);  _buf += "\xe2\x96\x8c";  // ▌
    //  █  solid main body:      fg=mid          (bg irrelevant behind full block)
    aFg(_buf, t.mid);                    _buf += "\xe2\x96\x88";  // █
    //  ▐  right-edge shadow:    fg=lo, bg=mid  → left half main, right half dark
    aFg(_buf, t.lo);  aBg(_buf, t.mid);  _buf += "\xe2\x96\x90";  // ▐
    _buf += "\033[0m";
}

// ── Static elements (drawn once) ──────────────────────────────────────────────

void PixelRenderer::drawStatic() {
    // ── Board border (double-line box) ────────────────────────────────────────
    aFg(_buf, BORDER);

    moveTo(BOARD_ROW - 1, BOARD_COL - 1);
    _buf += "\xe2\x95\x94";                               // ╔
    _buf += rep("\xe2\x95\x90", BOARD_W * CELL_W);        // ═ × 30
    _buf += "\xe2\x95\x97";                               // ╗

    for (int r = 0; r < BOARD_H; ++r) {
        moveTo(BOARD_ROW + r, BOARD_COL - 1);
        _buf += "\xe2\x95\x91";                           // ║
        moveTo(BOARD_ROW + r, BOARD_COL + BOARD_W * CELL_W);
        _buf += "\xe2\x95\x91";                           // ║
    }

    moveTo(BOARD_ROW + BOARD_H, BOARD_COL - 1);
    _buf += "\xe2\x95\x9a";                               // ╚
    _buf += rep("\xe2\x95\x90", BOARD_W * CELL_W);        // ═ × 30
    _buf += "\xe2\x95\x9d";                               // ╝
    _buf += "\033[0m";

    // ── Sidebar: "TETRIS" rainbow title box ──────────────────────────────────
    //
    //   ╔═══════════════╗   17 wide (╔ + 15×═ + ╗)
    //   ║  T E T R I S  ║   ║ + 2sp + T·E·T·R·I·S + 2sp + ║ = 17
    //   ╚═══════════════╝
    //
    aFg(_buf, BORDER);
    moveTo(BOARD_ROW - 1, SIDE_COL);
    _buf += "\xe2\x95\x94" + rep("\xe2\x95\x90", 15) + "\xe2\x95\x97";  // ╔═════════════════╗

    moveTo(BOARD_ROW, SIDE_COL);
    aFg(_buf, BORDER);
    _buf += "\xe2\x95\x91  ";   // ║ + 2 spaces
    static constexpr char LETTERS[] = "TETRIS";
    for (int i = 0; i < 6; ++i) {
        if (i > 0) _buf += ' ';
        aFg(_buf, TITLE_COLS[i]);
        _buf += "\033[1m";
        _buf += LETTERS[i];
        _buf += "\033[22m";     // bold off, colour stays
    }
    aFg(_buf, BORDER);
    _buf += "  \xe2\x95\x91";   // 2 spaces + ║

    moveTo(BOARD_ROW + 1, SIDE_COL);
    aFg(_buf, BORDER);
    _buf += "\xe2\x95\x9a" + rep("\xe2\x95\x90", 15) + "\xe2\x95\x9d";  // ╚═════════════════╝
    _buf += "\033[0m";

    // ── Sidebar static labels ─────────────────────────────────────────────────
    const auto lbl = [&](int row, std::string_view text) {
        moveTo(row, SIDE_COL);
        aFg(_buf, LABEL);
        _buf += text;
        _buf += "\033[0m";
    };

    lbl(BOARD_ROW +  3, "\xe2\x97\x86 SCORE");              // ◆ SCORE
    lbl(BOARD_ROW +  6, "\xe2\x97\x86 LEVEL");              // ◆ LEVEL
    lbl(BOARD_ROW +  9, "\xe2\x97\x86 LINES");              // ◆ LINES
    lbl(BOARD_ROW + 12, "\xe2\x97\x86 NEXT");               // ◆ NEXT
    lbl(BOARD_ROW + 18,
        "\xe2\x86\x90\xe2\x86\x92  move    "
        "\xe2\x86\x91  rotate");                            // ←→  move  ↑  rotate
    lbl(BOARD_ROW + 19,
        "\xe2\x86\x93  soft drop  "
        "SPC  hard drop");                                  // ↓ soft   SPC hard
    lbl(BOARD_ROW + 20, "Q / ESC  quit");
}

// ── Dynamic sidebar values ────────────────────────────────────────────────────

void PixelRenderer::flushSidebar(int score, int level, int lines, int next) {
    const auto val = [&](int row, int v) {
        moveTo(row, SIDE_COL + 2);
        _buf += "\033[1m";
        aFg(_buf, {255, 255, 255});
        _buf += std::to_string(v);
        _buf += "\033[K\033[0m";
    };

    if (score != _prevScore) { val(BOARD_ROW +  4, score); _prevScore = score; }
    if (level != _prevLevel) { val(BOARD_ROW +  7, level); _prevLevel = level; }
    if (lines != _prevLines) { val(BOARD_ROW + 10, lines); _prevLines = lines; }

    if (next != _prevNext) {
        // Clear preview area
        for (int r = 0; r < 4; ++r) {
            moveTo(BOARD_ROW + 13 + r, SIDE_COL);
            _buf += "\033[K";
        }
        // Render next piece at rotation 0
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (PIECES[next][0][r][c]) {
                    moveTo(BOARD_ROW + 13 + r, SIDE_COL + c * CELL_W);
                    appendCell(next + 1);
                }
        _prevNext = next;
    }
}

// ── Per-frame draw ────────────────────────────────────────────────────────────

void PixelRenderer::draw(const GameState& s) {
    _buf.clear();

    if (!_staticDrawn) {
        drawStatic();
        _staticDrawn = true;
    }

    // Build back-buffer: locked cells + ghost overlay + active piece overlay
    Board back = s.board;
    const auto& shape = PIECES[s.curPiece][s.curRot];

    if (s.ghostY != s.curY)
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                if (shape[r][c]) {
                    int br = s.ghostY + r, bc = s.curX + c;
                    if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W && !back[br][bc])
                        back[br][bc] = 8;
                }

    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (shape[r][c]) {
                int br = s.curY + r, bc = s.curX + c;
                if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W)
                    back[br][bc] = s.curPiece + 1;
            }

    // Diff render: only write cells that changed
    for (int r = 0; r < BOARD_H; ++r)
        for (int c = 0; c < BOARD_W; ++c)
            if (back[r][c] != _front[r][c]) {
                moveTo(BOARD_ROW + r, BOARD_COL + c * CELL_W);
                appendCell(back[r][c]);
                _front[r][c] = back[r][c];
            }

    flushSidebar(s.score, s.level, s.totalLines, s.nextPiece);

    if (!_buf.empty())
        writeAll(STDOUT_FILENO, _buf.data(), _buf.size());
}

// ── Game-over overlay ─────────────────────────────────────────────────────────

void PixelRenderer::drawGameOver(int score) {
    _buf.clear();

    // Centred overlay box spanning the full board inner width (30 cols)
    const int innerW = BOARD_W * CELL_W;   // 30
    const int boxRow = BOARD_ROW + BOARD_H / 2 - 4;
    const int boxCol = BOARD_COL;

    // Write one row of the overlay
    auto goLine = [&](int r, std::string_view content) {
        moveTo(boxRow + r, boxCol);
        _buf += "\033[38;2;255;255;255m\033[48;2;150;0;0m";
        _buf += content;
        _buf += "\033[K\033[0m";
    };

    // Horizontal rule: left-corner + (innerW-2) × ═ + right-corner
    auto hbar = [&](const char* lc, const char* rc) {
        std::string s = lc;
        s += rep("\xe2\x95\x90", innerW - 2);   // ═
        s += rc;
        return s;
    };

    // Content row: ║ + message centred in innerW-2 chars + ║
    auto crow = [&](std::string_view msg) {
        int n   = static_cast<int>(msg.size());   // ASCII only, so bytes == visual width
        int pad = (innerW - 2 - n) / 2;
        std::string s = "\xe2\x95\x91";           // ║
        for (int i = 0; i < pad; ++i) s += ' ';
        s += msg;
        for (int i = 0; i < (innerW - 2 - n - pad); ++i) s += ' ';
        s += "\xe2\x95\x91";                      // ║
        return s;
    };

    char sc[32];
    snprintf(sc, sizeof(sc), "Score: %d", score);

    goLine(0, hbar("\xe2\x95\x94", "\xe2\x95\x97"));  // ╔═══╗
    goLine(1, crow(""));
    goLine(2, crow("G A M E   O V E R"));
    goLine(3, crow(""));
    goLine(4, hbar("\xe2\x95\xa0", "\xe2\x95\xa3"));  // ╠═══╣
    goLine(5, crow(sc));
    goLine(6, hbar("\xe2\x95\xa0", "\xe2\x95\xa3"));  // ╠═══╣
    goLine(7, crow("Press any key"));
    goLine(8, hbar("\xe2\x95\x9a", "\xe2\x95\x9d"));  // ╚═══╝

    writeAll(STDOUT_FILENO, _buf.data(), _buf.size());
    _buf.clear();

    while (pollInput() == Action::None)
        std::this_thread::sleep_for(50ms);
}
