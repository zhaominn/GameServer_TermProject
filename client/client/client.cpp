#include <atlimage.h> // cimage

#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>

#include <WS2tcpip.h>

#include <process.h> // 랜덤 이름
#pragma comment (lib, "WS2_32.LIB")

#include "..\..\server\server\game_header.h"
using namespace std;

class Character {
public:
	short x;
	short y;
	int dir;
	int frame;
	bool can_see;
	char name[MAX_ID_LENGTH]{};
	long long id{};

	Character() {
		dir = MOVE_DOWN;
		frame = 0;
		can_see = false;
	}

};

SOCKET my_socket;
bool key[4] = { false, false, false, false };
Character player;
unordered_map<INT, Character> Characters;

atomic<bool> network_running{ true };
thread network_thread;
// mutex m_network;

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
	sprintf_s(player.name, "player_%d", _getpid()); // 임시 이름
	strncpy_s(packet.name, sizeof(packet.name), player.name, _TRUNCATE);

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
		player.can_see = true;
	}
	break;
	case S2C_P_ENTER:
	{
		sc_packet_enter* packet = reinterpret_cast<sc_packet_enter*>(ptr);
		int id = packet->id;

		if (id < MAX_USER) {
			Characters[id].x = packet->x;
			Characters[id].y = packet->y;
			Characters[id].id = packet->id;
			strncpy_s(Characters[id].name, packet->name, MAX_ID_LENGTH);
			Characters[id].name[MAX_ID_LENGTH - 1] = '\0';
			Characters[id].can_see = true;;
		}
		break;
	}
	case S2C_P_MOVE:
	{
		sc_packet_move* packet = reinterpret_cast<sc_packet_move*>(ptr);
		int other_id = packet->id;

		if (other_id < MAX_USER) {
			Characters[other_id].x = packet->x;
			Characters[other_id].y = packet->y;
			if (player.id == packet->id) {
				player.x = packet->x;
				player.y = packet->y;
			}
		}
		break;
	}

	case S2C_P_LEAVE:
	{
		sc_packet_leave* packet = reinterpret_cast<sc_packet_leave*>(ptr);
		int other_id = packet->id;
		if (other_id < MAX_USER) {
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
	logIn();
	network_thread = std::thread(network_thread_func);

	WNDCLASS wc = { 0, WndProc, 0, 0, hInstance, 0, LoadCursor(0, IDC_ARROW),
	(HBRUSH)(COLOR_WINDOW + 1), 0, L"MMORPG" };
	RegisterClass(&wc);

	hWnd = CreateWindow(L"MMORPG", L"MMORPG", WS_OVERLAPPEDWINDOW, 0, 0, WINDOW_SIZE+15, WINDOW_SIZE, NULL, (HMENU)NULL, hInstance, NULL);
	ShowWindow(hWnd, nCmdShow);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.wParam;
}

CImage Ninja, Background;
CImage backBuffer;
HFONT hFont;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	HDC hDC, memDC;
		PAINTSTRUCT ps;
		RECT rc = { 0, 0, WINDOW_SIZE, WINDOW_SIZE };
		HBRUSH hBrush;

	switch (uMsg) {
	case WM_CREATE: {
		Ninja.Load(L"img/Ninja.png");
		Background.Load(L"img/Grass2.png");

		backBuffer.Create(WINDOW_SIZE, WINDOW_SIZE, 32);

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
		SetTextColor(memDC, RGB(255, 255, 0));
		SetBkMode(memDC, TRANSPARENT);

		hBrush = CreateSolidBrush(RGB(0, 0, 0));
		FillRect(memDC, &rc, hBrush);
		DeleteObject(hBrush);

		drawBackground(Background, memDC);

		Ninja.Draw(memDC, TILE_SIZE * WINDOW_CENTER, TILE_SIZE * WINDOW_CENTER,
			TILE_SIZE, TILE_SIZE, (player.dir - 1) * TILE_SIZE, player.frame * TILE_SIZE, TILE_SIZE, TILE_SIZE);
		WCHAR szId[16];
		swprintf(szId, 32, L"%d", player.id);
		TextOut(memDC, TILE_SIZE * WINDOW_CENTER, TILE_SIZE * WINDOW_CENTER - 20, szId, wcslen(szId));

		for (auto& p : Characters) {
			const Character& ch = p.second;
			if (!ch.can_see) continue;

			int offset_x = ch.x - player.x;
			int offset_y = ch.y - player.y;
			int draw_x = TILE_SIZE * (WINDOW_CENTER + offset_x);
			int draw_y = TILE_SIZE * (WINDOW_CENTER + offset_y);

			Ninja.Draw(memDC, draw_x, draw_y, TILE_SIZE, TILE_SIZE,
				(ch.dir - 1) * TILE_SIZE, ch.frame * TILE_SIZE, TILE_SIZE, TILE_SIZE);

			swprintf(szId, 32, L"%d", ch.id);
			TextOut(memDC, draw_x, draw_y - 20, szId, wcslen(szId));
		}

		BitBlt(hDC, 0, 0, WINDOW_SIZE, WINDOW_SIZE, memDC, 0, 0, SRCCOPY);
		backBuffer.ReleaseDC();

		EndPaint(hWnd, &ps);
		break;
	}
	case WM_KEYDOWN:
	{
		switch (wParam) {
		case VK_UP: case 'w': case'W': key[0] = true; player.dir = MOVE_UP; break;
		case VK_LEFT: case'a': case'A': key[1] = true; player.dir = MOVE_LEFT; break;
		case VK_DOWN: case's': case'S': key[2] = true; player.dir = MOVE_DOWN; break;
		case VK_RIGHT: case'd': case'D': key[3] = true; player.dir = MOVE_RIGHT; break;
		case 'Q': PostQuitMessage(0); break;
		default: break;
		}
		break;
	}
	case WM_KEYUP:
	{
		switch (wParam) {
		case VK_UP: case 'w': case'W': key[0] = false; break;
		case VK_LEFT: case'a': case'A': key[1] = false; break;
		case VK_DOWN: case's': case'S': key[2] = false; break;
		case VK_RIGHT: case'd': case'D': key[3] = false; break;
		case 'Q': PostQuitMessage(0); break;
		default: break;
		}
		break;
	}
	case WM_TIMER:
	{
		if (wParam == 1) {
			static auto last_move_time = chrono::steady_clock::now();

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
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;
	}
	case WM_DESTROY:
	{
		if (!Background.IsNull()) Background.Destroy();
		if (!Ninja.IsNull()) Ninja.Destroy();
		if (!backBuffer.IsNull()) backBuffer.Destroy();

		if (hFont) { DeleteObject(hFont); hFont = nullptr; }

		network_running = false;
		if (network_thread.joinable())
			network_thread.join();

		closesocket(my_socket);
		WSACleanup();
		PostQuitMessage(0);
		break;
	}
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}