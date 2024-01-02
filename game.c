#include "game.h"
#include "utils.h"
#include "move.h"

Game* alloc_game_state(int* error) {
    *error = 0;
    Game* game = malloc(sizeof(Game));

    game->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if(!game->mutex) {
        FURI_LOG_E(TAG, "cannot create mutex\r\n");
        free(game);
        *error = 255;
        return NULL;
    }

    game->levelData = alloc_level_data();
    game->levelSet = alloc_level_set();
    game->stats = alloc_stats();

    game->currentLevel = 0; //4   16  21??? 22  32
    game->gameMoves = 0;

    game->undoMovable = MOVABLE_NOT_FOUND;
    game->currentMovable = MOVABLE_NOT_FOUND;
    game->nextMovable = MOVABLE_NOT_FOUND;
    game->menuPausedPos = 0;

    game->mainMenuBtn = MODE_BTN;
    game->mainMenuMode = NEW_GAME;
    game->mainMenuInfo = false;
    game->hasContinue = false;
    game->selectedSet = furi_string_alloc_set(assetLevels[0]);
    game->selectedLevel = 0;
    game->continueSet = furi_string_alloc_set(assetLevels[0]);
    game->continueLevel = 0;
    game->setPos = 0;
    game->setCount = 1;

    game->state = INTRO;
    game->gameOverReason = NOT_GAME_OVER;

    game->move.frameNo = 0;

    return game;
}

//-----------------------------------------------------------------------------

void free_game_state(Game* game) {
    view_port_free(game->viewPort);
    furi_mutex_free(game->mutex);
    free_level_data(game->levelData);
    free_level_set(game->levelSet);
    free_stats(game->stats);
    furi_string_free(game->selectedSet);
    furi_string_free(game->continueSet);
    free(game);
}

//-----------------------------------------------------------------------------

GameOver is_game_over(PlayGround* mv, Stats* stats) {
    uint8_t sumMov = 0;
    uint8_t sum = 0;
    uint8_t x, y;
    for(uint8_t i = 0; i < WALL_TILE; i++) {
        sum += stats->ofBrick[i];
    }
    for(y = 0; y < SIZE_Y; y++) {
        for(x = 0; x < SIZE_X; x++) {
            sumMov += (*mv)[y][x];
        }
    }
    if((sum > 0) && (sumMov == 0)) {
        return CANNOT_MOVE;
    }
    for(uint8_t i = 0; i < WALL_TILE; i++) {
        if(stats->ofBrick[i] == 1) return BRICKS_LEFT;
    }
    return NOT_GAME_OVER;
}

//-----------------------------------------------------------------------------

bool is_level_finished(Stats* stats) {
    uint8_t sum = 0;
    for(uint8_t i = 0; i < WALL_TILE; i++) {
        sum += stats->ofBrick[i];
    }
    return (sum == 0);
}

//-----------------------------------------------------------------------------

Neighbors find_neighbors(PlayGround* pg, uint8_t x, uint8_t y) {
    Neighbors ne;

    ne.u = (y > 0) ? (*pg)[y - 1][x] : EMPTY_TILE;
    ne.l = (x > 0) ? (*pg)[y][x - 1] : EMPTY_TILE;

    ne.d = (y < SIZE_Y - 1) ? (*pg)[y + 1][x] : EMPTY_TILE;
    ne.r = (x < SIZE_X - 1) ? (*pg)[y][x + 1] : EMPTY_TILE;

    ne.dl = ((y < SIZE_Y - 1) && (x > 0)) ? (*pg)[y + 1][x - 1] : EMPTY_TILE;
    ne.ur = ((y > 0) && (x < SIZE_X - 1)) ? (*pg)[y - 1][x + 1] : EMPTY_TILE;

    ne.ul = ((y > 0) && (x > 0)) ? (*pg)[y - 1][x - 1] : EMPTY_TILE;
    ne.dr = ((x < SIZE_X - 1) && (y < SIZE_Y - 1)) ? (*pg)[y + 1][x + 1] : EMPTY_TILE;

    return ne;
}

//-----------------------------------------------------------------------------

void index_set(Game* game) {
    const char* findSetId = furi_string_get_cstr(game->levelSet->id);
    game->setCount = ASSETS_LEVELS_COUNT;
    game->setPos = 0;
    for(uint8_t i = 0; i < ASSETS_LEVELS_COUNT; i++) {
        if(strcmp(findSetId, assetLevels[i]) == 0) {
            game->setPos = i;
            return;
        }
    }
}

//-----------------------------------------------------------------------------

void initial_load_game(Game* game) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    game->hasContinue = load_last_level(game->continueSet, &game->continueLevel);

    if(game->hasContinue) {
        furi_string_set(game->selectedSet, game->continueSet);
        game->selectedLevel = game->continueLevel + 1;
        game->mainMenuMode = CONTINUE;
    } else {
        furi_string_set(game->selectedSet, assetLevels[0]);
        game->selectedLevel = 0;
        game->mainMenuMode = NEW_GAME;
    }

    load_level_set(storage, game->selectedSet, game->levelSet);
    furi_record_close(RECORD_STORAGE);
    index_set(game);

    if(game->selectedLevel > game->levelSet->maxLevel - 1) {
        game->selectedLevel = game->levelSet->maxLevel - 1;
    }

    randomize_bg(&game->bg);
}

//-----------------------------------------------------------------------------

void load_gameset_if_needed(Game* game, FuriString* expectedSet) {
    if(furi_string_cmp(expectedSet, game->levelSet->id) != 0) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        load_level_set(storage, expectedSet, game->levelSet);
        furi_record_close(RECORD_STORAGE);
    }
    index_set(game);
}

//-----------------------------------------------------------------------------

void start_game_at_level(Game* game, uint8_t levelNo) {
    if(levelNo < game->levelSet->maxLevel) {
        game->currentLevel = levelNo;
        refresh_level(game);
    } else {
        game->mainMenuBtn = LEVELSET_BTN;
        game->mainMenuMode = CUSTOM;

        game->setPos = (game->setPos < game->setCount - 1) ? game->setPos + 1 : 0;
        furi_string_set(game->selectedSet, assetLevels[game->setPos]);
        load_gameset_if_needed(game, game->selectedSet);
        game->selectedLevel = 0;

        game->state = MAIN_MENU;
    }
}

//-----------------------------------------------------------------------------

void refresh_level(Game* g) {
    clear_board(&g->board);
    clear_board(&g->boardUndo);
    clear_board(&g->toAnimate);

    furi_string_set(g->selectedSet, g->levelSet->id);
    furi_string_set(g->continueSet, g->levelSet->id);

    g->selectedLevel = g->currentLevel;

    // Open storage
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(load_level(storage, g->levelSet->id, g->currentLevel, g->levelData)) {
        parse_level_notation(furi_string_get_cstr(g->levelData->board), &g->board);
    }
    // Close storage
    furi_record_close(RECORD_STORAGE);

    map_movability(&g->board, &g->movables);
    update_board_stats(&g->board, g->stats);
    g->currentMovable = find_movable(&g->movables);
    g->undoMovable = MOVABLE_NOT_FOUND;
    g->gameMoves = 0;
    g->state = SELECT_BRICK;
}

//-----------------------------------------------------------------------------

void level_finished(Game* g) {
    g->hasContinue = true;
    furi_string_set(g->selectedSet, g->levelSet->id);
    furi_string_set(g->continueSet, g->levelSet->id);
    g->continueLevel = g->currentLevel;
    save_last_level(g->levelSet->id, g->currentLevel);
}

//-----------------------------------------------------------------------------

void forget_continue(Game* game) {
    game->hasContinue = false;
    furi_string_set(game->selectedSet, assetLevels[0]);
    furi_string_set(game->continueSet, assetLevels[0]);
    game->selectedLevel = 0;
    game->continueLevel = 0;
    delete_progress();
}

//-----------------------------------------------------------------------------

void click_selected(Game* game) {
    const uint8_t dir = movable_dir(&game->movables, game->currentMovable);
    switch(dir) {
    case MOVABLE_LEFT:
    case MOVABLE_RIGHT:
        start_move(game, dir);
        break;
    case MOVABLE_BOTH:
        game->state = SELECT_DIRECTION;
        break;
    default:
        break;
    }
}

//-----------------------------------------------------------------------------

void start_gravity(Game* g) {
    uint8_t x, y;
    bool change = false;

    clear_board(&g->toAnimate);

    // go through it bottom to top so as all the blocks tumble down on top of each other
    for(y = (SIZE_Y - 2); y > 0; y--) {
        for(x = (SIZE_X - 1); x > 0; x--) {
            if((is_block(g->board[y][x])) && (g->board[y + 1][x] == EMPTY_TILE)) {
                change = true;
                g->toAnimate[y][x] = 1;
            }
        }
    }

    if(change) {
        g->move.frameNo = 0;
        g->move.delay = 5;
        g->state = MOVE_GRAVITY;
    } else {
        g->state = SELECT_BRICK;
        start_explosion(g);
    }
}

//-----------------------------------------------------------------------------

void stop_gravity(Game* g) {
    uint8_t x, y;
    for(y = 0; y < SIZE_Y - 1; y++) {
        for(x = 0; x < SIZE_X; x++) {
            if(g->toAnimate[y][x] == 1) {
                g->board[y + 1][x] = g->board[y][x];
                g->board[y][x] = EMPTY_TILE;
            }
        }
    }

    start_gravity(g);
}

//-----------------------------------------------------------------------------

void start_explosion(Game* g) {
    uint8_t x, y;
    bool change = false;

    clear_board(&g->toAnimate);

    // go through it bottom to top so as all the blocks tumble down on top of each other
    for(y = 0; y < SIZE_Y; y++) {
        for(x = 0; x < SIZE_X; x++) {
            if(is_block(g->board[y][x])) {
                if(((y > 0) && (g->board[y][x] == g->board[y - 1][x])) ||
                   ((x > 0) && (g->board[y][x] == g->board[y][x - 1])) ||
                   ((y < SIZE_Y - 1) && (g->board[y][x] == g->board[y + 1][x])) ||
                   ((x < SIZE_X - 1) && (g->board[y][x] == g->board[y][x + 1]))) {
                    change = true;
                    g->toAnimate[y][x] = 1;
                }
            }
        }
    }

    if(change) {
        g->move.frameNo = 0;
        g->move.delay = 12;
        g->state = EXPLODE;
    } else {
        g->state = SELECT_BRICK;
        movement_stoped(g);
    }
}

//-----------------------------------------------------------------------------

void stop_explosion(Game* g) {
    uint8_t x, y;
    for(y = 0; y < SIZE_Y - 1; y++) {
        for(x = 0; x < SIZE_X; x++) {
            if(g->toAnimate[y][x] == 1) {
                g->board[y][x] = EMPTY_TILE;
            }
        }
    }

    start_gravity(g);
}

//-----------------------------------------------------------------------------

void start_move(Game* g, uint8_t direction) {
    g->undoMovable = g->currentMovable;
    copy_level(g->boardUndo, g->board);
    g->gameMoves++;
    g->move.dir = direction;
    g->move.x = coord_x(g->currentMovable);
    g->move.y = coord_y(g->currentMovable);
    g->move.frameNo = 0;
    g->nextMovable = coord_from((g->move.x + ((direction == MOVABLE_LEFT) ? -1 : 1)), g->move.y);
    g->state = MOVE_SIDES;
}

//-----------------------------------------------------------------------------

void stop_move(Game* g) {
    uint8_t deltaX = ((g->move.dir & MOVABLE_LEFT) != 0) ? -1 : 1;
    uint8_t tile = g->board[g->move.y][g->move.x];

    g->board[g->move.y][g->move.x] = EMPTY_TILE;
    g->board[g->move.y][cap_x(g->move.x + deltaX)] = tile;

    start_gravity(g);
}

//-----------------------------------------------------------------------------

void movement_stoped(Game* g) {
    map_movability(&g->board, &g->movables);
    update_board_stats(&g->board, g->stats);
    g->currentMovable = g->nextMovable;
    g->nextMovable = MOVABLE_NOT_FOUND;
    if(!is_block(g->board[coord_y(g->currentMovable)][coord_x(g->currentMovable)])) {
        find_movable_down(&g->movables, &g->currentMovable);
    }
    if(!is_block(g->board[coord_y(g->currentMovable)][coord_x(g->currentMovable)])) {
        find_movable_right(&g->movables, &g->currentMovable);
    }
    if(!is_block(g->board[coord_y(g->currentMovable)][coord_x(g->currentMovable)])) {
        g->currentMovable = MOVABLE_NOT_FOUND;
    }

    g->gameOverReason = is_game_over(&g->movables, g->stats);

    if(g->gameOverReason > NOT_GAME_OVER) {
        g->state = GAME_OVER;
    } else if(is_level_finished(g->stats)) {
        g->state = LEVEL_FINISHED;
        level_finished(g);
    } else {
        g->state = SELECT_BRICK;
    }
}

//-----------------------------------------------------------------------------

bool undo(Game* g) {
    if(g->undoMovable != MOVABLE_NOT_FOUND) {
        g->currentMovable = g->undoMovable;
        g->undoMovable = MOVABLE_NOT_FOUND;
        copy_level(g->board, g->boardUndo);
        map_movability(&g->board, &g->movables);
        update_board_stats(&g->board, g->stats);
        g->gameMoves--;
        g->state = SELECT_BRICK;
        return true;
    } else {
        g->state = SELECT_BRICK;
        return false;
    }
}