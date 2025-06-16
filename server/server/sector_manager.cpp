#include "pch.h"
#include "sector_manager.h"

std::unordered_set<long long> sectors[SECTOR_W][SECTOR_H];
inline int get_sector_x(int x) { return x / SECTOR_SIZE; }
inline int get_sector_y(int y) { return y / SECTOR_SIZE; }

void add_object(long long obj_id, int x, int y) {
	int sx = get_sector_x(x);
	int sy = get_sector_y(y);
	sectors[sx][sy].insert(obj_id);
}

void move_object(long long obj_id, int old_x, int old_y, int new_x, int new_y) {
	int old_sx = get_sector_x(old_x), old_sy = get_sector_y(old_y);
	int new_sx = get_sector_x(new_x), new_sy = get_sector_y(new_y);

	if (old_sx != new_sx || old_sy != new_sy) {
		sectors[old_sx][old_sy].erase(obj_id); // 옛 섹터에서 제거
		sectors[new_sx][new_sy].insert(obj_id); // 새 섹터에 추가
	}
}

void remove_object(long long obj_id, int x, int y) {
	int sx = get_sector_x(x);
	int sy = get_sector_y(y);
	sectors[sx][sy].erase(obj_id);
}

void get_aoi_candidates(int px, int py, std::set<long long>& out) {
	int sx = get_sector_x(px), sy = get_sector_y(py);

	// 내 섹터+주변 셀(15x15 시야, SECTOR_SIZE=8이면 대략 2x2 섹터)
	for (int dx = -1; dx <= 1; ++dx)
		for (int dy = -1; dy <= 1; ++dy) {
			int nsx = sx + dx, nsy = sy + dy;
			if (nsx < 0 || nsx >= SECTOR_W || nsy < 0 || nsy >= SECTOR_H) continue;
			for (auto id : sectors[nsx][nsy])
				out.insert(id);
		}
}