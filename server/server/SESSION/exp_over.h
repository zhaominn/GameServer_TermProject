#pragma once
#include "../MAP/tile_map.h"

enum IO_TYPE { RECV, SEND, ACCEPT };

class EXP_OVER
{
public:
	WSAOVERLAPPED	wsa_over;
	WSABUF			wsabuf[1];
	unsigned char	packet[MAX_CHAT_LENGTH];
	IO_TYPE			io_type;
	SOCKET accept_socket;

	EXP_OVER(IO_TYPE t) : io_type(t)
	{
		ZeroMemory(&wsa_over, sizeof(wsa_over));
		wsabuf[0].buf = reinterpret_cast<CHAR*>(packet);
		wsabuf[0].len = sizeof(packet);
	}
};
