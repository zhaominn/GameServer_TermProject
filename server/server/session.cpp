#include <iostream>
#include <set>

#include <WS2tcpip.h>
#include <MSWSock.h>
#pragma comment (lib,"WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

#include <unordered_map>

#include "session.h"
#include "game_header.h"

extern std::unordered_map<long long, std::shared_ptr<SESSION>> Characters;
extern std::unordered_map<long long, std::shared_ptr<NPC_SESSION>> npcs;

SESSION::SESSION(long long session_id, SOCKET s) : id(session_id), socket(s)
{
	state = EMPTY;
	remained = 0;
	recv_packet();
}

SESSION::~SESSION()
{
	sc_packet_leave leave_packet;
	leave_packet.size = sizeof(leave_packet);
	leave_packet.type = S2C_P_LEAVE;
	leave_packet.id = id;
	for (auto& c : Characters) {
		if (id != c.first)
			c.second->send_packet(&leave_packet);
	}
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

	if (target_id < MAX_USER) {
		auto& t = Characters[target_id];
		packet.x = t->x;
		packet.y = t->y;
	}
	else {
		auto& n = npcs[target_id];
		packet.x = n->x;
		packet.y = n->y;
	}
	send_packet(&packet);
}

void SESSION::send_add_player_packet(int target_id) {
	sc_packet_enter packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_ENTER;
	packet.id = target_id;

	if (target_id < MAX_USER) {
		auto& t = Characters[target_id];
		strncpy_s(packet.name, t->name.c_str(), MAX_ID_LENGTH);
		packet.o_type = 0; // 플레이어
		packet.x = t->x;
		packet.y = t->y;
	}
	else {
		auto& n = npcs[target_id];
		strncpy_s(packet.name, n->name.c_str(), MAX_ID_LENGTH);
		packet.o_type = 1; // NPC
		packet.x = n->x;
		packet.y = n->y;
	}

	send_packet(&packet);
}

void SESSION::send_remove_player_packet(int target_id) {
	sc_packet_leave packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_LEAVE;
	packet.id = target_id;
	send_packet(&packet);
}

void SESSION::process_packet(unsigned char* p)
{
	const unsigned char packet_type = p[1];
	switch (packet_type) {
	case C2S_P_LOGIN:
	{
		cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);
		name = packet->name;
		x = rand() % MAP_WIDTH;
		y = rand() % MAP_HEIGHT;
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
		switch (packet->direction) {
		case MOVE_UP: if (y > 0) y = y - 1; break;
		case MOVE_DOWN: if (y < (MAP_HEIGHT - 1)) y = y + 1; break;
		case MOVE_LEFT: if (x > 0) x = x - 1; break;
		case MOVE_RIGHT:if (x < (MAP_WIDTH - 1)) x = x + 1; break;
		}

		std::set<int> near_list;
		std::set<int> old_vlist = view_list;

		send_move_player_packet(id);

		for (auto& c : Characters) {
			if (c.first != id && can_see(*c.second))
				near_list.insert(c.second->id);
		}
		for (auto& c : npcs) {
			if (can_see(*c.second))
				near_list.insert(c.second->id);
		}

		for (auto& nl : near_list) {
			if (nl < MAX_USER) {
				if (Characters[nl]->view_list.count(id)) {
					Characters[nl]->send_move_player_packet(id);
				}
			}
			else {
				if (old_vlist.count(nl) == 0)
					npcs[nl]->active = true;
			}

			if (old_vlist.count(nl) == 0)
				send_add_player_packet(nl);
		}

		for (auto& ol : old_vlist) {
			if(near_list.count(ol)==0){
				send_remove_player_packet(ol);
				if (ol < MAX_USER)
					Characters[ol]->send_remove_player_packet(ol);
			}
		}
		view_list = std::move(near_list);

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