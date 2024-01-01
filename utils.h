#pragma once

#include <furi.h>

#include "game.h"

inline uint8_t cap_x(uint8_t coord) {
    return MIN(MAX(0, coord), (SIZE_X - 1));
}

inline uint8_t cap_y(uint8_t coord) {
    return MIN(MAX(0, coord), (SIZE_Y - 1));
}

inline bool is_state_pause(State gameState) {
    return ((gameState < SELECT_BRICK) || (gameState >= PAUSED));
}

inline void copy_level(PlayGround target, PlayGround source) {
    memcpy(target, source, sizeof(uint8_t) * SIZE_X * SIZE_Y);
}

inline void clear_board(PlayGround* ani) {
    memset(ani, '\0', sizeof(uint8_t) * SIZE_X * SIZE_Y);
}

inline void randomize_bg(BackGround* bg) {
    memset(bg, 0, sizeof(uint8_t) * SIZE_X_BG * SIZE_Y_BG);
    int r;
    uint8_t ra, x, y;
    for(y = 0; y < SIZE_Y_BG; y++) {
        for(x = 0; x < SIZE_X_BG; x++) {
            do {
                r = rand();
                ra = (r % 7) + 1;
                if(y > 0) {
                    if((*bg)[y - 1][x] == ra) {
                        ra = 0;
                    }
                }
                if(x > 0) {
                    if((*bg)[y][x - 1] == ra) {
                        ra = 0;
                    }
                }
            } while(ra == 0);
            (*bg)[y][x] = ra;
        }
    }
}

inline const char* game_mode_label(GameMode mode) {
    switch(mode) {
    case CONTINUE:
        return "Continue";
    case CUSTOM:
        return "Custom";
    default:
    case NEW_GAME:
        return "New Game";
    }
}
