#include <iostream>
#include <unordered_map>
#include <set>
#include <WS2tcpip.h>
#include <MSWSock.h>

#include <thread>
#include <vector>
#include <mutex>

#include <crtdbg.h>
using namespace std;

#include "game_header.h"
#pragma comment (lib,"WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

enum IO_TYPE { RECV, SEND, ACCEPT };

class EXP_OVER
{
public:
	WSAOVERLAPPED	wsa_over;
	WSABUF			wsabuf[1];
	unsigned char	packet[MAX_CHAT_LENGTH];
	IO_TYPE			io_type;

	EXP_OVER(IO_TYPE t) : io_type(t)
	{
		ZeroMemory(&wsa_over, sizeof(wsa_over));
		wsabuf[0].buf = reinterpret_cast<CHAR*>(packet);
		wsabuf[0].len = sizeof(packet);
	}

	//virtual ~EXP_OVER();
};

class IO_OVER : public EXP_OVER {
public:
	IO_OVER(IO_TYPE t) : EXP_OVER(t) {}
};

class ACCEPT_OVER : public EXP_OVER {
public:
	SOCKET accept_socket;
	ACCEPT_OVER() : EXP_OVER(ACCEPT) {
		accept_socket = INVALID_SOCKET;
	}
	//virtual ~ACCEPT_OVER();
};

class SESSION;

unordered_map<long long, shared_ptr<SESSION>> Characters;

class SESSION {
public:
	long long id;
	string name;
	short x, y;
	short max_hp;
	short hp;
	short level;
	int   exp;

	SOCKET socket;
	unsigned char remained;
	unsigned char recv_buffer[MAX_CHAT_LENGTH];

public:
	SESSION() {
		std::cout << "DEFAULT SESSION 생성자 호출\n";
		exit(-1);
	}

	SESSION(long long session_id, SOCKET s) : id(session_id), socket(s)
	{
		remained = 0;
		recv();
	}

	~SESSION()
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

	void recv()
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

		auto* recv_over = new IO_OVER(RECV);
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


	void send_packet(void* packet)
	{
		IO_OVER* o = new IO_OVER(SEND);
		const unsigned char packet_size = reinterpret_cast<unsigned char*>(packet)[0];
		memcpy(o->packet, packet, packet_size);
		o->wsabuf[0].len = packet_size;
		DWORD size_sent;
		WSASend(socket, o->wsabuf, 1, &size_sent, 0, &(o->wsa_over), NULL);
	}

	void send_player_info()
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

	void send_player_pos() {
		sc_packet_move packet;
		packet.size = sizeof(packet);
		packet.type = S2C_P_MOVE;
		packet.id = id;
		packet.x = x;
		packet.y = y;
		send_packet(&packet);
	}

	void process_packet(unsigned char* p)
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
};

std::atomic<long long> global_new_id = 0;

SOCKET s_socket;
mutex m_characters;

std::set<void*> allocated_overlapped_ptrs;

void do_accept()
{
	ACCEPT_OVER* accept_over = new ACCEPT_OVER();
	
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (c_socket == INVALID_SOCKET) {
		printf("WSASocket failed for accept: %d\n", WSAGetLastError());
		delete accept_over;
		printf("[DELETE COMPLETE] accept_over: %p\n", accept_over);
		return;
	}
	accept_over->accept_socket = c_socket;
	printf("ACCEPT_OVER NEW %p sock=%d\n", accept_over, accept_over->accept_socket);

	BOOL ok = AcceptEx(s_socket, c_socket, accept_over->packet, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		NULL, reinterpret_cast<LPWSAOVERLAPPED>(accept_over));
	if (!ok && WSAGetLastError() != WSA_IO_PENDING) {
		printf("AcceptEx failed: %d\n", WSAGetLastError());
		closesocket(c_socket);
		delete accept_over;
		return;
	}
	allocated_overlapped_ptrs.insert(accept_over);
}

void work_thread(HANDLE hIOCP) {
	while (true) {
		DWORD io_size;
		WSAOVERLAPPED* o;
		ULONG_PTR key;
		BOOL ret = GetQueuedCompletionStatus(hIOCP, &io_size, &key, &o, INFINITE);
		if (!o) continue;

		EXP_OVER* eo = reinterpret_cast<EXP_OVER*>(o);

		std::shared_ptr<SESSION> character;
		{
			std::lock_guard<std::mutex> lock(m_characters);

			if ((eo->io_type == RECV || eo->io_type == SEND) && (0 == io_size)) {
				if (Characters.count(key) != 0)
					Characters.erase(key);
				delete eo;
				continue;
			}
		}

		switch (eo->io_type) {
		case ACCEPT:
		{
			ACCEPT_OVER* accept_over = static_cast<ACCEPT_OVER*>(eo);

			long long session_id = global_new_id++;
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(accept_over->accept_socket), hIOCP, session_id, 0);

			{
				std::lock_guard<std::mutex> lock(m_characters);
				Characters.emplace(session_id, std::make_shared<SESSION>(session_id, accept_over->accept_socket));
			}
			if (allocated_overlapped_ptrs.count(o) == 0) {
				printf(">>>>> [HEAP CORRUPTION] Unmanaged overlapped pointer returned from GQCS: %p <<<<<\n", o);
				// 혹은 assert(false), __debugbreak();
			}
			allocated_overlapped_ptrs.erase(o);
			
			printf("[ACCEPT DELETE] accept_over: %p, io_type: %d\n", accept_over, accept_over->io_type);
			delete o;

			do_accept();
			break;
		}
		case SEND:
		{
			delete o;
			break;
		}
		case RECV:
		{
			auto* io_over = static_cast<IO_OVER*>(eo);
			{
				std::lock_guard<std::mutex> lock(m_characters);
				auto it = Characters.find(key);
				if (it == Characters.end()) {
					delete io_over;
					break;
				}
				character = it->second;
			}
			unsigned char* p = io_over->packet;
			int data_size = io_size + character->remained;
			while (p < io_over->packet + data_size) {
				unsigned char packet_size = *p;
				if (p + packet_size > io_over->packet + data_size)
					break;
				character->process_packet(p);
				p = p + packet_size;
			}
			if (p < io_over->packet + data_size) {
				character->remained = static_cast<unsigned char>(io_over->packet + data_size - p);
				memcpy(character->recv_buffer, p, character->remained);
			}
			else {
				character->remained = 0;
			}
			character->recv();
			delete o;
			break;
		}
		}
	}
}

int main()
{
	// main()의 가장 첫 부분에
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_ALWAYS_DF);

	std::wcout.imbue(std::locale("korean"));

	WSADATA WSAData;
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0) {
		std::cout << "WSAStartup 실패!\n";
		return -1;
	}

	s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (s_socket <= 0) std::cout << "ERRPR" << "원인";
	else std::cout << "Socket Created.\n";

	SOCKADDR_IN server_addr;
	ZeroMemory(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(GAME_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(s_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
	listen(s_socket, SOMAXCONN);
	INT addr_size = sizeof(server_addr);

	HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(s_socket), hIOCP, -1, 0);

	do_accept();

	int worker_cnt = std::thread::hardware_concurrency();
	if (worker_cnt == 0) worker_cnt = 4;
	vector<std::thread> workers;
	for (int i = 0; i < worker_cnt; ++i) {
		workers.emplace_back([hIOCP]() { work_thread(hIOCP); });
	}

	for (auto& t : workers) t.join();

	closesocket(s_socket);
	WSACleanup();
	return 0;
}

