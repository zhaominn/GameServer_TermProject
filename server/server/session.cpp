#include <iostream>

#include <WS2tcpip.h>
#include <MSWSock.h>
#pragma comment (lib,"WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

#include <unordered_map>

#include "session.h"
#include "game_header.h"

extern std::unordered_map<long long, std::shared_ptr<SESSION>> Characters;

SESSION::SESSION() {
	std::cout << "DEFAULT SESSION 생성자 호출\n";
	exit(-1);
}

SESSION::SESSION(long long session_id, SOCKET s) : id(session_id), socket(s)
{
	remained = 0;
	recv();
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

void SESSION::recv()
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
	// else 정상동작 (recv_over의 책임 Life-cycle은 IOCP/worker에게 위임)
}

void SESSION::send_packet(void* packet)
{
	EXP_OVER* o = new EXP_OVER(SEND);
	const unsigned char packet_size = reinterpret_cast<unsigned char*>(packet)[0];
	memcpy(o->packet, packet, packet_size);
	o->wsabuf[0].len = packet_size;
	DWORD size_sent;
	WSASend(socket, o->wsabuf, 1, &size_sent, 0, &(o->wsa_over), NULL);
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

void SESSION::send_player_pos() {
	sc_packet_move packet;
	packet.size = sizeof(packet);
	packet.type = S2C_P_MOVE;
	packet.id = id;
	packet.x = x;
	packet.y = y;
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
			if (c.first != id)
				c.second->send_packet(&enter_packet);
		}

		for (auto& c : Characters) {
			if (c.first != id) {
				sc_packet_enter character_enter_packet;
				character_enter_packet.size = sizeof(character_enter_packet);
				character_enter_packet.type = S2C_P_ENTER;
				character_enter_packet.id = c.first;
				strcpy_s(character_enter_packet.name, c.second->name.c_str());
				character_enter_packet.o_type = 0;
				character_enter_packet.x = c.second->x;
				character_enter_packet.y = c.second->y;
				send_packet(&character_enter_packet);
			}
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

		sc_packet_move move_packet;
		move_packet.size = sizeof(move_packet);
		move_packet.type = S2C_P_MOVE;
		move_packet.id = id;
		move_packet.x = x;
		move_packet.y = y;
		for (auto& u : Characters) {
			u.second->send_packet(&move_packet);
		}
		send_player_pos();
		break;
	}
	default:
		std::cout << "Error Invalid Packet Type\n";
		exit(-1);
	}
}