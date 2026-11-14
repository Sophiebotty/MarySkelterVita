/*
 * room_map.h - small hard-coded test room on a square grid.
 *
 * World mapping: cell (cx, cz) covers x in [cx*ROOM_CELL_SIZE, (cx+1)*ROOM_CELL_SIZE]
 * and z in [cz*ROOM_CELL_SIZE, (cz+1)*ROOM_CELL_SIZE]. Floor at y = 0,
 * ceiling at y = ROOM_WALL_HEIGHT.
 */
#ifndef ROOM_MAP_H
#define ROOM_MAP_H

#define ROOM_W 12
#define ROOM_D 12

#define ROOM_CELL_SIZE   2.0f
#define ROOM_WALL_HEIGHT 2.5f

/* 1 if the cell is a wall (cells outside the map count as walls). */
int room_is_solid(int cx, int cz);

/* World-space centre of a cell. */
float room_cell_center(int c);

#endif /* ROOM_MAP_H */
