#pragma once
#include "game_header.h"




void add_object(long long obj_id, int x, int y);
void move_object(long long obj_id, int old_x, int old_y, int new_x, int new_y);
void remove_object(long long obj_id, int x, int y);

void get_aoi_candidates(int px, int py, std::set<long long>& out);