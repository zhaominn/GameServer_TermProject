#include "pch.h"
#include "npc_session.h"

extern std::unordered_map<long long, std::shared_ptr<SESSION>> Characters;
extern std::unordered_map<long long, std::shared_ptr<NPC_SESSION>> npcs;
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
	npc_type = rand() % 2 + 1;
	next_move = std::chrono::steady_clock::now() + std::chrono::milliseconds(100 + (rand() % 5000));
}

void NPC_SESSION::send_enter_packet(SESSION* session) {
	sc_packet_enter packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_ENTER;
	packet.id = id;
	strncpy_s(packet.name, sizeof(packet.name), name.c_str(), _TRUNCATE);
	packet.name[sizeof(packet.name) - 1] = 0;
	packet.o_type = npc_type; // NPC
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

void NPC_SESSION::npc_move() {
	auto now = std::chrono::steady_clock::now();
	if ((next_move > now) || !active) return;
	int old_x = x, old_y = y;

	std::unordered_map<long long, bool> old_seen;
	for (auto& u : Characters)
		old_seen[u.first] = u.second->view_list.count(id) > 0;

	// -- PEACE TYPE:
	if (npc_type == 1) {
		if (chasing && Characters.count(chase_target_id)) {
			auto target = Characters[chase_target_id];
			int nx = x, ny = y;
			if (abs(target->x - x) + abs(target->y - y) == 1) {
				give_damage(target.get(), ATTACK_POWER);

				next_move = now + std::chrono::milliseconds(200 + rand() % 500);
				return;
			}
			else {
				if (target->x > x) { nx++; dir = MOVE_RIGHT; }
				else if (target->x < x) { nx--; dir = MOVE_LEFT; }
				else if (target->y > y) { ny++; dir = MOVE_DOWN; }
				else if (target->y < y) { ny--; dir = MOVE_UP; }
			}

			bool blocked = false;
			for (auto oid : sectors[nx / SECTOR_SIZE][ny / SECTOR_SIZE]) {
				if (oid >= MAX_USER + NUM_MONSTER) {
					if (obstacles.count(oid) && obstacles[oid]->x == nx && obstacles[oid]->y == ny) {
						blocked = true; break;
					}
				}
			}
			if (blocked) {
				next_move = now + std::chrono::milliseconds(200 + rand() % 500);
				return;
			}
			x = nx; y = ny;
			for (auto& u : Characters)
				if (u.second->can_see(*this))
					u.second->send_move_player_packet(id);
		}
		else {
			next_move = now + std::chrono::milliseconds(200 + rand() % 2000);
			return;
		}
	}

	// -- AGRO TYPE: 플레이어 시야 11내에 보이면 아무나 추적, 없으면 가만히
	else if (npc_type == 2) {
		long long target_id = -1;
		int min_dist = 99999;

		for (auto& p : Characters) {
			int dx = abs(p.second->x - x);
			int dy = abs(p.second->y - y);
			if (dx <= 5 && dy <= 5) {
				int dist = dx + dy;
				if (dist < min_dist) {
					min_dist = dist;
					target_id = p.first;
				}
			}
		}

		if (target_id != -1 && Characters.count(target_id)) {
			auto target = Characters[target_id];
			int dx = target->x - x;
			int dy = target->y - y;

			if (abs(dx) + abs(dy) == 1) {
				give_damage(target.get(), ATTACK_POWER);

				next_move = now + std::chrono::milliseconds(200 + rand() % 500);
				return;
			}

			int nx = x, ny = y, new_dir = dir;
			if (abs(dx) >= abs(dy)) {
				if (dx > 0) { nx++; new_dir = MOVE_RIGHT; }
				else if (dx < 0) { nx--; new_dir = MOVE_LEFT; }
			}
			else {
				if (dy > 0) { ny++; new_dir = MOVE_DOWN; }
				else if (dy < 0) { ny--; new_dir = MOVE_UP; }
			}

			bool blocked = false;
			for (auto oid : sectors[nx / SECTOR_SIZE][ny / SECTOR_SIZE]) {
				if (oid >= MAX_USER + NUM_MONSTER) {
					if (obstacles.count(oid) && obstacles[oid]->x == nx && obstacles[oid]->y == ny) {
						blocked = true;
						break;
					}
				}
			}
			if (blocked) {
				next_move = now + std::chrono::milliseconds(200 + rand() % 1000);
				return;
			}

			x = nx;
			y = ny;
			dir = new_dir;

			for (auto& u : Characters)
				if (u.second->can_see(*this))
					u.second->send_move_player_packet(id);
		}
		else {
			next_move = now + std::chrono::milliseconds(200 + rand() % 1000);
			return;
		}
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
	}

	next_move = std::chrono::steady_clock::now() + std::chrono::milliseconds(100 + (rand() % 5000));
}

void NPC_SESSION::take_damage(int damage, long long attacker_id) {
	hp -= damage;
	if (hp < 0) {
		hp = 0;
		int exp_size = Characters[attacker_id]->level * Characters[attacker_id]->level * 2* npc_type;
		Characters[attacker_id]->plus_exp(exp_size);
		npc_die();
	}

	if (npc_type == 1 && !chasing && Characters.count(attacker_id)) {
		chasing = true;
		chase_target_id = attacker_id;
	}
	send_state_change_packet();
}

void NPC_SESSION::give_damage(SESSION* target, int damage) {
	printf("[%s]  -> [%s] (id:%lld) : %d 데미지를 주었습니다.\n",
		name.c_str(), target->name.c_str(), target->id, damage);

	target->take_damage(damage, id);
	send_state_change_packet();
}

void NPC_SESSION::npc_die() {
		// 기존 LEAVE 패킷/컨테이너 erase 전...
		active = false;
		dead = true;
		respawn_time = std::chrono::steady_clock::now() + std::chrono::seconds(30);

		// 나머지 기존 remove_npc 코드 실행
		sc_packet_leave packet;
		packet.size = sizeof(packet);
		packet.type = S2C_P_LEAVE;
		packet.id = id;

		for (auto& c : Characters) {
			if (c.second->view_list.count(id)) {
				c.second->view_list.erase(id);
			}
		}
		remove_object(id, x, y);
}