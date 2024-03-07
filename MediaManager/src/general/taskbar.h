#pragma once
#include "shobjidl_core.h"

class Taskbar
{
public:
	HWND hwnd;
	ITaskbarList3* ITaskbarList = nullptr;
	Taskbar(HWND hwnd);
	void setBusy(HWND hwnd, bool busy);
	void setPause(HWND hwnd, bool pause);
	void setProgress(HWND hwnd, ULONGLONG done, ULONGLONG total);
	void setState(HWND hwnd, TBPFLAG state);
	void Stop(HWND hwnd);
	void Flash(HWND hwnd);
	~Taskbar();
};

