#pragma once
#include "exp_over.h"

enum STATE { EMPTY, CONNECTED, INGAME };

class SESSION {
public:
	long long id;
	std::string name;
	short x, y;
	char dir;
	short max_hp;
	short hp;
	short level;
	int   exp;

	SOCKET socket;
	STATE state;
	unsigned char remained;
	unsigned char recv_buffer[MAX_CHAT_LENGTH];

	std::set<long long> view_list;

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

	void send_state_change_packet();

	virtual void take_damage(int damage, long long attacker_id);

	void process_packet(unsigned char* p);

	bool can_see(const SESSION& other) const;

	bool can_see_obstacle(const int x, const int y) const;
};
