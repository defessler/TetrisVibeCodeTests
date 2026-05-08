#pragma once
// Abstract renderer interface for Tetris.
// Implement this to support any graphics backend (ANSI, SDL2, ncurses, …).

#include "game.h"

class Renderer {
public:
    virtual ~Renderer() = default;

    // Called once at startup
    virtual void init() = 0;

    // Called once at shutdown
    virtual void shutdown() = 0;

    // Draw the entire game state.  Called every frame.
    virtual void draw(const GameState& state) = 0;

    // Show the game-over screen; block until the user acknowledges.
    virtual void drawGameOver(int score) = 0;

    // Poll for the next user action.  Returns Action::None if nothing pending.
    virtual Action pollInput() = 0;
};
