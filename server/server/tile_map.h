#pragma once
#include"obstacle.h"
class TILE_MAP {
public:
	bool state;
};

extern TILE_MAP tile_map[MAP_WIDTH][MAP_HEIGHT];

std::pair<int, int> astar_next_move(int sx, int sy, int tx, int ty);