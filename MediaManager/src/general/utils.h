#pragma once
#include <string>
#include <map>
#include <QPixmap>
#include "config.h"
#include <QString>
#include <QtGlobal>
#include "windows.h"
#include <QList>
#include <algorithm>
#include <random>
#include <iterator>
#include <UIAutomationClient.h>
#include <Qt>
#include <QImage>
#include <chrono>
#include <QColor>
#include "colorPaletteExtractor.h"
#include <QStringList>

class MainApp;

namespace utils {
	QString pathAppend(const QString& path1, const QString& path2);
	QPixmap openImage(QString path);
	DWORD checkProcRunning(std::string procfilename);
	bool text_to_bool(std::string str);
	bool pid_exists(qint64 pid);
	std::string bool_to_text(bool b);
	QString bool_to_text_qt(bool b);
	std::string linux_to_windows_path(std::string& path);
	QString getVideoDuration(QString path);
	std::string formatSeconds(double total_seconds);
	QString formatSecondsQt(double total_seconds);
	bool hiddenCheck(QStringList &settings);
	int randint(int start,int stop);
	double randfloat(double start, double stop);
	QList<std::string> getFiles(std::string directory,bool recursive = false);
	QStringList getFilesQt(QString directory,bool recursive = false);
	QList<std::string> getDirs(std::string directory, bool recursive = false);
	QStringList getDirsQt(QString directory, bool recursive = false);
	QStringList getDirNames(QString path);
	static BOOL CALLBACK enumWindowCallback(HWND hWnd, LPARAM lparam);
	QList<HWND> get_hwnds_for_pid(qint64 pid);
	void bring_pid_to_foreground(qint64 pid, HWND hwnd = nullptr);
	bool is_pid_full_screen(qint64 pid, HWND hwnd = nullptr);
	bool bring_hwnd_to_foreground_uiautomation_method(IUIAutomation* uiAutomation,HWND hwnd);
	void bring_hwnd_to_foreground_attach_method(HWND hwnd);
	void bring_hwnd_to_foreground_alt_method(HWND hwnd);
	void bring_hwnd_to_foreground_console_method(HWND hwnd);
	void bring_hwnd_to_foreground(HWND hwnd);
	bool is_hwnd_full_screen(HWND hwnd);
	qreal volume_convert(int volume);
	QString getParentDirectoryName(QString path, int depth);
	double take_closest(QList<double> const& vec, int value);
	std::string convert_time_to_text(unsigned long int seconds);
	bool IsMyWindowCovered(QWidget* MyWindow);
	bool IsMyWindowVisible(QWidget* MyWindow);
	QColor blendColors(const QColor& color1, const QColor& color2, double ratio);
	void changeQImageHue(QImage& img, QColor& newcolor, double blend_ratio);
	std::string getAppId(const char* VERSION);
	bool isSingleInstanceRunning(QString appid);
	void openFileExplorer(QString path);
	void numlock_toggle_on();
	std::chrono::microseconds QueryUnbiasedInterruptTimeChrono();
	double get_luminance(QColor& color);
	QColor complementary_color(QColor& color);
	color_area& get_weighted_random_color(QList<color_area>& colors);

	template <typename T>
	inline void shuffle_qlist(QList<T>& list) {
		std::random_device seed;
		std::mt19937 gen{ seed() };
		std::shuffle(list.begin(), list.end(), gen);
	}
	template<typename Iter, typename RandomGenerator>
	Iter select_randomly(Iter start, Iter end, RandomGenerator& g) {
		std::uniform_int_distribution<> dis(0, std::distance(start, end) - 1);
		std::advance(start, dis(g));
		return start;
	}

	template<typename Iter>
	Iter select_randomly(Iter start, Iter end) {
		static std::random_device rd;
		static std::mt19937 gen(rd());
		return select_randomly(start, end, gen);
	}
}