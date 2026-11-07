# Snake logic origin

The game logic in `src/games/snake_logic.c` is adapted from the SDL Snake example:
https://github.com/libsdl-org/SDL/blob/96292a5b464258a2b926e0a3d72f8b98c2a81aa6/examples/demo/01-snake/snake.c

SDL release-3.2.20, commit `96292a5b464258a2b926e0a3d72f8b98c2a81aa6`.
`upstream.c` is the unmodified reference, not compiled or linked into Malospal OS.
The example explicitly places its code in the public domain:

> This code is public domain. Feel free to use it for any purpose!

Retained: directional cells, packed three-bit board, head/tail traversal,
wrapping at edges, reversal rejection and food growth.

Malospal OS changes: freestanding types and byte accesses; a padding byte for the last
packed cell; bounded food placement with seeded PRNG; one food and a complete
three-cell initial body; explicit loss/win results; growth on the eating step;
score and restart managed by the game frontend. SDL itself is not a dependency.
