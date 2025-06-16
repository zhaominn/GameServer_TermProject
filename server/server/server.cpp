#include "pch.h"
#include "npc_session.h"

using namespace std;

unordered_map<long long, shared_ptr<SESSION>> Characters;
unordered_map<long long, shared_ptr<NPC_SESSION>> npcs;

atomic<int> global_new_id = 0;
atomic<bool> npc_running = true;
atomic<bool> autosave_running = true;

SOCKET s_socket;
mutex m_work, m_npc, m_autosave;

std::unordered_map<int, std::shared_ptr<Obstacle>> obstacles;

void InitializeTileMap() {
	memset(tile_map, 0, sizeof(tile_map));
}

void InitializeObstacles() {
	static std::random_device rd;
	static std::mt19937 gen(rd());
	static std::uniform_int_distribution<> dist_x(0, MAP_WIDTH - 1);
	static std::uniform_int_distribution<> dist_y(0, MAP_HEIGHT - 1);

	int next_obs_id = MAX_USER + NUM_MONSTER;
	for (int i = 0; i < NUM_OBSTACLE; ++i) {
		int x = dist_x(gen);
		int y = dist_y(gen);

		auto obs = std::make_shared<Obstacle>(next_obs_id++, x, y);
		obstacles[obs->id] = obs; // map 등록
		sector_manager.add_object(obs->id, x, y); // 섹터 등록
		tile_map[x][y].state = 1;
	}
}

void InitializeNPC()
{
	cout << "NPC intialize begin.\n";
	for (int i = 0; i < NUM_MONSTER; ++i) {
		int npc_id = MAX_USER + i;
		npcs[npc_id] = make_shared<NPC_SESSION>(npc_id, "NPC" + std::to_string(i), i % 4 + 1);
		sector_manager.add_object(npcs[npc_id]->id, npcs[npc_id]->x, npcs[npc_id]->y);
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

		std::lock_guard<std::mutex> lock(m_npc);
		for (auto& it : npcs) {
			auto& npc = it.second;

			if (npc->dead && npc->respawn_time <= chrono::steady_clock::now()) {
				npc->hp = NPC_MAX_HP;
				npc->active = true;
				npc->dead = false;

				sector_manager.add_object(npc->id, npc->x, npc->y);
				printf("[%s] (id:%lld) 부활! x=%d y=%d\n", npc->name.c_str(), npc->id, npc->x, npc->y);

				for (auto& c : Characters)
					if (c.second->can_see(*npc))
						c.second->send_add_player_packet(npc->id);
			}
			npc->npc_move();
		}
	}
}

void autosave_thread_func() {
	while (autosave_running) {
		std::this_thread::sleep_for(std::chrono::minutes(2)); // 2분마다 일괄 저장

		std::lock_guard<std::mutex> lock(m_autosave);
		printf("[오토DB세이브] 전체 세션/플레이어 DB 업데이트 시작\n");
		for (auto& kv : Characters) {
			auto& session = kv.second;
			database_manager.update_user_info(
				session->id,
				session->x,
				session->y,
				session->dir,
				session->max_hp,
				session->hp,
				session->level,
				session->exp,
				session->name
			);
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
			std::lock_guard<std::mutex> lock(m_work);

			if ((eo->io_type == RECV || eo->io_type == SEND) && (0 == io_size)) {
				if (Characters.count(key) != 0) {
					Characters[key]->on_logout();
					Characters.erase(key);
				}
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
				std::lock_guard<std::mutex> lock(m_work);
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
				std::lock_guard<std::mutex> lock(m_work);
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

	if (!database_manager.InitODBC_DB()) {
		cerr << "ODBC DB 초기화/연결 실패! 서버 실행 중단" << endl;
		return -1;
	}

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
	
	InitializeTileMap();
	InitializeObstacles();
	InitializeNPC();
	thread npc_thread(npc_thread_func);
	thread autosave_thread(autosave_thread_func);
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

	autosave_running = false;
	autosave_thread.join();

	for (auto& sess : Characters)
		sess.second->on_logout();
	closesocket(s_socket);
	WSACleanup();

	database_manager.CloseODBC_DB();
	return 0;
}