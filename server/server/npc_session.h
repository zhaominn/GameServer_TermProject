#pragma once
#include "session.h"

enum NPC_TYPE {
	PEACE = (char)1,		 // 고정 평화
	AGRO = (char)2,			 // 고정 어그로
	ROAMING_PEACE = (char)3, // 돌아다니는 평화 NPC
	ROAMING_AGRO = (char)4	 // 돌아다니는 어그로 NPC
};

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

	NPC_SESSION(int id, std::string name, char npc_type);

	void send_enter_packet(SESSION* session);

	void peace_npc_move();

	void agro_npc_move();

	void roaming_peace_npc_move();

	void roaming_agro_npc_move();

	void npc_move();

	void take_damage(int damage, long long attacker_id) override;

	void give_damage(SESSION* target, int damage) override;

	void npc_die();
};