#include "pch.h"
#include "npc_session.h"

extern std::unordered_map<long long, std::shared_ptr<SESSION>> Characters;
extern std::unordered_map<long long, std::shared_ptr<Obstacle>> obstacles;
extern std::unordered_set<long long> sectors[SECTOR_W][SECTOR_H];

NPC_SESSION::NPC_SESSION(int id, std::string name) {
	this->id = id;
	this->name = name;
	x = rand() % MAP_WIDTH;
	y = rand() % MAP_HEIGHT;
	max_hp = NPC_MAX_HP;
	hp = NPC_MAX_HP;
	level = 0;
	exp = 0;
	state = INGAME;
	active = false;
	next_move = std::chrono::steady_clock::now() + std::chrono::milliseconds(100 + (rand() % 5000));
}

void NPC_SESSION::send_enter_packet(SESSION* session) {
	sc_packet_enter packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_ENTER;
	packet.id = id;
	strncpy_s(packet.name, sizeof(packet.name), name.c_str(), _TRUNCATE);
	packet.name[sizeof(packet.name) - 1] = 0;
	packet.o_type = 1; // NPC
	packet.x = x;
	packet.y = y;

	session->send_packet(&packet);
}

void NPC_SESSION::random_move() {
	auto now = std::chrono::steady_clock::now();
	if ((next_move > now) || !active) return;

	std::unordered_map<long long, bool> old_seen;
	for (auto& u : Characters)
		old_seen[u.first] = u.second->view_list.count(id) > 0;

	// 랜덤 방향으로 이동
	int dir = rand() % 4;
	int old_x = x; int old_y = y;
	switch (dir) {
	case 0: if (y > 0) --y; break;
	case 1: if (y < MAP_HEIGHT - 1) ++y; break;
	case 2: if (x > 0) --x; break;
	case 3: if (x < MAP_WIDTH - 1) ++x; break;
	}

	bool blocked = false;
	for (auto id : sectors[x / SECTOR_SIZE][y / SECTOR_SIZE]) {
		if (id >= MAX_USER + NUM_MONSTER) { // 장애물 id
			if (obstacles.count(id) && obstacles[id]->x == x && obstacles[id]->y == y) {
				blocked = true;
				break;
			}
		}
	}

	if (blocked) {
		x = old_x;
		y = old_y;
		return;
	}

	for (auto& u : Characters) {
		bool now_seen = u.second->can_see(*this);
		bool was_seen = old_seen[u.first];

		if (now_seen && !was_seen) {
			// ENTER: 처음 보임
			u.second->send_add_player_packet(id);
			u.second->view_list.insert(id);
		}
		else if (!now_seen && was_seen) {
			// LEAVE: 시야에서 나감
			u.second->send_remove_player_packet(id);
			u.second->view_list.erase(id);
		}
		else if (now_seen && was_seen) {
			// MOVE: 여전히 시야 안 (좌표만 update)
			u.second->send_move_player_packet(id);
		}
	}

	next_move = std::chrono::steady_clock::now() + std::chrono::milliseconds(100 + (rand() % 5000));
}