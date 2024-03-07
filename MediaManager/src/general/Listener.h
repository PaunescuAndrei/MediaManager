#pragma once
#include <string>
#include <windows.h>
#include <QtGlobal>
#include "MpcApi.h"
#include <QProcess>

class MainApp;

class Listener
{
public:
	MainApp* App = nullptr;
	HWND mpchc_hwnd = HWND();
	HWND hwnd = HWND();
	QString path = "";
	qint64 pid = -1;
	double currentPosition = -1;
	double duration = -1;
	bool first_getcurrentpositon = true;
	bool change_in_progress = false;
	bool seek_done = false;
	int state = 2;
	int* CLASS_COUNT;
	bool endvideo = false;
	char* windowClassName;
	QProcess *process;
	Listener(QString path,int *CLASS_COUNT, QObject *parent = nullptr, MainApp* App = nullptr);
	static LRESULT CALLBACK WndProcWrapper(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT OnCopyData(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	LRESULT send_osd_message(QString message, int duration);
	LRESULT send_message(MPCAPI_COMMAND command, QString data = "", bool blocking = false);
	bool is_process_alive();
	void closeWindow();
	void change_path(QString path);
	void change_video(QString path, double position = 0);
	~Listener();
};

