#include <atlimage.h> // cimage

#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>

#include <WS2tcpip.h>

#include <process.h> // 랜덤 이름
#pragma comment (lib, "WS2_32.LIB")

#include "..\..\server\server\game_header.h"
using namespace std;

bool make_id = false;
char id_buffer[MAX_ID_LENGTH] = "";
int id_len = 0;

class Character {
public:
	long long id{};
	short x;
	short y;
	short hp;
	short level;
	int   exp;
	int death_draw_tick;
	char npc_type;

	int dir;
	int frame;
	bool can_see;
	char name[MAX_ID_LENGTH]{};

	Character() {
		dir = MOVE_DOWN;
		frame = 0;
		can_see = false;
		death_draw_tick = 0;
	}

};

SOCKET my_socket;
bool key[4] = { false, false, false, false };
bool attack = false;
Character player;

unordered_map<INT, Character> Characters;
unordered_map<INT, Character> npcs;

atomic<bool> network_running{ true };
thread network_thread;

class Obstacle {
public:
	int id;
	int x, y;
	int rock_num;
	bool can_see;

	Obstacle() {};
};

unordered_map<INT, Obstacle> obstacles;

void send_packet(void* packet)
{
	unsigned char* p = reinterpret_cast<unsigned char*>(packet);
	size_t sent = 0;
	send(my_socket, (const char*)packet, p[0], 0);
}

void Initialize_socket()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
	my_socket = socket(AF_INET, SOCK_STREAM, 0);

	SOCKADDR_IN server_addr;
	ZeroMemory(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(GAME_PORT);
	inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
	connect(my_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
}

void logIn() {
	cs_packet_login packet;
	packet.size = sizeof(packet);
	packet.type = C2S_P_LOGIN;
	strncpy_s(packet.name, sizeof(packet.name), player.name, _TRUNCATE);
	send_packet(&packet);
}

void reborn() {
	player.x = rand() % MAP_WIDTH;
	player.y = rand() % MAP_HEIGHT;

	// 2. HP 회복 (최대 체력으로)
	player.hp = PLAYER_MAX_HP;

	// 3. 경험치 반감
	player.exp /= 2;

	cs_packet_reborn packet;
	packet.size = sizeof(packet);
	packet.type = C2S_P_REVIVE;
	packet.x = player.x;
	packet.y = player.y;
	send_packet(&packet);

}

void process_packet(char* ptr)
{
	switch (ptr[1])
	{
	case S2C_P_AVATAR_INFO:
	{
		sc_packet_avatar_info* packet = reinterpret_cast<sc_packet_avatar_info*>(ptr);
		player.id = packet->id;
		player.x = packet->x;
		player.y = packet->y;
		player.hp = packet->hp;
		player.level = packet->level;
		player.exp = packet->exp;
		player.can_see = true;
		break;
	}
	case S2C_P_ENTER:
	{
		sc_packet_enter* packet = reinterpret_cast<sc_packet_enter*>(ptr);
		int id = packet->id;

		if (id == player.id) {
			// 내 캐릭터는 Characters에 안 넣고 player에만!
			player.x = packet->x;
			player.y = packet->y;
			strncpy_s(player.name, packet->name, MAX_ID_LENGTH);
			player.name[MAX_ID_LENGTH - 1] = '\0';
			player.can_see = true;
		}
		else if (packet->o_type == 1) { // PEACE MONSTER
			npcs[id].x = packet->x;
			npcs[id].y = packet->y;
			npcs[id].id = id;
			strncpy_s(npcs[id].name, packet->name, MAX_ID_LENGTH);
			npcs[id].name[MAX_ID_LENGTH - 1] = '\0';
			npcs[id].can_see = true;
			npcs[id].npc_type = 1;
		}
		else if (packet->o_type == 2) { // AGRO MONSTER
			npcs[id].x = packet->x;
			npcs[id].y = packet->y;
			npcs[id].id = id;
			strncpy_s(npcs[id].name, packet->name, MAX_ID_LENGTH);
			npcs[id].name[MAX_ID_LENGTH - 1] = '\0';
			npcs[id].can_see = true;
			npcs[id].npc_type = 2;
		}
		else if (packet->o_type == 3) { // 장애물!
			obstacles[id].id = id;
			obstacles[id].x = packet->x;
			obstacles[id].y = packet->y;
			obstacles[id].rock_num = packet->rock_num;
			obstacles[id].can_see = true;
		}
		else {
			Characters[id].x = packet->x;
			Characters[id].y = packet->y;
			Characters[id].id = id;
			strncpy_s(Characters[id].name, packet->name, MAX_ID_LENGTH);
			Characters[id].name[MAX_ID_LENGTH - 1] = '\0';
			Characters[id].can_see = true;
		}
		break;
	}
	case S2C_P_MOVE:
	{
		sc_packet_move* packet = reinterpret_cast<sc_packet_move*>(ptr);
		int other_id = packet->id;
		char dir = packet->dir;

		if (other_id == player.id) {
			player.x = packet->x;
			player.y = packet->y;
			player.dir = packet->dir;
		}
		else if (npcs.count(other_id)) {  // NPC 이동
			npcs[other_id].frame = (npcs[other_id].frame + 1) % 4;
			npcs[other_id].x = packet->x;
			npcs[other_id].y = packet->y;
			npcs[other_id].dir = packet->dir;
		}
		else if (Characters.count(other_id)) { // 플레이어 이동
			Characters[other_id].frame = (Characters[other_id].frame + 1) % 4;
			Characters[other_id].x = packet->x;
			Characters[other_id].y = packet->y;
			Characters[other_id].dir = packet->dir;
		}
		break;
	}
	case S2C_P_STAT_CHANGE: {
		sc_packet_stat_change* pkt = reinterpret_cast<sc_packet_stat_change*>(ptr);
		long long id = pkt->id;
		short   hp = pkt->hp;
		short   level = pkt->level;
		int     exp = pkt->exp;

		if (id == player.id) {
			player.hp = hp;
			player.level = level;
			player.exp = exp;

			if (player.hp <= 0) {
				reborn();
			}
		}
		else if (npcs.count(id)) {
			npcs[id].hp = hp;
			npcs[id].level = level;
			npcs[id].exp = exp;
			if (npcs[id].hp <= 0) {
				npcs[id].frame = 6;
				npcs[id].dir = 2;
			}
		}
		else if (Characters.count(id)) {
			Characters[id].hp = hp;
			Characters[id].level = level;
			Characters[id].exp = exp;
			if (Characters[id].hp <= 0) {
				Characters[id].frame = 6;
				Characters[id].dir = 2;
			}
		}
		break;
	}
	case S2C_P_LEAVE:
	{
		sc_packet_leave* packet = reinterpret_cast<sc_packet_leave*>(ptr);
		int other_id = packet->id;
		if (other_id == player.id) {
			player.can_see = false;
		}
		else if (obstacles.count(other_id)) { // 장애물 사라짐
			obstacles[other_id].can_see = false;
		}
		else if (npcs.count(other_id)) { // NPC 나감
			npcs[other_id].can_see = false;
		}
		else if (Characters.count(other_id)) { // 플레이어 나감
			Characters[other_id].can_see = false;
		}
		break;
	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[MAX_CHAT_LENGTH];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			process_packet(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void network_thread_func() {
	char net_buf[1024];
	while (network_running) {
		int ret = recv(my_socket, net_buf, 1024, 0); // blocking
		if (ret > 0) {
			process_data(net_buf, ret);
		}
		else if (ret == 0) {
			network_running = false;
		}
	}
	printf("network thread exit\n");
}

void drawBackground(CImage& img, HDC hdc) {
	int center_tile_x = player.x;
	int center_tile_y = player.y;

	for (int dy = -WINDOW_CENTER; dy <= WINDOW_CENTER; ++dy) {
		for (int dx = -WINDOW_CENTER; dx <= WINDOW_CENTER; ++dx) {
			int map_x = center_tile_x + dx;
			int map_y = center_tile_y + dy;
			if (map_x < 0 || map_x >= MAP_WIDTH || map_y < 0 || map_y >= MAP_HEIGHT) continue;
			int draw_x = (dx + WINDOW_CENTER) * TILE_SIZE;
			int draw_y = (dy + WINDOW_CENTER) * TILE_SIZE;

			int tile_offset_x = (map_x * TILE_SIZE) % 2000;
			if (tile_offset_x < 0) tile_offset_x += 2000;
			int tile_offset_y = (map_y * TILE_SIZE) % 2000;
			if (tile_offset_y < 0) tile_offset_y += 2000;
			img.Draw(
				hdc, draw_x, draw_y, TILE_SIZE, TILE_SIZE,
				tile_offset_x, tile_offset_y, TILE_SIZE, TILE_SIZE
			);
		}
	}
}

HWND hWnd;
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

HINSTANCE g_hInst;
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nCmdShow)
{
	Initialize_socket();
	network_thread = std::thread(network_thread_func);

	WNDCLASS wc = { 0, WndProc, 0, 0, hInstance, 0, LoadCursor(0, IDC_ARROW),
	(HBRUSH)(COLOR_WINDOW + 1), 0, L"MMORPG" };
	RegisterClass(&wc);

	hWnd = CreateWindow(L"MMORPG", L"MMORPG", WS_OVERLAPPEDWINDOW, 0, 0,
		TOTAL_WIN_W, WINDOW_SIZE, NULL, (HMENU)NULL, hInstance, NULL);
	ShowWindow(hWnd, nCmdShow);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return msg.wParam;
}

CImage Ninja, NinjaDark, NinjaRed, Background, Rock;
CImage backBuffer;
HFONT hFont;
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	HDC hDC, memDC;
	PAINTSTRUCT ps;
	RECT rc = { 0, 0, TOTAL_WIN_W, WINDOW_SIZE };
	HBRUSH hBrush;

	switch (uMsg) {
	case WM_CREATE: {
		Ninja.Load(L"img/Ninja.png");
		NinjaDark.Load(L"img/NinjaDark.png");
		NinjaRed.Load(L"img/NinjaRed.png");
		Background.Load(L"img/Grass2.png");
		Rock.Load(L"img/Rock.png");

		backBuffer.Create(TOTAL_WIN_W, WINDOW_SIZE, 32);

		hFont = CreateFont(
			20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			VARIABLE_PITCH, NULL);

		SetTimer(hWnd, 1, 100, FALSE);
		break;
	}
	case WM_PAINT: {
		hDC = BeginPaint(hWnd, &ps);

		memDC = backBuffer.GetDC();
		SelectObject(memDC, hFont);
		SetTextColor(memDC, RGB(0, 0, 0));
		SetBkMode(memDC, TRANSPARENT);

		hBrush = CreateSolidBrush(RGB(255, 255, 255));
		FillRect(memDC, &rc, hBrush);
		DeleteObject(hBrush);

		if (make_id) {
			drawBackground(Background, memDC);

			Ninja.Draw(memDC, TILE_SIZE * WINDOW_CENTER, TILE_SIZE * WINDOW_CENTER, TILE_SIZE, TILE_SIZE,
				(player.dir - 1) * TILE_SIZE, player.frame * TILE_SIZE, TILE_SIZE, TILE_SIZE);

			wchar_t szName[32];
			MultiByteToWideChar(CP_ACP, 0, player.name, -1, szName, 32);
			TextOut(memDC, TILE_SIZE * WINDOW_CENTER, TILE_SIZE * WINDOW_CENTER - 20, szName, wcslen(szName));

			for (auto& p : Characters) {
				const Character& ch = p.second;
				if (!ch.can_see) continue;

				int offset_x = ch.x - player.x;
				int offset_y = ch.y - player.y;
				int draw_x = TILE_SIZE * (WINDOW_CENTER + offset_x);
				int draw_y = TILE_SIZE * (WINDOW_CENTER + offset_y);

				NinjaDark.Draw(memDC, draw_x, draw_y, TILE_SIZE, TILE_SIZE,
					(ch.dir - 1) * TILE_SIZE, ch.frame * TILE_SIZE, TILE_SIZE, TILE_SIZE);

				MultiByteToWideChar(CP_ACP, 0, ch.name, -1, szName, 32);
				TextOut(memDC, draw_x, draw_y - 20, szName, wcslen(szName));
			}

			for (auto& p : npcs) {
				const Character& ch = p.second;
				if (!ch.can_see) continue;

				int offset_x = ch.x - player.x;
				int offset_y = ch.y - player.y;
				int draw_x = TILE_SIZE * (WINDOW_CENTER + offset_x);
				int draw_y = TILE_SIZE * (WINDOW_CENTER + offset_y);

				if (ch.npc_type == 1)
					NinjaDark.Draw(memDC, draw_x, draw_y, TILE_SIZE, TILE_SIZE,
						(ch.dir - 1) * TILE_SIZE, ch.frame * TILE_SIZE, TILE_SIZE, TILE_SIZE);
				else if (ch.npc_type == 2)
					NinjaRed.Draw(memDC, draw_x, draw_y, TILE_SIZE, TILE_SIZE,
						(ch.dir - 1) * TILE_SIZE, ch.frame * TILE_SIZE, TILE_SIZE, TILE_SIZE);

				MultiByteToWideChar(CP_ACP, 0, ch.name, -1, szName, 32);
				TextOut(memDC, draw_x, draw_y - 20, szName, wcslen(szName));
			}

			for (auto& obs : obstacles) {
				if (!obs.second.can_see) continue;
				int offset_x = obs.second.x - player.x;
				int offset_y = obs.second.y - player.y;
				int draw_x = TILE_SIZE * (WINDOW_CENTER + offset_x);
				int draw_y = TILE_SIZE * (WINDOW_CENTER + offset_y);

				Rock.Draw(memDC, draw_x, draw_y, TILE_SIZE, TILE_SIZE,
					0, 0 * TILE_SIZE, TILE_SIZE, TILE_SIZE);
			}

			//---------- Draw UI ----------

			SetTextColor(memDC, RGB(0, 0, 0));

			WCHAR buf[128];
			swprintf(buf, 128, L"ID    : %d", player.id);
			TextOut(memDC, UI_LEFT, UI_TOP + UI_LINE * 0, buf, wcslen(buf));
			swprintf(buf, 128, L"X     : %d", player.x);
			TextOut(memDC, UI_LEFT, UI_TOP + UI_LINE * 1, buf, wcslen(buf));
			swprintf(buf, 128, L"Y     : %d", player.y);
			TextOut(memDC, UI_LEFT, UI_TOP + UI_LINE * 2, buf, wcslen(buf));
			swprintf(buf, 128, L"LEVEL : %d", player.level);
			TextOut(memDC, UI_LEFT, UI_TOP + UI_LINE * 3, buf, wcslen(buf));
			swprintf(buf, 128, L"EXP   : %d", player.exp);
			TextOut(memDC, UI_LEFT, UI_TOP + UI_LINE * 4, buf, wcslen(buf));

			// EXP 바(Rectangle/FillRect)
			int bar_left = UI_LEFT;
			int bar_top = UI_TOP + UI_LINE * 5;
			int bar_width = 120;
			int max_exp = player.level * 100;
			int exp_w = (player.exp * bar_width) / max_exp;


			// 배경(빈 칸)
			HBRUSH bg = CreateSolidBrush(RGB(200, 200, 200));
			Rectangle(memDC, bar_left - 2, bar_top - 1, bar_left + bar_width + 2, bar_top + 13);
			DeleteObject(bg);

			// EXP 차오른 부분
			HBRUSH exp_brush = CreateSolidBrush(RGB(255, 100, 0));
			RECT r_exp = { bar_left, bar_top, bar_left + exp_w, bar_top + 11 };
			FillRect(memDC, &r_exp, exp_brush);
			DeleteObject(exp_brush);

			swprintf(buf, 128, L"HP   : %d", player.hp);
			TextOut(memDC, UI_LEFT, UI_TOP + UI_LINE * 6, buf, wcslen(buf));

			// HP 바(Rectangle/FillRect)
			bar_top = UI_TOP + UI_LINE * 7;
			int hp_w = (player.hp > PLAYER_MAX_HP) ? bar_width : player.hp * bar_width / PLAYER_MAX_HP;

			// 배경(빈 칸)
			Rectangle(memDC, bar_left - 2, bar_top - 1, bar_left + bar_width + 2, bar_top + 13);
			DeleteObject(bg);

			// HP 차오른 부분
			HBRUSH hp_brush = CreateSolidBrush(RGB(255, 0, 0));
			RECT r_hp = { bar_left, bar_top, bar_left + hp_w, bar_top + 11 };
			FillRect(memDC, &r_hp, hp_brush);
			DeleteObject(hp_brush);


		}
		else {
			RECT r = { 50, 220, 400, 260 };
			SetBkMode(memDC, TRANSPARENT);
			SetTextColor(memDC, RGB(255, 0, 0));
			DrawTextA(memDC, "아이디를 입력하세요:", -1, &r, DT_LEFT | DT_SINGLELINE);

			RECT r2 = { 50, 250, 400, 280 };
			DrawTextA(memDC, id_buffer, -1, &r2, DT_LEFT | DT_SINGLELINE);
		}

		BitBlt(hDC, 0, 0, TOTAL_WIN_W, WINDOW_SIZE, memDC, 0, 0, SRCCOPY);
		backBuffer.ReleaseDC();

		EndPaint(hWnd, &ps);
		break;
	}
	case WM_CHAR:
		if (!make_id) {
			if (wParam == VK_BACK && id_len > 0) {
				id_buffer[--id_len] = '\0';
			}
			else if (wParam == VK_RETURN) {
				if (id_len == 0)
					sprintf_s(player.name, "player_%d", _getpid());
				else {
					strncpy_s(player.name, sizeof(player.name), id_buffer, _TRUNCATE);
					player.name[sizeof(player.name) - 1] = '\0';
				}
				logIn();
				make_id = true;
				id_len = 0; id_buffer[0] = '\0';
			}
			else if (id_len < MAX_ID_LENGTH - 1 && isprint((char)wParam)) {
				id_buffer[id_len++] = (char)wParam;
				id_buffer[id_len] = '\0';
			}
			InvalidateRect(hWnd, NULL, FALSE);
			break;
		}
		break;
	case WM_KEYDOWN:
	{
		if (make_id) {
			switch (wParam) {
			case VK_UP: case 'w': case'W': key[0] = true; player.dir = MOVE_UP; break;
			case VK_LEFT: case'a': case'A': key[1] = true; player.dir = MOVE_LEFT; break;
			case VK_DOWN: case's': case'S': key[2] = true; player.dir = MOVE_DOWN; break;
			case VK_RIGHT: case'd': case'D': key[3] = true; player.dir = MOVE_RIGHT; break;
			case 'r': case'R': attack = true; break;
			case 'Q': PostQuitMessage(0); break;
			default: break;
			}
		}
		break;
	}
	case WM_KEYUP:
	{
		if (make_id) {
			switch (wParam) {
			case VK_UP: case 'w': case'W': key[0] = false; break;
			case VK_LEFT: case'a': case'A': key[1] = false; break;
			case VK_DOWN: case's': case'S': key[2] = false; break;
			case VK_RIGHT: case'd': case'D': key[3] = false; break;
			case 'r': case'R': attack = false; break;
			case 'Q': PostQuitMessage(0); break;
			default: break;
			}
		}
		break;
	}
	case WM_TIMER:
	{
		if (wParam == 1 && make_id) {
			static auto last_move_time = chrono::steady_clock::now();
			static auto last_attack_time = chrono::steady_clock::now();


			if ((key[0] || key[1] || key[2] || key[3])
				&& (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - last_move_time).count() >= MOVE_DELAY_MS)) {
				player.frame = (player.frame + 1) % 4;
				cs_packet_move p;
				p.size = sizeof(p);
				p.type = C2S_P_MOVE;
				p.direction = player.dir;
				send_packet(&p);

				last_move_time = chrono::steady_clock::now();
			}
			if (attack
				&& (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - last_attack_time).count() >= ATTACK_DELAY_MS)) {
				player.frame = (player.frame + 1) % 2 + 4;
				cs_packet_attack p;
				p.size = sizeof(p);
				p.type = C2S_P_ATTACK;
				send_packet(&p);

				last_attack_time = chrono::steady_clock::now();
			}
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;
	}
	case WM_DESTROY:
	{
		if (!Background.IsNull()) Background.Destroy();
		if (!Ninja.IsNull()) Ninja.Destroy();
		if (!NinjaDark.IsNull()) NinjaDark.Destroy();
		if (!NinjaRed.IsNull()) NinjaRed.Destroy();
		if (!backBuffer.IsNull()) backBuffer.Destroy();

		if (hFont) { DeleteObject(hFont); hFont = nullptr; }

		closesocket(my_socket);
		network_running = false;
		if (network_thread.joinable())
			network_thread.join();

		WSACleanup();
		PostQuitMessage(0);
		break;
	}
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}