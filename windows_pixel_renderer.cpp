// windows_pixel_renderer.cpp — pixel-art renderer for Windows Console
//
// Visual design is identical to pixel_renderer.cpp (Linux):
//   ▌█▐  half-block bevel with 24-bit true-colour ANSI codes.
// I/O layer uses Windows Console API + WriteFile (goes through the VT
// processor when ENABLE_VIRTUAL_TERMINAL_PROCESSING is set).

#include "windows_pixel_renderer.h"
#include <cstdio>

// ── Colour palette (shared with Linux build, duplicated here to avoid
//    cross-platform header entanglement) ────────────────────────────────────

struct RGB { unsigned char r, g, b; };

struct Theme {
    RGB hi;   // ▌ fg  – left-edge highlight
    RGB mid;  // ▌ bg + █ fg + ▐ bg  – main body
    RGB lo;   // ▐ fg  – right-edge shadow
};

static constexpr Theme THEMES[8] = {
    {{ 14, 16, 24}, {  9, 11, 17}, {  6,  7, 12}},  // 0 empty
    {{150,255,255}, {  0,200,220}, {  0,110,145}},   // 1 I  cyan
    {{255,245, 80}, {230,165,  0}, {150,100,  0}},   // 2 O  gold
    {{240, 90,255}, {185, 15,230}, {110,  0,160}},   // 3 T  violet
    {{ 95,255,115}, { 10,195, 50}, {  0,120, 25}},   // 4 S  lime
    {{255, 90, 75}, {220, 10, 10}, {145,  0,  0}},   // 5 Z  red
    {{ 80,135,255}, { 10, 50,220}, {  0, 10,145}},   // 6 J  cobalt
    {{255,195, 55}, {220,110,  0}, {145, 60,  0}},   // 7 L  amber
};

static constexpr RGB TITLE_COLS[6] = {
    {185, 15,230}, { 10,195, 50}, {  0,200,220},
    {220,110,  0}, {220, 10, 10}, { 10, 50,220},
};

static constexpr RGB BORDER  = { 50, 140, 220};
static constexpr RGB LABEL   = {140, 160, 200};
static constexpr RGB GHOST_C = { 55,  65,  95};
static constexpr RGB EMPTY_C = { 22,  26,  40};

static void aFg(std::string& b, RGB c) {
    char t[24]; snprintf(t, sizeof(t), "\033[38;2;%u;%u;%um", c.r, c.g, c.b); b += t;
}
static void aBg(std::string& b, RGB c) {
    char t[24]; snprintf(t, sizeof(t), "\033[48;2;%u;%u;%um", c.r, c.g, c.b); b += t;
}
static std::string rep(const char* s, int n) {
    std::string r; for (int i = 0; i < n; ++i) r += s; return r;
}

// ── I/O ───────────────────────────────────────────────────────────────────────

void WindowsPixelRenderer::flush() {
    if (_buf.empty()) return;
    DWORD written = 0;
    WriteFile(_hOut, _buf.data(), static_cast<DWORD>(_buf.size()), &written, nullptr);
    _buf.clear();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void WindowsPixelRenderer::init() {
    _hIn  = GetStdHandle(STD_INPUT_HANDLE);
    _hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    GetConsoleMode(_hIn,  &_savedInMode);
    GetConsoleMode(_hOut, &_savedOutMode);

    SetConsoleMode(_hIn, 0);   // raw input
    SetConsoleMode(_hOut,
        _savedOutMode
        | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        | DISABLE_NEWLINE_AUTO_RETURN);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    for (auto& row : _front) row.fill(-1);
    _buf.reserve(16384);

    _buf += "\033[?25l";   // hide cursor
    _buf += "\033[2J";     // clear screen
    flush();
}

void WindowsPixelRenderer::shutdown() {
    _buf += "\033[?25h\033[2J\033[H";
    flush();
    SetConsoleMode(_hIn,  _savedInMode);
    SetConsoleMode(_hOut, _savedOutMode);
}

// ── Input ─────────────────────────────────────────────────────────────────────

Action WindowsPixelRenderer::pollInput() {
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

    if (ch == 'q' || ch == 'Q' || vk == VK_ESCAPE) return Action::Quit;
    if (ch == ' '  || vk == VK_SPACE)               return Action::HardDrop;
    if (ch == 'a'  || ch == 'A' || vk == VK_LEFT)   return Action::Left;
    if (ch == 'd'  || ch == 'D' || vk == VK_RIGHT)  return Action::Right;
    if (ch == 's'  || ch == 'S' || vk == VK_DOWN)   return Action::SoftDrop;
    if (ch == 'w'  || ch == 'W' || vk == VK_UP)     return Action::Rotate;

    return Action::None;
}

// ── Cell rendering ────────────────────────────────────────────────────────────

void WindowsPixelRenderer::moveTo(int row, int col) {
    _buf += "\033[";
    _buf += std::to_string(row);
    _buf += ';';
    _buf += std::to_string(col);
    _buf += 'H';
}

void WindowsPixelRenderer::appendCell(int id) {
    const RGB& ebg = THEMES[0].mid;

    if (id == 0) {
        aFg(_buf, EMPTY_C); aBg(_buf, ebg);
        _buf += " \xc2\xb7 \033[0m";
        return;
    }
    if (id == 8) {
        aFg(_buf, GHOST_C); aBg(_buf, ebg);
        _buf += "\xe2\x96\x8c \xe2\x96\x90\033[0m";
        return;
    }

    const Theme& t = THEMES[id];
    aFg(_buf, t.hi);  aBg(_buf, t.mid); _buf += "\xe2\x96\x8c";  // ▌
    aFg(_buf, t.mid);                   _buf += "\xe2\x96\x88";  // █
    aFg(_buf, t.lo);  aBg(_buf, t.mid); _buf += "\xe2\x96\x90";  // ▐
    _buf += "\033[0m";
}

// ── Static elements ───────────────────────────────────────────────────────────

void WindowsPixelRenderer::drawStatic() {
    aFg(_buf, BORDER);

    moveTo(BOARD_ROW - 1, BOARD_COL - 1);
    _buf += "\xe2\x95\x94" + rep("\xe2\x95\x90", BOARD_W * CELL_W) + "\xe2\x95\x97";

    for (int r = 0; r < BOARD_H; ++r) {
        moveTo(BOARD_ROW + r, BOARD_COL - 1);
        _buf += "\xe2\x95\x91";
        moveTo(BOARD_ROW + r, BOARD_COL + BOARD_W * CELL_W);
        _buf += "\xe2\x95\x91";
    }

    moveTo(BOARD_ROW + BOARD_H, BOARD_COL - 1);
    _buf += "\xe2\x95\x9a" + rep("\xe2\x95\x90", BOARD_W * CELL_W) + "\xe2\x95\x9d";
    _buf += "\033[0m";

    // "TETRIS" rainbow title box (17 wide: ╔ + 15×═ + ╗)
    aFg(_buf, BORDER);
    moveTo(BOARD_ROW - 1, SIDE_COL);
    _buf += "\xe2\x95\x94" + rep("\xe2\x95\x90", 15) + "\xe2\x95\x97";

    moveTo(BOARD_ROW, SIDE_COL);
    aFg(_buf, BORDER); _buf += "\xe2\x95\x91  ";
    static constexpr char LETTERS[] = "TETRIS";
    for (int i = 0; i < 6; ++i) {
        if (i > 0) _buf += ' ';
        aFg(_buf, TITLE_COLS[i]);
        _buf += "\033[1m"; _buf += LETTERS[i]; _buf += "\033[22m";
    }
    aFg(_buf, BORDER); _buf += "  \xe2\x95\x91";

    moveTo(BOARD_ROW + 1, SIDE_COL);
    aFg(_buf, BORDER);
    _buf += "\xe2\x95\x9a" + rep("\xe2\x95\x90", 15) + "\xe2\x95\x9d";
    _buf += "\033[0m";

    const auto lbl = [&](int row, const char* text) {
        moveTo(row, SIDE_COL);
        aFg(_buf, LABEL); _buf += text; _buf += "\033[0m";
    };

    lbl(BOARD_ROW +  3, "\xe2\x97\x86 SCORE");
    lbl(BOARD_ROW +  6, "\xe2\x97\x86 LEVEL");
    lbl(BOARD_ROW +  9, "\xe2\x97\x86 LINES");
    lbl(BOARD_ROW + 12, "\xe2\x97\x86 NEXT");
    lbl(BOARD_ROW + 18,
        "\xe2\x86\x90\xe2\x86\x92  move    "
        "\xe2\x86\x91  rotate");
    lbl(BOARD_ROW + 19,
        "\xe2\x86\x93  soft drop  "
        "SPC  hard drop");
    lbl(BOARD_ROW + 20, "Q / ESC  quit");
}

// ── Dynamic sidebar ───────────────────────────────────────────────────────────

void WindowsPixelRenderer::flushSidebar(int score, int level, int lines, int next) {
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
        for (int r = 0; r < 4; ++r) {
            moveTo(BOARD_ROW + 13 + r, SIDE_COL);
            _buf += "\033[K";
        }
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

void WindowsPixelRenderer::draw(const GameState& s) {
    _buf.clear();

    if (!_staticDrawn) { drawStatic(); _staticDrawn = true; }

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

    for (int r = 0; r < BOARD_H; ++r)
        for (int c = 0; c < BOARD_W; ++c)
            if (back[r][c] != _front[r][c]) {
                moveTo(BOARD_ROW + r, BOARD_COL + c * CELL_W);
                appendCell(back[r][c]);
                _front[r][c] = back[r][c];
            }

    flushSidebar(s.score, s.level, s.totalLines, s.nextPiece);
    flush();
}

// ── Game-over overlay ─────────────────────────────────────────────────────────

void WindowsPixelRenderer::drawGameOver(int score) {
    const int innerW = BOARD_W * CELL_W;
    const int boxRow = BOARD_ROW + BOARD_H / 2 - 4;
    const int boxCol = BOARD_COL;

    auto goLine = [&](int r, const std::string& content) {
        moveTo(boxRow + r, boxCol);
        _buf += "\033[38;2;255;255;255m\033[48;2;150;0;0m";
        _buf += content;
        _buf += "\033[K\033[0m";
    };

    auto hbar = [&](const char* lc, const char* rc) {
        return std::string(lc) + rep("\xe2\x95\x90", innerW - 2) + rc;
    };

    auto crow = [&](const char* msg) {
        int n   = static_cast<int>(strlen(msg));
        int pad = (innerW - 2 - n) / 2;
        std::string s = "\xe2\x95\x91";
        for (int i = 0; i < pad; ++i) s += ' ';
        s += msg;
        for (int i = 0; i < (innerW - 2 - n - pad); ++i) s += ' ';
        s += "\xe2\x95\x91";
        return s;
    };

    char sc[32];
    snprintf(sc, sizeof(sc), "Score: %d", score);

    goLine(0, hbar("\xe2\x95\x94", "\xe2\x95\x97"));
    goLine(1, crow(""));
    goLine(2, crow("G A M E   O V E R"));
    goLine(3, crow(""));
    goLine(4, hbar("\xe2\x95\xa0", "\xe2\x95\xa3"));
    goLine(5, crow(sc));
    goLine(6, hbar("\xe2\x95\xa0", "\xe2\x95\xa3"));
    goLine(7, crow("Press any key"));
    goLine(8, hbar("\xe2\x95\x9a", "\xe2\x95\x9d"));
    flush();

    while (pollInput() == Action::None)
        Sleep(50);
}
