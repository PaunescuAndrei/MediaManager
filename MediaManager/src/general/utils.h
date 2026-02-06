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
#include <definitions.h>
#include <QSize>
#include <initializer_list>
#include <vector>

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
	bool readAudioFile(const QString& path, std::vector<float>& audioData, int& sampleRate, int& channels, const std::atomic<bool>* cancelFlag = nullptr);
	std::string formatSeconds(double total_seconds, bool showHours = true);
	QString formatSecondsQt(double total_seconds, bool showHours = true);
	std::string formatSecondsCompact(double total_seconds);
	QString formatSecondsCompactQt(double total_seconds);
	bool hiddenCheck(QStringList &settings);
	int randint(int start,int stop, quint32 seed = 0);
	double randfloat(double start, double stop, quint32 seed = 0);
	QList<std::string> getFiles(std::string directory,bool recursive = false, bool relative_path = false);
	QStringList getFilesQt(QString directory,bool recursive = false, bool relative_path = false);
	QList<std::string> getDirs(std::string directory, bool recursive = false);
	QStringList getDirsQt(QString directory, bool recursive = false);
	QStringList getDirNames(QString path);
	QString getRootPath(const QString &pathStr);
	static BOOL CALLBACK enumWindowCallback(HWND hWnd, LPARAM lparam);
	QList<HWND> get_hwnds_for_pid(qint64 pid);
	void bring_pid_to_foreground(qint64 pid, HWND hwnd = nullptr);
	bool is_pid_full_screen(qint64 pid, HWND hwnd = nullptr);
	bool bring_hwnd_to_foreground_uiautomation_method(HWND hwnd, IUIAutomation* uiAutomation);
	void bring_hwnd_to_foreground_attach_method(HWND hwnd);
	void bring_hwnd_to_foreground_alt_method(HWND hwnd);
	void bring_hwnd_to_foreground_console_method(HWND hwnd);
	void bring_hwnd_to_foreground(HWND hwnd);
	void bring_hwnd_to_foreground_all(HWND hwnd, IUIAutomation* uiAutomation);
	bool is_hwnd_full_screen(HWND hwnd);
	qreal volume_convert(int volume);
	QString getParentDirectoryName(QString path, int depth);
	double take_closest(QList<double> const& vec, int value);
	std::string convert_time_to_text(unsigned long int seconds);
	std::string convert_time_to_text(double seconds);
	bool IsMyWindowCovered(QWidget* MyWindow);
	bool IsMyWindowVisible(QWidget* MyWindow);
	QColor blendColors(const QColor& color1, const QColor& color2, double ratio);
	void changeQImageHue(QImage& img, QColor& newcolor, double blend_ratio);
	std::string getAppId(const char* VERSION);
	QString formatTimeAgo(qint64 seconds);
	QSize adjustSizeForAspect(const QSize& baseSize, double contentWidthFraction, int nonContentHeight, std::initializer_list<double> aspectGuesses);
	bool isSingleInstanceRunning(QString appid);
	void openFileExplorer(QString path);
	void numlock_toggle_on();
	std::chrono::microseconds QueryUnbiasedInterruptTimeChrono();
	double get_luminance(const QColor& color);
	QColor complementary_color(const QColor& color);
	QColor get_vibrant_color(const QColor& color, int satIncrease = 20, int lightAdjust = 0);
	QColor get_vibrant_color_exponential(const QColor& color, float saturation_strength = 0.1, float lightning_strength = 0.1);
	color_area& get_weighted_random_color(QList<color_area>& colors);
	inline long double normalize(long double x, long double maxVal);
	QList<long double> calculateWeights(const QList<VideoWeightedData>& items, double biasViews, double biasRating, double biasTags, double biasBpm, double biasGeneral, long double maxViews, long double maxRating, long double maxTagsWeight, long double maxBpm, long double no_views_weight = 0, long double no_rating_weight = 0, long double no_tags_weight = 0);
	QString weightedRandomChoice(const QList<VideoWeightedData>& items, QRandomGenerator& generator, double biasViews, double biasRating, double biasTags, double biasBpm, double biasGeneral, long double no_views_weight = 0, long double no_rating_weight = 0, long double no_tags_weight = 0);
	QMap<int, long double> calculateProbabilities(const QList<VideoWeightedData>& items, double biasViews, double biasRating, double biasTags, double biasBpm, double biasGeneral, long double no_views_weight, long double no_rating_weight, long double no_tags_weight);
	quint32 stringToSeed(const QString& textSeed);

	template <typename Func, typename... Args>
	void measureExecutionTime(Func&& func, const QString& message, Args&&... args) {
		auto start = std::chrono::high_resolution_clock::now();

		std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);

		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> elapsed = end - start;

		qDebug() << message << "Elapsed Time:" << elapsed.count() << "ms";
	}
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

	typedef BOOL(WINAPI* PFN_IsWindowArranged)(HWND hwnd);
	BOOL CallIsWindowArranged(HWND hwnd);
}
