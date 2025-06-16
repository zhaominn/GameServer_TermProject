#pragma once
#include "database_manager.h"


class SECTOR_MANAGER {
public:
void add_object(int obj_id, int x, int y);
void move_object(int obj_id, int old_x, int old_y, int new_x, int new_y);
void remove_object(int obj_id, int x, int y);
void get_aoi_candidates(int px, int py, std::set<int>& out);
};

extern SECTOR_MANAGER sector_manager;
