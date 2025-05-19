#include <atlimage.h> // cimage

#include <string>
#include <windows.h>
#include <iostream>
#include <unordered_map>

bool key[4] = { false, false, false, false };
constexpr char DOWN = 0;
constexpr char UP = 1;
constexpr char LEFT = 2;
constexpr char RIGHT = 3;

constexpr unsigned short MAP_H = 16;
constexpr unsigned short MAP_W = 16;

class Player {
public:
	short x;
	short y;
	int dir;
	int frame;

	Player() {
		x = 8;
		y = 8;
		frame = 0;
	}
};

Player players;

HWND hwndOutput;
HWND hWnd;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

HINSTANCE g_hInst;
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nCmdShow)
{
	WNDCLASS wc = { 0, WndProc, 0, 0, hInstance, 0, LoadCursor(0, IDC_ARROW),
					(HBRUSH)(COLOR_WINDOW + 1), 0, L"MMORPG" };
	RegisterClass(&wc);

	hWnd = CreateWindow(L"MMORPG", L"MMORPG", WS_OVERLAPPEDWINDOW, 0, 0, 800, 800, NULL, (HMENU)NULL, hInstance, NULL);
	ShowWindow(hWnd, nCmdShow);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return msg.wParam;
}

HFONT hFont = nullptr;
CImage Ninja, Background;
CImage backBuffer;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	float width = 800 / 16.0f; // 보드의 각 칸 크기
	HDC hDC;

	switch (uMsg) {
	case WM_CREATE: {
		HRESULT sol = Ninja.Load(L"img/Ninja.png");
		HRESULT bgroung = Background.Load(L"img/Grass2.png");

		backBuffer.Create(800, 800, 32);

		hFont = CreateFont(
			20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
			VARIABLE_PITCH, NULL);

		SetTimer(hWnd, 1, 100, FALSE);
		break;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		hDC = BeginPaint(hWnd, &ps);

		HDC memDC = backBuffer.GetDC();

		Background.Draw(memDC, 0, 0, 800, 800, 0, 0, 1024, 1024);

		SelectObject(memDC, hFont);
		SetTextColor(memDC, RGB(255, 255, 0));
		SetBkMode(memDC, TRANSPARENT);

		// 말 그리기
		Ninja.Draw(memDC, players.x * width, players.y * width,
			width, width, players.dir * 100, players.frame * 100, 100, 100);
		WCHAR szId[16];
		wsprintf(szId, L"%d", 1);
		TextOut(memDC, players.x * width, players.y * width - 20,
			szId, lstrlen(szId));

		BitBlt(hDC, 0, 0, 800, 800, memDC, 0, 0, SRCCOPY);
		// 메모리 DC에서 화면 DC로 복사
		backBuffer.ReleaseDC();

		EndPaint(hWnd, &ps);
		break;
	}
	case WM_KEYDOWN:
	{
		switch (wParam) {
		case VK_UP: case 'w': case'W':	 key[0] = true; players.dir = UP; break;
		case VK_LEFT: case'a': case'A':	 key[1] = true; players.dir = LEFT; break;
		case VK_DOWN: case's': case'S':	 key[2] = true; players.dir = DOWN; break;
		case VK_RIGHT: case'd': case'D': key[3] = true; players.dir = RIGHT; break;
		case 'Q': PostQuitMessage(0); break;
		default: break;
		}
		break;
	}
	case WM_KEYUP:
	{
		switch (wParam) {
		case VK_UP: case 'w': case'W':	 key[0] = false; break;
		case VK_LEFT: case'a': case'A':	 key[1] = false; break;
		case VK_DOWN: case's': case'S':	 key[2] = false; break;
		case VK_RIGHT: case'd': case'D': key[3] = false; break;
		case 'Q': PostQuitMessage(0); break;
		default: break;
		}
		break;
	}
	case WM_TIMER:
	{
		if (wParam == 1) {
			bool moved = false;
			if (key[0]) { --players.y; players.dir = UP; moved = true; }
			if (key[1]) { --players.x; players.dir = LEFT; moved = true; }
			if (key[2]) { ++players.y; players.dir = DOWN; moved = true; }
			if (key[3]) { ++players.x; players.dir = RIGHT; moved = true; }
			// 프레임 증가 (한 번에 하나 방향만 누른다고 가정)
			if (moved)
				players.frame = (players.frame + 1) % 4;
			else
				players.frame = 0;
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

		PostQuitMessage(0);
		break;
	}
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}