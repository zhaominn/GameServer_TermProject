#pragma once
#include "session.h"

class NPC_SESSION : public SESSION {
public:
	bool		active;
	bool		chasing = false;
	long long	chase_target_id = -1;
	char		npc_type;
	std::chrono::steady_clock::time_point next_move;
	bool dead = false;
	std::chrono::steady_clock::time_point respawn_time;

	NPC_SESSION() = default;

	NPC_SESSION(int id, std::string name);

	void send_enter_packet(SESSION* session);

	void random_move();

	void npc_move();

	void take_damage(int damage, long long attacker_id) override;

	void give_damage(SESSION* target,int damage) override;

	void npc_die();
};