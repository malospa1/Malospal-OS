# PCG basic C

Source: https://github.com/imneme/pcg-c-basic
Pinned commit: `bc39cd76ac3d541e618606bcc6e1e5ba5e5e6aa3`.
Copyright 2014 Melissa O'Neill. Apache-2.0; see `LICENSE.txt`.

Used for the slot mini-game's repeatable random symbols, without libc or a heap.
The source implementation is unchanged. The header includes `stdint.h` instead
of `inttypes.h` for a freestanding kernel; no formatting macros are used.
Only the per-instance API is used by Malospal OS. This is not a security RNG.
