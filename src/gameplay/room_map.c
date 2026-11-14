/*
 * room_map.c - original test layout: one room with a 2x2 pillar block and
 * two single pillars, used to check depth ordering and (later) collision.
 * '#' = wall, '.' = floor. Row index = cz, column index = cx.
 */
#include "gameplay/room_map.h"

static const char *const ROOM_ROWS[ROOM_D] = {
    "############",
    "#..........#",
    "#..........#",
    "#...##.....#",
    "#...##.....#",
    "#..........#",
    "#.......#..#",
    "#..........#",
    "#..........#",
    "#..#.......#",
    "#..........#",
    "############",
};

int room_is_solid(int cx, int cz)
{
    if (cx < 0 || cz < 0 || cx >= ROOM_W || cz >= ROOM_D)
        return 1;
    return ROOM_ROWS[cz][cx] == '#';
}

float room_cell_center(int c)
{
    return ((float)c + 0.5f) * ROOM_CELL_SIZE;
}
