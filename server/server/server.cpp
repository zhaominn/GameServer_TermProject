#include <iostream>
#include <string>

#include <WS2tcpip.h>
#include <MSWSock.h>
#pragma comment (lib,"WS2_32.LIB")
#pragma comment (lib, "MSWSock.LIB")

#include <thread>
#include <vector>
#include <mutex>
#include <set>
#include <unordered_map>

#include "session.h"
#include "game_header.h"

using namespace std;

unordered_map<long long, shared_ptr<SESSION>> Characters;
unordered_map<long long, shared_ptr<NPC_SESSION>> npcs;

atomic<long long> global_new_id = 0;
atomic<bool> npc_running = true;

SOCKET s_socket;
mutex m_characters;

void InitializeNPC()
{
	cout << "NPC intialize begin.\n";
	for (int i = 0; i < NUM_MONSTER; ++i) {
		int npc_id = MAX_USER + i;
		npcs[npc_id] = make_shared<NPC_SESSION>(npc_id, "NPC" + std::to_string(i));
	}
	cout << "NPC initialize end.\n";
}

void do_accept()
{
	EXP_OVER* accept_over = new EXP_OVER(ACCEPT);

	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (c_socket == INVALID_SOCKET) {
		printf("WSASocket failed for accept: %d\n", WSAGetLastError());
		delete accept_over;
		return;
	}
	accept_over->accept_socket = c_socket;

	BOOL ok = AcceptEx(s_socket, c_socket, accept_over->packet, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		NULL, reinterpret_cast<LPWSAOVERLAPPED>(accept_over));
	if (!ok && WSAGetLastError() != WSA_IO_PENDING) {
		printf("AcceptEx failed: %d\n", WSAGetLastError());
		closesocket(c_socket);
		delete accept_over;
		return;
	}
}

void npc_thread_func() {
	while (npc_running) {
		std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 0.5초마다

		auto now = std::chrono::steady_clock::now();
		std::lock_guard<std::mutex> lock(m_characters);
		for (auto& it : npcs) {
			auto& npc = it.second;
			if ((npc->next_move > now)|| (!npc->active)) continue;

			std::unordered_map<long long, bool> old_seen;
			for (auto& u : Characters)
				old_seen[u.first] = u.second->view_list.count(npc->id) > 0;

			// 랜덤 방향으로 이동
			int dir = rand() % 4;
			switch (dir) {
			case 0: if (npc->y > 0) npc->y--; break;
			case 1: if (npc->y < MAP_HEIGHT - 1) npc->y++; break;
			case 2: if (npc->x > 0) npc->x--; break;
			case 3: if (npc->x < MAP_WIDTH - 1) npc->x++; break;
			}

			for (auto& u : Characters) {
				bool now_seen = u.second->can_see(*npc);
				bool was_seen = old_seen[u.first];

				if (now_seen && !was_seen) {
					// ENTER: 처음 보임
					u.second->send_add_player_packet(npc->id);
					u.second->view_list.insert(npc->id);
				}
				else if (!now_seen && was_seen) {
					// LEAVE: 시야에서 나감
					u.second->send_remove_player_packet(npc->id);
					u.second->view_list.erase(npc->id);
				}
				else if (now_seen && was_seen) {
					// MOVE: 여전히 시야 안 (좌표만 update)
					u.second->send_move_player_packet(npc->id);
				}
			}

			npc->next_move = std::chrono::steady_clock::now() + std::chrono::milliseconds(100 + (rand() % 5000));
		}
	}
}

void work_thread(HANDLE hIOCP) {
	while (true) {
		DWORD io_size;
		ULONG_PTR key;
		WSAOVERLAPPED* over;
		GetQueuedCompletionStatus(hIOCP, &io_size, &key, &over, INFINITE);
		if (!over) continue;

		EXP_OVER* eo = reinterpret_cast<EXP_OVER*>(over);

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
			long long session_id = global_new_id++;
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(eo->accept_socket), hIOCP, session_id, 0);

			{
				std::lock_guard<std::mutex> lock(m_characters);
				Characters.emplace(session_id, std::make_shared<SESSION>(session_id, eo->accept_socket));
			}

			delete eo;

			do_accept();
			break;
		}
		case SEND:
		{
			delete eo;
			break;
		}
		case RECV:
		{
			std::shared_ptr<SESSION> character;
			{
				std::lock_guard<std::mutex> lock(m_characters);
				auto it = Characters.find(key);
				if (it == Characters.end()) {
					delete eo;
					break;
				}
				character = it->second;
			}
			unsigned char* p = eo->packet;
			int data_size = io_size + character->remained;
			while (p < eo->packet + data_size) {
				unsigned char packet_size = *p;
				if (p + packet_size > eo->packet + data_size)
					break;
				character->process_packet(p);
				p = p + packet_size;
			}
			if (p < eo->packet + data_size) {
				character->remained = static_cast<unsigned char>(eo->packet + data_size - p);
				memcpy(character->recv_buffer, p, character->remained);
			}
			else {
				character->remained = 0;
			}
			character->recv_packet();
			delete eo;
			break;
		}
		}
	}
}

int main()
{
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

	InitializeNPC();
	thread npc_thread(npc_thread_func);
	do_accept();

	int worker_cnt = std::thread::hardware_concurrency();
	if (worker_cnt == 0) worker_cnt = 4;
	vector<std::thread> workers;
	for (int i = 0; i < worker_cnt; ++i) {
		workers.emplace_back([hIOCP]() { work_thread(hIOCP); });
	}

	for (auto& t : workers) t.join();

	npc_running = false;
	npc_thread.join();

	closesocket(s_socket);
	WSACleanup();
	return 0;
}

