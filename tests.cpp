// tests.cpp — unit tests for Tetris game logic
// Build & run: make test

#include "test_runner.h"
#include "game.h"
#include "game_logic.h"

#include <algorithm>
#include <numeric>

// ── Helpers ───────────────────────────────────────────────────────────────────

static Board emptyBoard() { return {}; }

// Fill every cell of a row with color id 1 (a locked I piece color)
static void fillRow(Board& b, int row) {
    b[row].fill(1);
}

// Fill all cells of a row except one column
static void fillRowExcept(Board& b, int row, int skipCol) {
    b[row].fill(1);
    b[row][skipCol] = 0;
}

// ── fits() ────────────────────────────────────────────────────────────────────

TEST("fits: I-piece fits on empty board at spawn position") {
    const auto board = emptyBoard();
    // I-piece rot 0 occupies row 1 of its 4×4 bounding box: row 1, cols 0-3
    // With px=3, py=-1: board rows 0..-1+1=0, cols 3..6 → all in-bounds
    EXPECT_TRUE(fits(board, 0, 0, 3, -1));
}

TEST("fits: piece blocked by left wall") {
    const auto board = emptyBoard();
    // O-piece rot 0 uses cols 1-2 in its box; at px=-2 that puts it at cols -1,0
    EXPECT_FALSE(fits(board, 1, 0, -2, 0));
}

TEST("fits: piece blocked by right wall") {
    const auto board = emptyBoard();
    // O-piece at px=9 → cells at cols 10,11 → out of bounds
    EXPECT_FALSE(fits(board, 1, 0, 9, 0));
}

TEST("fits: piece blocked by floor") {
    const auto board = emptyBoard();
    // I-piece rot 0 has cells in row 1 of its box; at py=BOARD_H-1 that's row BOARD_H
    EXPECT_FALSE(fits(board, 0, 0, 3, BOARD_H - 1));
}

TEST("fits: piece blocked by locked cell") {
    auto board = emptyBoard();
    board[5][4] = 1;  // single locked cell
    // T-piece rot 0 at px=3, py=4: occupies (4,4),(5,3),(5,4),(5,5)
    EXPECT_FALSE(fits(board, 2, 0, 3, 4));
}

TEST("fits: piece fits above a locked cell") {
    auto board = emptyBoard();
    board[10][4] = 1;
    // T-piece one row above the locked cell should still fit
    EXPECT_TRUE(fits(board, 2, 0, 3, 3));
}

TEST("fits: piece partially above board (negative py) is allowed") {
    const auto board = emptyBoard();
    // Spawn row for most pieces starts at py=-1; cells above board are ignored
    EXPECT_TRUE(fits(board, 2, 0, 3, -1));
}

// ── lockPiece() ───────────────────────────────────────────────────────────────

TEST("lockPiece: sets correct color id in board") {
    auto board = emptyBoard();
    // O-piece (index 1) → color id 2; rot 0 cells at relative (0,1),(0,2),(1,1),(1,2)
    lockPiece(board, 1, 0, 0, 5);
    EXPECT_EQ(board[5][1], 2);
    EXPECT_EQ(board[5][2], 2);
    EXPECT_EQ(board[6][1], 2);
    EXPECT_EQ(board[6][2], 2);
}

TEST("lockPiece: cells above board are silently ignored") {
    auto board = emptyBoard();
    // Lock I-piece at py=-1 (part of it is above the board)
    lockPiece(board, 0, 0, 3, -1);
    // Row -1+1=0 is the first real row; row -1 should not crash or corrupt
    // I rot0 row0 is empty, row1 has cells → py+1 = 0 → lands on row 0
    EXPECT_EQ(board[0][3], 1);
    EXPECT_EQ(board[0][4], 1);
    EXPECT_EQ(board[0][5], 1);
    EXPECT_EQ(board[0][6], 1);
}

TEST("lockPiece: does not overwrite other cells") {
    auto board = emptyBoard();
    board[5][0] = 3;  // pre-existing locked cell elsewhere
    lockPiece(board, 1, 0, 4, 5);
    EXPECT_EQ(board[5][0], 3);  // untouched
}

// ── clearLines() ──────────────────────────────────────────────────────────────

TEST("clearLines: no full lines returns 0") {
    auto board = emptyBoard();
    fillRowExcept(board, BOARD_H - 1, 5);
    EXPECT_EQ(clearLines(board), 0);
}

TEST("clearLines: one full line at bottom returns 1") {
    auto board = emptyBoard();
    fillRow(board, BOARD_H - 1);
    EXPECT_EQ(clearLines(board), 1);
}

TEST("clearLines: cleared row is replaced by empty row") {
    auto board = emptyBoard();
    fillRow(board, BOARD_H - 1);
    board[BOARD_H - 2][0] = 2;  // marker in the row above
    (void)clearLines(board);
    // After clearing, old row BOARD_H-2 shifts down to BOARD_H-1
    EXPECT_EQ(board[BOARD_H - 1][0], 2);
    // Top row should now be empty
    for (int c = 0; c < BOARD_W; ++c)
        EXPECT_EQ(board[0][c], 0);
}

TEST("clearLines: two consecutive full lines returns 2") {
    auto board = emptyBoard();
    fillRow(board, BOARD_H - 1);
    fillRow(board, BOARD_H - 2);
    EXPECT_EQ(clearLines(board), 2);
}

TEST("clearLines: four full lines (Tetris) returns 4") {
    auto board = emptyBoard();
    for (int r = BOARD_H - 4; r < BOARD_H; ++r) fillRow(board, r);
    EXPECT_EQ(clearLines(board), 4);
}

TEST("clearLines: non-adjacent full lines both clear") {
    auto board = emptyBoard();
    fillRow(board, BOARD_H - 1);
    fillRow(board, BOARD_H - 3);
    EXPECT_EQ(clearLines(board), 2);
}

TEST("clearLines: partial line not cleared") {
    auto board = emptyBoard();
    fillRowExcept(board, BOARD_H - 1, 0);
    const int before = board[BOARD_H - 1][1];
    (void)clearLines(board);
    EXPECT_EQ(board[BOARD_H - 1][1], before);  // row survived
}

// ── ghostRow() ────────────────────────────────────────────────────────────────

TEST("ghostRow: on empty board I-piece drops to floor") {
    const auto board = emptyBoard();
    // I rot0 row1 has cells; at py=0 the lowest cell is at row 1
    // It can fall until row 1 + drops = BOARD_H-1 → py = BOARD_H - 2
    const int gy = ghostRow(board, 0, 0, 3, 0);
    EXPECT_EQ(gy, BOARD_H - 2);
}

TEST("ghostRow: piece resting on locked cell") {
    auto board = emptyBoard();
    fillRow(board, BOARD_H - 1);  // full bottom row (not yet cleared)
    // I rot0 at py=0: cells in row 1 of box; drops until row above filled row
    const int gy = ghostRow(board, 0, 0, 3, 0);
    EXPECT_EQ(gy, BOARD_H - 3);  // two rows above the locked row
}

TEST("ghostRow: equals curY when already at rest") {
    auto board = emptyBoard();
    fillRow(board, BOARD_H - 1);
    // Place piece so it's already sitting on the locked row
    const int py = BOARD_H - 3;
    EXPECT_EQ(ghostRow(board, 0, 0, 3, py), py);
}

TEST("ghostRow: O-piece drops to floor on empty board") {
    const auto board = emptyBoard();
    // O rot0 cells in rows 0,1 of box; lowest cell = py+1
    // Drops until py+1 = BOARD_H-1 → py = BOARD_H-2
    EXPECT_EQ(ghostRow(board, 1, 0, 0, 0), BOARD_H - 2);
}

// ── lineScore() ───────────────────────────────────────────────────────────────

TEST("lineScore: 0 lines is always 0") {
    EXPECT_EQ(lineScore(0, 0), 0);
    EXPECT_EQ(lineScore(0, 9), 0);
}

TEST("lineScore: single line level 0") {
    EXPECT_EQ(lineScore(1, 0), 100);
}

TEST("lineScore: double line level 0") {
    EXPECT_EQ(lineScore(2, 0), 300);
}

TEST("lineScore: triple line level 0") {
    EXPECT_EQ(lineScore(3, 0), 500);
}

TEST("lineScore: Tetris (4 lines) level 0") {
    EXPECT_EQ(lineScore(4, 0), 800);
}

TEST("lineScore: scales with level") {
    EXPECT_EQ(lineScore(1, 1), 200);   // 100 * (1+1)
    EXPECT_EQ(lineScore(4, 4), 4000);  // 800 * (4+1)
}

// ── tryRotate() ───────────────────────────────────────────────────────────────

TEST("tryRotate: T-piece basic rotation on empty board") {
    auto board = emptyBoard();
    int rot = 0, px = 4, py = 5;
    EXPECT_TRUE(tryRotate(board, 2, rot, px, py));
    EXPECT_EQ(rot, 1);
}

TEST("tryRotate: four rotations return to original") {
    auto board = emptyBoard();
    int rot = 0, px = 4, py = 5;
    for (int i = 0; i < 4; ++i) tryRotate(board, 2, rot, px, py);
    EXPECT_EQ(rot, 0);
}

TEST("tryRotate: I-piece rotates on empty board") {
    auto board = emptyBoard();
    int rot = 0, px = 3, py = 5;
    EXPECT_TRUE(tryRotate(board, 0, rot, px, py));
    EXPECT_EQ(rot, 1);
}

TEST("tryRotate: rotation blocked by walls and floor returns false") {
    auto board = emptyBoard();
    // Completely fill the board so no kick can succeed
    for (auto& row : board) row.fill(1);
    int rot = 0, px = 3, py = 5;
    EXPECT_FALSE(tryRotate(board, 2, rot, px, py));
    EXPECT_EQ(rot, 0);  // unchanged
}

TEST("tryRotate: wall kick allows rotation near right edge") {
    auto board = emptyBoard();
    // Place I-piece flush against the right wall (px=7, rot=0)
    // Normal rotation would push it out of bounds; SRS kick should fix it
    int rot = 0, px = 7, py = 5;
    const bool ok = tryRotate(board, 0, rot, px, py);
    EXPECT_TRUE(ok);
    // After kick the piece must still be in-bounds
    EXPECT_TRUE(fits(board, 0, rot, px, py));
}

TEST("tryRotate: S-piece rotates correctly") {
    auto board = emptyBoard();
    int rot = 0, px = 3, py = 5;
    EXPECT_TRUE(tryRotate(board, 3, rot, px, py));
    EXPECT_EQ(rot, 1);
}

// ── PIECES table sanity ───────────────────────────────────────────────────────

TEST("PIECES: each piece has exactly 4 rotations") {
    EXPECT_EQ(static_cast<int>(PIECES.size()), 7);
    for (const auto& p : PIECES)
        EXPECT_EQ(static_cast<int>(p.size()), 4);
}

TEST("PIECES: I-piece has 4 filled cells in every rotation") {
    for (const auto& rot : PIECES[0]) {
        int count = 0;
        for (const auto& row : rot)
            for (int cell : row)
                count += cell;
        EXPECT_EQ(count, 4);
    }
}

TEST("PIECES: O-piece is symmetric across all 4 rotations") {
    // All 4 rotations of O must be identical
    for (int r = 1; r < 4; ++r)
        EXPECT_TRUE(PIECES[1][r] == PIECES[1][0]);
}

TEST("PIECES: every piece has exactly 4 filled cells per rotation") {
    for (int p = 0; p < 7; ++p)
        for (int r = 0; r < 4; ++r) {
            int count = 0;
            for (const auto& row : PIECES[p][r])
                for (int cell : row)
                    count += cell;
            EXPECT_EQ(count, 4);
        }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\nTetris unit tests\n"
              << "─────────────────\n";
    return testing::run_tests();
}
