#include "slots_logic.h"

uint32_t slots_triple_points(unsigned int symbol) {
    static const uint32_t points[SLOTS_SYMBOLS] = {10, 20, 30, 50, 80, 150};
    return symbol < SLOTS_SYMBOLS ? points[symbol] : 0;
}

uint32_t slots_points(const uint8_t reels[SLOTS_REELS]) {
    for (unsigned int i = 0; i < SLOTS_REELS; ++i) {
        if (reels[i] >= SLOTS_SYMBOLS) { return 0; }
    }
    if (reels[0] == reels[1] && reels[1] == reels[2]) { return slots_triple_points(reels[0]); }
    return reels[0] == reels[1] || reels[1] == reels[2] || reels[0] == reels[2] ? 5 : 0;
}

void slots_initialize(struct slots_game *game, uint64_t seed) {
    *game = (struct slots_game){.reels = {0, 1, 3}};
    pcg32_srandom_r(&game->random, seed, 54);
}

bool slots_start(struct slots_game *game, uint64_t now) {
    if (game->spinning) { return false; }
    for (unsigned int i = 0; i < SLOTS_REELS; ++i) {
        game->target[i] = (uint8_t)pcg32_boundedrand_r(&game->random, SLOTS_SYMBOLS);
    }
    game->spinning = true;
    game->started = now;
    game->last = 0;
    if (game->spins < SLOTS_SCORE_MAX) { ++game->spins; }
    return true;
}

bool slots_finish(struct slots_game *game) {
    if (!game->spinning) { return false; }
    for (unsigned int i = 0; i < SLOTS_REELS; ++i) { game->reels[i] = game->target[i]; }
    game->last = slots_points(game->reels);
    game->score += game->last;
    if (game->score > SLOTS_SCORE_MAX) { game->score = SLOTS_SCORE_MAX; }
    if (game->last && game->matches < SLOTS_SCORE_MAX) { ++game->matches; }
    game->spinning = false;
    return true;
}

bool slots_update(struct slots_game *game, uint64_t now) {
    return game->spinning && now - game->started >= SLOTS_SPIN_TICKS && slots_finish(game);
}

bool slots_reel_stopped(const struct slots_game *game, unsigned int reel, uint64_t now) {
    return !game->spinning || reel >= SLOTS_REELS || now - game->started >= 70 + reel * 40;
}
