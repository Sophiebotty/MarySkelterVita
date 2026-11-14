#!/bin/bash
# Host-side (PC) test of the collision code against the real room map.
# Run from WSL. Needs a native C toolchain with libc headers:
#   sudo apt install build-essential
# Not part of the Vita build.
set -e
cd "$(dirname "$0")/../.."
gcc -std=gnu99 -Wall -Wextra -O2 -I src \
    tests/host/test_collision.c src/gameplay/collision.c src/gameplay/room_map.c \
    -lm -o /tmp/test_collision
/tmp/test_collision
