#include "pch.h"
#include "npc_session.h"

extern std::unordered_map<long long, std::shared_ptr<SESSION>> Characters;
extern std::unordered_map<long long, std::shared_ptr<NPC_SESSION>> npcs;
extern std::unordered_map<long long, std::shared_ptr<Obstacle>> obstacles;
extern std::unordered_set<long long> sectors[SECTOR_W][SECTOR_H];

SESSION::SESSION(long long session_id, SOCKET s) : id(session_id), socket(s)
{
	state = EMPTY;
	remained = 0;
	recv_packet();
}

SESSION::~SESSION()
{
	db_update_user_info(id, x, y, dir, max_hp, hp, level, exp, name);

	sc_packet_leave leave_packet;
	leave_packet.size = sizeof(leave_packet);
	leave_packet.type = S2C_P_LEAVE;
	leave_packet.id = id;
	for (auto& c : Characters) {
		if (id != c.first)
			c.second->send_packet(&leave_packet);
	}
	sector_manager.remove_object(id, x, y);
	closesocket(socket);
}

void SESSION::recv_packet()
{

	if (socket == INVALID_SOCKET) {
		// 이미 제거된 세션, 죽은 소켓일 수 있음
		return;
	}

	// remained 범위 체크
	if (remained > MAX_CHAT_LENGTH) {
		printf("Error: remained buffer overflow! (%d)\n", remained);
		remained = 0; // 혹은 assert!
	}

	auto* recv_over = new EXP_OVER(RECV);
	DWORD recv_flag = 0;
	ZeroMemory(&recv_over->wsa_over, sizeof(recv_over->wsa_over));
	if (remained > 0)
		memcpy(recv_over->packet, recv_buffer, remained);

	recv_over->wsabuf[0].buf = reinterpret_cast<CHAR*>(recv_over->packet + remained);
	recv_over->wsabuf[0].len = sizeof(recv_over->packet) - remained;

	int r = WSARecv(socket, recv_over->wsabuf, 1, NULL, &recv_flag, &recv_over->wsa_over, NULL);
	int err = WSAGetLastError();

	if (r == SOCKET_ERROR && err != WSA_IO_PENDING) {
		printf("WSARecv failed: %d, socket=%d\n", err, socket);
		delete recv_over;
		// 세션 강제 삭제 또는 오류 기록
		return;
	}
}

void SESSION::send_packet(void* packet)
{
	EXP_OVER* o = new EXP_OVER(SEND);
	const unsigned char packet_size = reinterpret_cast<unsigned char*>(packet)[0];
	memcpy(o->packet, packet, packet_size);
	o->wsabuf[0].len = packet_size;
	WSASend(socket, o->wsabuf, 1, 0, 0, &(o->wsa_over), NULL);
}

void SESSION::send_player_info()
{
	sc_packet_avatar_info packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_AVATAR_INFO;
	packet.id = id;
	packet.x = x;
	packet.y = y;
	packet.level = level;
	packet.hp = hp;
	packet.exp = exp;
	send_packet(&packet);
}

void SESSION::send_move_player_packet(int target_id) {
	sc_packet_move packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_MOVE;
	packet.id = target_id;
	packet.x = x;
	packet.y = y;
	packet.dir = dir;

	if (target_id == id) {}
	else if (target_id < MAX_USER) {
		if (!Characters.count(target_id)) return;
		auto& t = Characters[target_id];
		packet.x = t->x;
		packet.y = t->y;
		packet.dir = t->dir;
	}
	else if (target_id < MAX_USER + NUM_MONSTER) {
		if (!npcs.count(target_id)) return;
		auto& t = npcs[target_id];
		packet.x = t->x;
		packet.y = t->y;
		packet.dir = t->dir;
	}
	send_packet(&packet);
}

void SESSION::send_add_player_packet(int target_id) {
	sc_packet_enter packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_ENTER;
	packet.id = target_id;

	if (target_id < MAX_USER) {
		if (!Characters.count(target_id)) return;
		auto& t = Characters[target_id];
		strncpy_s(packet.name, t->name.c_str(), MAX_ID_LENGTH);
		packet.o_type = 0; // 플레이어
		packet.x = t->x;
		packet.y = t->y;
	}
	else if (target_id < MAX_USER + NUM_MONSTER) {
		if (!npcs.count(target_id)) return;
		auto& n = npcs[target_id];
		strncpy_s(packet.name, n->name.c_str(), MAX_ID_LENGTH);
		packet.o_type = (n->npc_type == 1) ? 1 : 2;
		packet.x = n->x;
		packet.y = n->y;
	}
	else {
		if (!obstacles.count(target_id)) return;
		auto& n = obstacles[target_id];
		strncpy_s(packet.name, "Rock", MAX_ID_LENGTH);
		packet.o_type = 3; // Obstacle
		packet.x = n->x;
		packet.y = n->y;
		packet.rock_num = n->rock_num;
	}

	send_packet(&packet);
}

void SESSION::send_remove_player_packet(int target_id) {
	// printf("[DEBUG] send_remove_player_packet: target_id=%lld, by_id=%lld\n", target_id, id);

	sc_packet_leave packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_LEAVE;
	packet.id = target_id;
	send_packet(&packet);
}

void SESSION::send_state_change_packet() {
	sc_packet_stat_change p;
	p.size = sizeof(p);
	p.type = S2C_P_STAT_CHANGE;
	p.id = id;
	p.hp = hp;
	p.level = level;
	p.exp = exp;

	send_packet(&p);
}

void SESSION::take_damage(int damage, long long attacker_id = 0) {
	hp -= damage;
	if (hp < 0) hp = 0;

	send_state_change_packet();
}

void SESSION::give_damage(SESSION* target, int damage) {
	printf("[%s] -> [%s] (id:%lld) : %d 데미지를 주었습니다.\n",
		name.c_str(), target->name.c_str(), target->id, damage);

	target->take_damage(damage);
	send_state_change_packet();
}

void SESSION::plus_exp(int size) {
	exp += size;

	while (exp >= level * 100) {
		++level;
		printf("[%s] %d레벨로 레벨업하고 %d의 hp를 얻었습니다.\n",
			name.c_str(), level, level * 10);
		hp = min(100, hp + level * 10);
	}

	send_state_change_packet();
}

void SESSION::process_packet(unsigned char* p)
{
	const unsigned char packet_type = p[1];
	switch (packet_type) {
	case C2S_P_LOGIN:
	{
		cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);
		id = packet->id;

		std::string db_name;
		short db_x, db_y, db_maxhp, db_hp, db_level;
		char db_dir;
		int db_exp;

		long long my_id = id;
		bool found = db_get_user_info(my_id, db_name, db_x, db_y, db_dir, db_maxhp, db_hp, db_level, db_exp);

		if (found) {
			// -- DB정보 불러오기 성공(기존유저)
			name = db_name;
			x = db_x;
			y = db_y;
			dir = db_dir;
			max_hp = db_maxhp;
			hp = db_hp;
			level = db_level;
			exp = db_exp;
		}
		else {
			char name_buffer[32];
			sprintf_s(name_buffer, sizeof(name_buffer), "player_%d", _getpid());
			name = name_buffer;

			x = rand() % MAP_WIDTH;
			y = rand() % MAP_HEIGHT;
			dir = 1;
			max_hp = PLAYER_MAX_HP;
			hp = PLAYER_MAX_HP;
			level = 1;
			exp = 50;
			db_insert_user_info(my_id, name, x, y, dir, max_hp, hp, level, exp);
		}

		printf("client[%lld] %s login\n", id, name.c_str());
		send_player_info();

		sc_packet_enter enter_packet;
		enter_packet.size = sizeof(enter_packet);
		enter_packet.type = S2C_P_ENTER;
		enter_packet.id = id;
		strcpy_s(enter_packet.name, name.c_str());
		enter_packet.o_type = 0; // player
		enter_packet.x = x;
		enter_packet.y = y;
		sector_manager.add_object(id, x, y);

		// 기존 AOI + view_list 동기화 과정
		for (auto& c : Characters) {
			if (c.first != id && can_see(*c.second)) {
				c.second->send_packet(&enter_packet);
				view_list.insert(c.second->id);
			}
		}

		for (auto& n : npcs) {
			if (can_see(*n.second)) {
				n.second->send_enter_packet(this);
				view_list.insert(n.second->id);
			}
		}

		for (auto& p : obstacles) {
			if (can_see_obstacle(p.second->x, p.second->y)) {
				view_list.insert(p.second->id);
				send_add_player_packet(p.second->id);
			}
		}

		for (long long vid : view_list) {
			if (vid >= MAX_USER) {
				auto it = npcs.find(vid);
				if (it != npcs.end() && it->second)
					it->second->active = true;
			}
			send_add_player_packet(vid);
		}
		break;
	}
	case C2S_P_MOVE:
	{
		cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);
		short old_x = x; short old_y = y;

		switch (packet->direction) {
		case MOVE_UP: if (y > 0)					dir = MOVE_UP;		--y; break;
		case MOVE_DOWN: if (y < (MAP_HEIGHT - 1))	dir = MOVE_DOWN;	++y; break;
		case MOVE_LEFT: if (x > 0)					dir = MOVE_LEFT;	--x; break;
		case MOVE_RIGHT:if (x < (MAP_WIDTH - 1))	dir = MOVE_RIGHT;	++x; break;
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
			break;
		}

		std::set<long long> candidate_ids;
		sector_manager.get_aoi_candidates(x, y, candidate_ids);
		std::set<long long> near_list;
		std::set<long long> old_vlist = view_list;

		for (auto id : candidate_ids) {
			if (id == this->id) continue; // 자기 자신은 제외
			if (Characters.count(id) && can_see(*Characters[id])) {
				near_list.insert(id);
			}
			else if (npcs.count(id) && can_see(*npcs[id])) {
				near_list.insert(id);
			}
			else if (obstacles.count(id) && can_see_obstacle(obstacles[id]->x, obstacles[id]->y)) {
				near_list.insert(id);
			}
		}

		sector_manager.move_object(id, old_x, old_y, x, y);
		send_move_player_packet(id);

		for (auto nl : near_list) {
			bool is_new = (old_vlist.count(nl) == 0);

			if (nl < MAX_USER) {
				if (Characters.count(nl) && Characters[nl]->view_list.count(id)) {
					Characters[nl]->send_move_player_packet(id);
				}
			}
			else if (nl < MAX_USER + NUM_MONSTER) {
				if (is_new && npcs.count(nl))
					npcs[nl]->active = true;
			}
			if (is_new)
				send_add_player_packet(nl);
		}

		for (auto ol : old_vlist) {
			if (near_list.count(ol) == 0) {
				send_remove_player_packet(ol);
				if (ol < MAX_USER && Characters.count(ol)) {
					Characters[ol]->send_remove_player_packet(id);
				}
			}
		}

		view_list = std::move(near_list);

		break;
	}
	case C2S_P_ATTACK: {
		const static int dx[] = { 0, 0, -1, 1 };
		const static int dy[] = { -1, 1, 0, 0 };

		long long exp_gain_target_id = -1;
		std::string exp_gain_target_name = "";

		for (int dir = 0; dir < 4; ++dir) {
			int tx = x + dx[dir];
			int ty = y + dy[dir];

			if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT)
				continue;

			for (auto it = Characters.begin(); it != Characters.end(); ) {
				if (it->second->x == tx && it->second->y == ty) {
					give_damage(it->second.get(), ATTACK_POWER);
					break;
				}
				else {
					++it;
				}
			}
			for (auto it = npcs.begin(); it != npcs.end(); ) {
				if (it->second->x == tx && it->second->y == ty && !it->second->dead) {
					give_damage(it->second.get(), ATTACK_POWER);
					break;
				}
				else {
					++it;
				}
			}

		}
		break;
	}
	case C2S_P_REVIVE: {
		cs_packet_reborn* packet = reinterpret_cast<cs_packet_reborn*>(p);
		x = packet->x;
		y = packet->y;
		hp = PLAYER_MAX_HP;
		exp /= 2;
		// 추가적으로 플레이어 상태 재초기화, sector 등록 등 처리
		send_state_change_packet(); // ← 클라/주변에 HP/EXP 갱신
		break;
	}
	default:
		std::cout << "Error Invalid Packet Type\n";
		exit(-1);
	}
}

bool SESSION::can_see(const SESSION& other) const {
	return abs(this->x - other.x) <= VIEW_RANGE
		&& abs(this->y - other.y) <= VIEW_RANGE;
}

bool SESSION::can_see_obstacle(const int x, const int y) const {
	return abs(this->x - x) <= VIEW_RANGE
		&& abs(this->y - y) <= VIEW_RANGE;
}

void SESSION::on_logout() {
	printf("client[%lld] %s logout\n", id, name.c_str());
	db_update_user_info(id, x, y, dir, max_hp, hp, level, exp, name);
}