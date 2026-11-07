#ifndef MALOSPAL_SLOTS_LOGIC_H
#define MALOSPAL_SLOTS_LOGIC_H

#include <stdbool.h>
#include <stdint.h>
#include "../../vendor/pcg/pcg_basic.h"

enum { SLOTS_SYMBOLS = 6, SLOTS_REELS = 3, SLOTS_SCORE_MAX = 999999, SLOTS_SPIN_TICKS = 150 };
struct slots_game {
    pcg32_random_t random;
    uint8_t reels[SLOTS_REELS], target[SLOTS_REELS];
    uint32_t score, spins, matches, last;
    uint64_t started;
    bool spinning;
};

uint32_t slots_points(const uint8_t reels[SLOTS_REELS]);
uint32_t slots_triple_points(unsigned int symbol);
void slots_initialize(struct slots_game *game, uint64_t seed);
bool slots_start(struct slots_game *game, uint64_t now);
bool slots_finish(struct slots_game *game);
bool slots_update(struct slots_game *game, uint64_t now);
bool slots_reel_stopped(const struct slots_game *game, unsigned int reel, uint64_t now);

#endif
