#include <atlimage.h> // cimage

#include <string>
#include <windows.h>
#include <iostream>
#include <unordered_map>

class Player {
public:
	short x;
	short y;
	int frame;

	Player() {
		x = 4;
		y = 4;
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
CImage Soldier, Background;
CImage backBuffer;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	float width = 800 / 8.0f; // 보드의 각 칸 크기
	HDC hDC;

	switch (uMsg) {
	case WM_CREATE: {
		HRESULT sol = Soldier.Load(L"img/Soldier.png");
		if (FAILED(sol)) MessageBox(0, L"Soldier.png 로드 실패!", 0, 0);
		HRESULT bgroung = Background.Load(L"img/Grass2.png");
		if (FAILED(bgroung)) MessageBox(0, L"Grass.png 로드 실패!", 0, 0);

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
		Soldier.Draw(memDC, players.x * width, players.y * width,
			width, width, players.frame * 100, 0, 100, 100);
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
		case VK_UP: case 'w': case'W': --players.y; break;
		case VK_LEFT: case'a': case'A': --players.x; break;
		case VK_DOWN: case's': case'S': ++players.y; break;
		case VK_RIGHT: case'd': case'D': ++players.x; break;
		case 'Q': PostQuitMessage(0); break;
		default: break;
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;
	}
	case WM_TIMER:
	{
		if (wParam == 1) {
			players.frame = (players.frame + 1) % 6;
		}
		InvalidateRect(hWnd, NULL, FALSE);
		break;
	}
	case WM_DESTROY:
	{
		if (!Background.IsNull()) Background.Destroy();
		if (!Soldier.IsNull()) Soldier.Destroy();
		if (!backBuffer.IsNull()) backBuffer.Destroy();

		if (hFont) { DeleteObject(hFont); hFont = nullptr; }

		PostQuitMessage(0);
		break;
	}
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}