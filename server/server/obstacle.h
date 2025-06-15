#pragma once
#include "session.h"

class Obstacle {
public:
	int id;
	int x, y;
	int rock_num;
	bool can_see;

	Obstacle(int obs_id, int obs_x, int obs_y);
};

