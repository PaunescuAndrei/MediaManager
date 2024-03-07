#pragma once
#include <windows.h>
#include <chrono>

class MainApp;

class RawInputKeyboard
{
public:
	MainApp* App = nullptr;
	HWND hwnd = HWND();
	char* windowClassName;
	UINT dwSize = 0;
	WCHAR keyScanCode = 0;
	std::chrono::time_point<std::chrono::steady_clock> cooldown_start = std::chrono::steady_clock::now();
	std::chrono::milliseconds cooldown_duration = std::chrono::milliseconds(100);
	RawInputKeyboard(MainApp* App);
	static LRESULT CALLBACK WndProcWrapper(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnCopyData(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void closeWindow();
	~RawInputKeyboard();
};

