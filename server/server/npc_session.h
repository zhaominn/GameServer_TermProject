#pragma once
#include "session.h"


class NPC_SESSION : public SESSION {
public:
	bool active;
	std::chrono::steady_clock::time_point next_move;

	NPC_SESSION() = default;

	NPC_SESSION(int id, std::string name);

	void send_enter_packet(SESSION* session);

	void random_move();
};