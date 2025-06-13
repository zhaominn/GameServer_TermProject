#include <iostream>

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

std::atomic<long long> global_new_id = 0;

SOCKET s_socket;
mutex m_characters;

std::set<void*> allocated_overlapped_ptrs;

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
			long long session_id = global_new_id++;
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(eo->accept_socket), hIOCP, session_id, 0);

			{
				std::lock_guard<std::mutex> lock(m_characters);
				Characters.emplace(session_id, std::make_shared<SESSION>(session_id, eo->accept_socket));
			}
			if (allocated_overlapped_ptrs.count(o) == 0) {
				printf(">>>>> [HEAP CORRUPTION] Unmanaged overlapped pointer returned from GQCS: %p <<<<<\n", o);
			}
			allocated_overlapped_ptrs.erase(o);
			
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
			character->recv();
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

