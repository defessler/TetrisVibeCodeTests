// game.cpp — Tetromino definitions, board logic, bag, SRS kicks

#include "game.h"
#include <algorithm>
#include <array>
#include <random>
#include <ranges>
#include <vector>

// ── Piece definitions ─────────────────────────────────────────────────────────

const std::array<Shape, 7> PIECES = {{
    // I
    {{
        {{{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}}},
        {{{0,0,1,0},{0,0,1,0},{0,0,1,0},{0,0,1,0}}},
        {{{0,0,0,0},{0,0,0,0},{1,1,1,1},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,0,0},{0,1,0,0},{0,1,0,0}}},
    }},
    // O
    {{
        {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
    }},
    // T
    {{
        {{{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}}},
        {{{0,0,0,0},{1,1,1,0},{0,1,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    }},
    // S
    {{
        {{{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,1,0},{0,0,1,0},{0,0,0,0}}},
        {{{0,0,0,0},{0,1,1,0},{1,1,0,0},{0,0,0,0}}},
        {{{1,0,0,0},{1,1,0,0},{0,1,0,0},{0,0,0,0}}},
    }},
    // Z
    {{
        {{{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,0,1,0},{0,1,1,0},{0,1,0,0},{0,0,0,0}}},
        {{{0,0,0,0},{1,1,0,0},{0,1,1,0},{0,0,0,0}}},
        {{{0,1,0,0},{1,1,0,0},{1,0,0,0},{0,0,0,0}}},
    }},
    // J
    {{
        {{{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,1,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}},
        {{{0,0,0,0},{1,1,1,0},{0,0,1,0},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,0,0},{1,1,0,0},{0,0,0,0}}},
    }},
    // L
    {{
        {{{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}},
        {{{0,1,0,0},{0,1,0,0},{0,1,1,0},{0,0,0,0}}},
        {{{0,0,0,0},{1,1,1,0},{1,0,0,0},{0,0,0,0}}},
        {{{1,1,0,0},{0,1,0,0},{0,1,0,0},{0,0,0,0}}},
    }},
}};

// ── Collision ─────────────────────────────────────────────────────────────────

bool fits(const Board& board, int piece, int rot, int px, int py) noexcept {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (PIECES[piece][rot][r][c]) {
                int br = py + r, bc = px + c;
                if (br >= BOARD_H || bc < 0 || bc >= BOARD_W) return false;
                if (br >= 0 && board[br][bc]) return false;
            }
    return true;
}

void lockPiece(Board& board, int piece, int rot, int px, int py) noexcept {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (PIECES[piece][rot][r][c]) {
                int br = py + r, bc = px + c;
                if (br >= 0 && br < BOARD_H && bc >= 0 && bc < BOARD_W)
                    board[br][bc] = piece + 1;
            }
}

int clearLines(Board& board) noexcept {
    int cleared = 0;
    for (int r = BOARD_H - 1; r >= 0; ) {
        const bool full = std::ranges::all_of(board[r], [](int cell){ return cell != 0; });
        if (full) {
            ++cleared;
            for (int rr = r; rr > 0; --rr) board[rr] = board[rr - 1];
            board[0] = {};
        } else {
            --r;
        }
    }
    return cleared;
}

int ghostRow(const Board& board, int piece, int rot, int px, int py) noexcept {
    int gy = py;
    while (fits(board, piece, rot, px, gy + 1)) ++gy;
    return gy;
}

int lineScore(int lines, int level) noexcept {
    static constexpr std::array<int, 5> base{0, 100, 300, 500, 800};
    return base[lines] * (level + 1);
}

// ── 7-bag randomizer ──────────────────────────────────────────────────────────

static std::mt19937 rng{std::random_device{}()};
static std::vector<int> bag;

int nextBag() {
    if (bag.empty()) {
        bag = {0,1,2,3,4,5,6};
        std::ranges::shuffle(bag, rng);
    }
    int v = bag.back(); bag.pop_back();
    return v;
}

// ── SRS wall kicks ────────────────────────────────────────────────────────────

using KickOffset = std::array<int, 2>;
using KickTable  = std::array<std::array<KickOffset, 5>, 4>;

static constexpr KickTable KICKS{{
    {{ {0,0},{-1,0},{-1,1},{0,-2},{-1,-2} }},
    {{ {0,0},{ 1,0},{ 1,-1},{0,2},{ 1,2} }},
    {{ {0,0},{ 1,0},{ 1,1},{0,-2},{ 1,-2} }},
    {{ {0,0},{-1,0},{-1,-1},{0,2},{-1,2} }},
}};

static constexpr KickTable KICKS_I{{
    {{ {0,0},{-2,0},{ 1,0},{-2,-1},{ 1,2} }},
    {{ {0,0},{-1,0},{ 2,0},{-1,2},{ 2,-1} }},
    {{ {0,0},{ 2,0},{-1,0},{ 2,1},{-1,-2} }},
    {{ {0,0},{ 1,0},{-2,0},{ 1,-2},{-2,1} }},
}};

bool tryRotate(const Board& board, int piece, int& rot, int& px, int& py) noexcept {
    const int newRot = (rot + 1) % 4;
    const KickTable& table = (piece == 0) ? KICKS_I : KICKS;
    for (const auto& [dx, dy] : table[rot]) {
        int nx = px + dx, ny = py - dy;
        if (fits(board, piece, newRot, nx, ny)) {
            rot = newRot; px = nx; py = ny;
            return true;
        }
    }
    return false;
}
