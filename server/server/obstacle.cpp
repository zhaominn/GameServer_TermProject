#include "pch.h"
#include "obstacle.h"

Obstacle::Obstacle(int obs_id, int obs_x, int obs_y) {
	id = obs_id;
	x = obs_x;
	y = obs_y;
	rock_num = rand() % 3;
	can_see = false;
}