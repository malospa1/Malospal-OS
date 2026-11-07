#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/games/slots_logic.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Slots failure %d: %s\n", __LINE__, #x); exit(1); } } while (0)

int main(void) {
    pcg32_random_t rng;
    pcg32_srandom_r(&rng, 42, 54);
    const uint32_t vector[] = {0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e};
    for (unsigned int i = 0; i < 6; ++i) { CHECK(pcg32_random_r(&rng) == vector[i]); }
    const uint32_t awards[] = {10, 20, 30, 50, 80, 150};
    unsigned int triples = 0, pairs = 0, misses = 0;
    for (uint8_t a = 0; a < 6; ++a) {
        for (uint8_t b = 0; b < 6; ++b) {
            for (uint8_t c = 0; c < 6; ++c) {
                const uint8_t reels[] = {a, b, c};
                const uint32_t expected = a == b && b == c ? awards[a] : a == b || a == c || b == c ? 5 : 0;
                CHECK(slots_points(reels) == expected);
                triples += expected >= 10; pairs += expected == 5; misses += expected == 0;
            }
        }
    }
    CHECK(triples == 6 && pairs == 90 && misses == 120);
    const uint8_t invalid[] = {255, 255, 255};
    CHECK(slots_points(invalid) == 0 && slots_triple_points(6) == 0);

    struct slots_game game;
    slots_initialize(&game, 42);
    CHECK(game.score == 0 && game.spins == 0 && !game.spinning && !slots_finish(&game));
    CHECK(slots_start(&game, 100) && game.spins == 1);
    struct slots_game snapshot = game;
    CHECK(!slots_start(&game, 200) && !memcmp(&snapshot, &game, sizeof(game)));
    CHECK(!slots_reel_stopped(&game, 0, 169) && slots_reel_stopped(&game, 0, 170));
    CHECK(!slots_reel_stopped(&game, 1, 209) && slots_reel_stopped(&game, 1, 210));
    CHECK(!slots_reel_stopped(&game, 2, 249) && slots_reel_stopped(&game, 2, 250));
    CHECK(!slots_update(&game, 249) && slots_update(&game, 250));
    CHECK(game.last == slots_points(snapshot.target) && game.score == game.last);
    CHECK(!memcmp(game.reels, snapshot.target, 3));
    const uint32_t saved = game.score;
    CHECK(!slots_finish(&game) && !slots_update(&game, 1000) && game.score == saved);

    CHECK(slots_start(&game, UINT64_MAX - 40));
    CHECK(!slots_update(&game, 108) && slots_update(&game, 109));
    game.score = SLOTS_SCORE_MAX - 1;
    game.spins = game.matches = SLOTS_SCORE_MAX;
    CHECK(slots_start(&game, 0));
    game.target[0] = game.target[1] = game.target[2] = 5;
    CHECK(slots_finish(&game) && game.last == 150 && game.score == SLOTS_SCORE_MAX);
    CHECK(game.spins == SLOTS_SCORE_MAX && game.matches == SLOTS_SCORE_MAX);

    slots_initialize(&game, 77);
    struct slots_game twin;
    slots_initialize(&twin, 77);
    for (unsigned int i = 0; i < 100000; ++i) {
        CHECK(slots_start(&game, i) && slots_start(&twin, i));
        CHECK(!memcmp(game.target, twin.target, 3));
        for (unsigned int reel = 0; reel < 3; ++reel) { CHECK(game.target[reel] < SLOTS_SYMBOLS); }
        const uint32_t before = game.score, points = slots_points(game.target);
        const pcg32_random_t random = game.random;
        CHECK(slots_finish(&game) && slots_update(&twin, (uint64_t)i + 150));
        CHECK(!memcmp(&random, &game.random, sizeof(random)));
        const uint32_t total = before + points > SLOTS_SCORE_MAX ? SLOTS_SCORE_MAX : before + points;
        CHECK(game.score == total && game.score == twin.score && !game.spinning);
    }
    slots_initialize(&game, 0);
    CHECK(game.score == 0 && game.matches == 0 && game.spins == 0 && slots_start(&game, 0));
    CHECK(slots_finish(&game));
    puts("PASS: PCG reference vector, all 216 combinations, staged stops, busy guard, one award, wrap/caps and 100000 spins.");
    return 0;
}
