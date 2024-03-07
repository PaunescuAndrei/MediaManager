#include "stdafx.h"
#include "taskbar.h"
#include <QtGlobal>

Taskbar::Taskbar(HWND hwnd) {
	this->hwnd = hwnd;
    HRESULT hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,IID_ITaskbarList3, (void**)&this->ITaskbarList);
	if (SUCCEEDED(hr))
	{
		HRESULT hr2 = this->ITaskbarList->HrInit();
		if (SUCCEEDED(hr2))
		{
			/*qDebug("ITaskbarList3::HrInit() succeeded.");*/
			//this->ITaskbarList->ActivateTab(hwnd);
		}
		else
		{
			this->ITaskbarList->Release();
			qWarning("ITaskbarList3::HrInit() has failed.");
		}
	}
	else
	{
		qWarning("ITaskbarList3 could not be created.");
	}
}

void Taskbar::setBusy(HWND hwnd, bool busy)
{
	if (this->ITaskbarList != nullptr) {
		if (busy)
			this->ITaskbarList->SetProgressState(hwnd, TBPF_INDETERMINATE);
		else
			this->ITaskbarList->SetProgressState(hwnd, TBPF_NOPROGRESS);
	}
}

void Taskbar::setPause(HWND hwnd, bool pause)
{
	if (this->ITaskbarList != nullptr) {
		if (pause)
			this->ITaskbarList->SetProgressState(hwnd, TBPF_PAUSED);
		else
			this->ITaskbarList->SetProgressState(hwnd, TBPF_NORMAL);
	}
}

void Taskbar::setProgress(HWND hwnd, ULONGLONG done, ULONGLONG total)
{
	if (this->ITaskbarList != nullptr) {
		this->ITaskbarList->SetProgressValue(hwnd, done, total);
	}
}

void Taskbar::setState(HWND hwnd, TBPFLAG state)
{
	if (this->ITaskbarList != nullptr) {
		this->ITaskbarList->SetProgressState(hwnd, state);
	}
}

void Taskbar::Stop(HWND hwnd)
{
	if (this->ITaskbarList != nullptr) {
		this->ITaskbarList->SetProgressState(hwnd, TBPF_NOPROGRESS);
	}
}

void Taskbar::Flash(HWND hwnd)
{
	FlashWindow(hwnd, true);
}

Taskbar::~Taskbar()
{
	if(this->ITaskbarList != nullptr)
		this->ITaskbarList->Release();
}
