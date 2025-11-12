#include "stdafx.h"
#include "RawInputKeyboard.h"
#include "utils.h"
#include <QDebug>
#include "MainApp.h"
#include "MainWindow.h"
#include "hidusage.h"
#include <QDateTime>

RawInputKeyboard::RawInputKeyboard(MainApp* App)
{
    this->App = App;
    WNDCLASS windowClass = {};
    windowClass.hInstance = (HINSTANCE)this->App->mainWindow->winId();
    windowClass.lpfnWndProc = WndProcWrapper;
    QDateTime date = QDateTime::currentDateTime();
    QString formattedTime = date.toString("hh_mm_ss");
    QString name = "RawInputKeyboardMM_" + formattedTime;
    this->windowClassName = new char[strlen(name.toUtf8().data()) + 1];
    strcpy(this->windowClassName, name.toUtf8().data());
    LPCSTR windowClassName = this->windowClassName;
    windowClass.lpszClassName = windowClassName;
    if (!RegisterClass(&windowClass)) {
        qDebug() << "Failed to register RawInputKeyboard window class.";
    }
    HWND messageWindow = CreateWindow(windowClassName, 0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, windowClass.hInstance, this);
    this->hwnd = messageWindow;
    if (!messageWindow) {
        qDebug() << "Failed to create message-only window";
    }
}

LRESULT CALLBACK RawInputKeyboard::WndProcWrapper(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RawInputKeyboard* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        pThis = static_cast<RawInputKeyboard*> ((reinterpret_cast<CREATESTRUCT*>(lParam))->lpCreateParams);
        SetLastError(0);
        if (!SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis)))
        {
            if (GetLastError() != 0)
            {
                qDebug() << "RawInputKeyboard window wrapper error.";
                return FALSE;
            }
        }
        RAWINPUTDEVICE rid;
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
        rid.usUsage = HID_USAGE_GENERIC_KEYBOARD;
        rid.hwndTarget = hWnd;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }
    else
    {
        pThis = reinterpret_cast<RawInputKeyboard*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }
    if (pThis != nullptr && msg == WM_INPUT)
    {
        return pThis->OnCopyData(hWnd, msg, wParam, lParam);
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT RawInputKeyboard::OnCopyData(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
		case WM_INPUT: {
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &this->dwSize, sizeof(RAWINPUTHEADER)) == -1) {
				break;
			}
			LPBYTE lpb = new BYTE[this->dwSize];
			if (lpb == NULL) {
				break;
			}
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &this->dwSize, sizeof(RAWINPUTHEADER)) != this->dwSize) {
				delete[] lpb;
				break;
			}
			PRAWINPUT raw = (PRAWINPUT)lpb;
			UINT Event;

			//StringCchPrintf(szOutput, STRSAFE_MAX_CCH, TEXT(" Kbd: make=%04x Flags:%04x Reserved:%04x ExtraInformation:%08x, msg=%04x VK=%04x \n"),
			//	raw->data.keyboard.MakeCode,
			//	raw->data.keyboard.Flags,
			//	raw->data.keyboard.Reserved,
			//	raw->data.keyboard.ExtraInformation,
			//	raw->data.keyboard.Message,
			//	raw->data.keyboard.VKey);
            
            //qDebug() << QStringLiteral("Kbd: make=%1 Flags=%2 Reserved=%3 ExtraInformation=%4, msg=%5 VK=%6")
            //    .arg(raw->data.keyboard.MakeCode, 4, 16, QChar('0'))
            //    .arg(raw->data.keyboard.Flags, 4, 16, QChar('0'))
            //    .arg(raw->data.keyboard.Reserved, 4, 16, QChar('0'))
            //    .arg(raw->data.keyboard.ExtraInformation, 8, 16, QChar('0'))
            //    .arg(raw->data.keyboard.Message, 4, 16, QChar('0'))
            //    .arg(raw->data.keyboard.VKey, 4, 16, QChar('0'));

			Event = raw->data.keyboard.Message;
			this->keyScanCode = MapVirtualKey(raw->data.keyboard.VKey, MAPVK_VK_TO_VSC);
			delete[] lpb;			// free this now

            // https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
            // https://www.freepascal.org/docs-html/rtl/keyboard/kbdscancode.html
            //qDebug().noquote() << QString::number(raw->data.keyboard.VKey, 16).rightJustified(2, '0').toUpper() << QString::number(this->keyScanCode, 16).rightJustified(2, '0').toUpper();
			// read key once on keydown event only
			if (Event == WM_KEYDOWN) {
                if ((this->App != nullptr && this->App->numlock_only_on == true && raw->data.keyboard.VKey == VK_CLEAR) or
                    (this->App != nullptr && this->App->numlock_only_on == false && this->keyScanCode == 76)) 
                {
                    auto elapsed = std::chrono::steady_clock::now() - this->cooldown_start;
                    if (elapsed >= this->cooldown_duration) {
                        this->App->VW->toggle_window();
                        this->cooldown_start = std::chrono::steady_clock::now();
                    }
                }
                if (this->App != nullptr && this->App->numlock_only_on == true && raw->data.keyboard.VKey == VK_F8) {
                    if(this->App->mainWindow->iconWatchingState)
                        this->App->mainWindow->playSpecialSoundEffect(true);
                }
			}
            break;
		}
		case WM_CLOSE:
			PostQuitMessage(0);
			break;
		default:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void RawInputKeyboard::closeWindow()
{
    SendMessageTimeout(this->hwnd, WM_CLOSE, 0, 0, 0, 1, NULL);
}

RawInputKeyboard::~RawInputKeyboard()
{
    this->closeWindow();
    UnregisterClass(this->windowClassName, NULL);
    delete[] this->windowClassName;
}

