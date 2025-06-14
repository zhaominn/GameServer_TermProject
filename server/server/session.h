#pragma once
#include <set>
#include <chrono>
#include "exp_over.h"

enum STATE { EMPTY, CONNECTED, INGAME };

class SESSION {
public:
	long long id;
	std::string name;
	short x, y;
	short max_hp;
	short hp;
	short level;
	int   exp;

	SOCKET socket;
	STATE state;
	unsigned char remained;
	unsigned char recv_buffer[MAX_CHAT_LENGTH];

	std::set<int> view_list;

public:
	SESSION() : id(0), name(), x(0), y(0), max_hp(0), hp(0), level(0), exp(0),
		socket(INVALID_SOCKET), state(EMPTY), remained(0) {
		memset(recv_buffer, 0, sizeof(recv_buffer));
	};

	SESSION(long long session_id, SOCKET s);

	~SESSION();

	void recv_packet();

	void send_packet(void* packet);

	void send_player_info();

	void send_move_player_packet(int target_id);

	void send_add_player_packet(int target_id);

	void send_remove_player_packet(int target_id);

	void process_packet(unsigned char* p);

	bool can_see(const SESSION& other) const;
};

class NPC_SESSION : public SESSION {
public:
	bool active;
	std::chrono::steady_clock::time_point next_move;

	NPC_SESSION() = default;

	NPC_SESSION(int id, std::string name) {
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
		next_move = std::chrono::steady_clock::now() + std::chrono::milliseconds(100 + (rand() % 5000))
	}

	void send_enter_packet(SESSION *session) {
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
};