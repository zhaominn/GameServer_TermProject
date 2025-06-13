#pragma once
#include <memory>
#include "exp_over.h"

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
	unsigned char remained;
	unsigned char recv_buffer[MAX_CHAT_LENGTH];

public:
	SESSION();

	SESSION(long long session_id, SOCKET s);

	~SESSION();

	void recv();

	void send_packet(void* packet);

	void send_player_info();

	void send_player_pos();

	void process_packet(unsigned char* p);
};
