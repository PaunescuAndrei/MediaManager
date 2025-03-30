#include "stdafx.h"
#include "utils.h"
#include <QString>
#include <QDebug>
#include <algorithm>
#include <cctype>
#include <QProcess>
#include <math.h>
#include <QDir>
#include "MainApp.h"
#include <chrono>
#include <QDirIterator>
#include <QDir>
#include "tlhelp32.h"
#include <QLocalSocket>
#include "realtimeapiset.h"

static BOOL CALLBACK utils::enumWindowCallback(HWND hWnd, LPARAM lparam)
{
	QPair<qint64, QList<HWND>*>* pair = (QPair<qint64, QList<HWND>*>*)lparam;
	qint64 pid = pair->first;
	QList<HWND>* hwnds = pair->second;
	DWORD found_pid = 0;
	if (IsWindowVisible(hWnd) && IsWindowEnabled(hWnd)) {
		GetWindowThreadProcessId(hWnd, &found_pid);
		if (pid == found_pid) {
			hwnds->append(hWnd);
		}
	}
	return true;
}

DWORD utils::checkProcRunning(std::string procfilename) {
	bool found = false;
	DWORD id = 0;
	HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 p32;
	p32.dwSize = sizeof(p32);
	BOOL hres = Process32First(hsnap, &p32);
	while (hres) {
		if (strcmp(p32.szExeFile, procfilename.c_str()) == 0) {
			id = p32.th32ProcessID;
			return id;
		}
		hres = Process32Next(hsnap, &p32);
	}
	CloseHandle(hsnap);
	return id;
}

QString utils::pathAppend(const QString& path1, const QString& path2)
{
	return path1 % QDir::separator() % path2;
}

QString utils::getParentDirectoryName(QString path, int depth) {
	QDir mainpath = QDir(path);
	//std::filesystem::path mainpath = std::filesystem::path(path);
	for (int i = 0; i < depth; i++) {
		mainpath.cdUp();
		//mainpath = mainpath.parent_path();
	}
	return mainpath.dirName();
}

int utils::randint(int start, int stop, quint32 seed) {
	QRandomGenerator generator;
	if (seed == 0) {
		generator.seed(QRandomGenerator::global()->generate());
	}
	else {
		generator.seed(seed);
	}
	std::mt19937 gen{ generator.generate()};
	std::uniform_int_distribution<> distribution(start, stop);
	return distribution(gen);
}

double utils::randfloat(double start, double stop, quint32 seed)
{
	QRandomGenerator generator;
	if (seed == 0) {
		generator.seed(QRandomGenerator::global()->generate());
	}
	else {
		generator.seed(seed);
	}
	std::mt19937 gen{ generator.generate() };
	std::uniform_real_distribution<> distribution(start, stop);
	return distribution(gen);
}

QList<HWND> utils::get_hwnds_for_pid(qint64 pid) {
	QList<HWND> hwnds = QList<HWND>();
	QPair<qint64, QList<HWND>*> pair = QPair<qint64, QList<HWND>*>(pid,&hwnds);
	EnumWindows(enumWindowCallback, (LPARAM)&pair);
	return hwnds;
}

void utils::bring_hwnd_to_foreground_attach_method(HWND hwnd)
{
	if (!IsWindow(hwnd)) return;

	//relation time of SetForegroundWindow lock
	DWORD lockTimeOut = 0;
	HWND  hCurrWnd = GetForegroundWindow();
	DWORD dwThisTID = GetCurrentThreadId(),
		dwCurrTID = GetWindowThreadProcessId(hCurrWnd, 0);

	//we need to bypass some limitations from Microsoft :)
	if (dwThisTID != dwCurrTID)
	{
		AttachThreadInput(dwThisTID, dwCurrTID, TRUE);

		SystemParametersInfo(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &lockTimeOut, 0);
		SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, 0, SPIF_SENDWININICHANGE |
			SPIF_UPDATEINIFILE);

		AllowSetForegroundWindow(ASFW_ANY);
	}

	bring_hwnd_to_foreground(hwnd);

	if (dwThisTID != dwCurrTID)
	{
		SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0,
			(PVOID)(size_t)lockTimeOut, SPIF_SENDWININICHANGE | SPIF_UPDATEINIFILE);
		AttachThreadInput(dwThisTID, dwCurrTID, FALSE);
	}
}

void utils::bring_hwnd_to_foreground_alt_method(HWND hwnd) {
	if (!IsWindow(hwnd)) return;

	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = VK_MENU;

	//to unlock SetForegroundWindow we need to imitate Alt pressing
	if (!GetAsyncKeyState(VK_MENU))
	{
		input.ki.dwFlags = 0;
		SendInput(1, &input, sizeof(INPUT));
	}

	bring_hwnd_to_foreground(hwnd);

	if (GetAsyncKeyState(VK_MENU))
	{
		input.ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(1, &input, sizeof(INPUT));
	}
}

void utils::bring_hwnd_to_foreground_console_method(HWND hwnd) {
	AllocConsole();
	auto hWndConsole = GetConsoleWindow();
	SetWindowPos(hWndConsole, 0, 0, 0, 0, 0, SWP_NOACTIVATE);
	FreeConsole();
	SetForegroundWindow(hwnd);
}

void utils::bring_hwnd_to_foreground(HWND hwnd) {
	//We can just call ShowWindow & SetForegroundWindow to bring hwnd to front. 
	//But that would also take maximized window out of maximized state. 
	//Using GetWindowPlacement preserves maximized state
	WINDOWPLACEMENT place;
	memset(&place, 0, sizeof(WINDOWPLACEMENT));
	place.length = sizeof(WINDOWPLACEMENT);
	GetWindowPlacement(hwnd, &place);

	switch (place.showCmd)
	{
	case SW_SHOWMAXIMIZED:
		ShowWindow(hwnd, SW_SHOWMAXIMIZED);
		break;
	case SW_SHOWMINIMIZED:
		ShowWindow(hwnd, SW_RESTORE);
		break;
	default:
		ShowWindow(hwnd, SW_SHOW);
		break;
	}
	SetForegroundWindow(hwnd);
}

bool utils::is_hwnd_full_screen(HWND hwnd) {
	tagRECT full_screen_rect = { 0,0,GetSystemMetrics(0),GetSystemMetrics(1) };
	tagRECT lprect;
	GetWindowRect(hwnd, &lprect);
	if (lprect.right == full_screen_rect.right && lprect.bottom == full_screen_rect.bottom)
		return true;
	return false;
}

bool utils::is_pid_full_screen(qint64 pid, HWND hwnd) {
	try {
		tagRECT full_screen_rect = { 0,0,GetSystemMetrics(0),GetSystemMetrics(1) };
		tagRECT lprect;
		if (hwnd == nullptr) {
			QList<HWND> hwnds = get_hwnds_for_pid(pid);
			for (HWND &hw : hwnds) {
				GetWindowRect(hw, &lprect);
				if (lprect.right == full_screen_rect.right && lprect.bottom == full_screen_rect.bottom)
					return true;
			}
		}
		else {
			GetWindowRect(hwnd, &lprect);
			if (lprect.right == full_screen_rect.right && lprect.bottom == full_screen_rect.bottom)
				return true;
		}
		return false;
	}
	catch (...) {
		return false;
	}
	return false;
}

bool utils::bring_hwnd_to_foreground_uiautomation_method(IUIAutomation* uiAutomation, HWND hwnd)
{
	bool result = false;
	// First restore if window is minimized
	WINDOWPLACEMENT placement{};
	placement.length = sizeof(placement);

	if (!GetWindowPlacement(hwnd, &placement))
		return false;

	bool minimized = placement.showCmd == SW_SHOWMINIMIZED;
	if (minimized)
		ShowWindow(hwnd, SW_RESTORE);

	// Then bring it to front using UI automation
	// stupid microsoft exception, just ignore it, it works
	IUIAutomationElement* window = nullptr;
	if (SUCCEEDED(uiAutomation->ElementFromHandle(hwnd, &window)))
	{
		if (SUCCEEDED(window->SetFocus()))
		{
			result = true;
		}
		window->Release();
	}
	return result;
}

qreal utils::volume_convert(int volume)
{
	return QAudio::convertVolume(volume / qreal(100.0), QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale);
}

void utils::bring_pid_to_foreground(qint64 pid, HWND hwnd) {
	if (hwnd == nullptr) {
		QList<HWND> hwnds = get_hwnds_for_pid(pid);
		for (HWND &hw : hwnds) {
			bring_hwnd_to_foreground(hw);
		}
	}
	else {
		bring_hwnd_to_foreground(hwnd);
	}
}


bool utils::text_to_bool(std::string str) {
	std::transform(str.begin(), str.end(), str.begin(), ::tolower);
	std::istringstream is(str);
	bool b;
	is >> std::boolalpha >> b;
	return b;
}

std::string utils::bool_to_text(bool b)
{
	std::stringstream converter;
	converter << std::boolalpha << b;   // flag boolalpha calls converter.setf(std::ios_base::boolalpha)
	std::string tmp = converter.str();
	tmp[0] = toupper(tmp[0]);
	return tmp;
}

QString utils::bool_to_text_qt(bool b)
{
	if (b)
		return "True";
	else
		return "False";
}

bool utils::pid_exists(qint64 pid) {
	//not implemented
	return false;
}

QList<std::string> utils::getFiles(std::string directory,bool recursive)
{
	QList<std::string> files = QList<std::string>();
	if (recursive) {
		QDirIterator it(QString::fromStdString(directory), QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext()) {
			files.append(QDir::toNativeSeparators(it.next()).toStdString());
		}
		//for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(entry.path().string());
		//}
	}
	else {
		QDirIterator it(QString::fromStdString(directory), QDir::Files, QDirIterator::NoIteratorFlags);
		while (it.hasNext()) {
			files.append(QDir::toNativeSeparators(it.next()).toStdString());
		}
		//for (const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(entry.path().string());
		//}
	}
	return files;
}

QStringList utils::getFilesQt(QString directory,bool recursive) {
	QStringList files = QStringList();
	if (recursive) {
		QDirIterator it(directory, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext()) {
			files.append(QDir::toNativeSeparators(it.next()));
		}
		//for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(QString::fromStdString(entry.path().string()));
		//}
	}
	else {
		QDirIterator it(directory, QDir::Files, QDirIterator::NoIteratorFlags);
		while (it.hasNext()) {
			files.append(QDir::toNativeSeparators(it.next()));
		}
		//for (const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(QString::fromStdString(entry.path().string()));
		//}
	}
	return files;
}

QList<std::string> utils::getDirs(std::string directory, bool recursive)
{
	QList<std::string> files = QList<std::string>();
	if (recursive) {
		QDirIterator it(QString::fromStdString(directory), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while (it.hasNext()) {
			files.append(it.next().toStdString());
		}
		//for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(entry.path().string());
		//}
	}
	else {
		QDirIterator it(QString::fromStdString(directory), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
		while (it.hasNext()) {
			files.append(it.next().toStdString());
		}
		//for (const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(entry.path().string());
		//}
	}
	return files;
}

QStringList utils::getDirsQt(QString directory, bool recursive) {
	QStringList files = QStringList();
	if (recursive) {
		QDirIterator it(directory, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
		while (it.hasNext()) {
			files.append(it.next());
		}
		//for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(QString::fromStdString(entry.path().string()));
		//}
	}
	else {
		QDirIterator it(directory, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
		while (it.hasNext()) {
			files.append(it.next());
		}
		//for (const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(QString::fromStdString(entry.path().string()));
		//}
	}
	return files;
}

QStringList utils::getDirNames(QString path) {
	QStringList files = QStringList();
	QFileInfo fileInfo1(path);
	if (fileInfo1.isDir())
		files.append(fileInfo1.baseName());
	while (!fileInfo1.dir().dirName().isEmpty()) {
		files.append(fileInfo1.dir().dirName());
		fileInfo1 = QFileInfo(fileInfo1.dir().absolutePath());
	}
	return files;
}

std::string utils::linux_to_windows_path(std::string& path) {
	std::replace(path.begin(), path.end(), '/', '\\');
	return path;
}

QPixmap utils::openImage(QString path) {
	QPixmap img = QPixmap(path);
	return img;
}

QString utils::getVideoDuration(QString path) {
	QProcess pingProcess;
	pingProcess.setProgram("ffprobe");
	pingProcess.setArguments({ "-v", "quiet", "-select_streams", "v:0", "-show_entries", "format=duration", "-of", "default=noprint_wrappers=1:nokey=1", path});
	pingProcess.start();
	pingProcess.waitForFinished();
	QString output(pingProcess.readAllStandardOutput());
	return output.trimmed();
}

std::string utils::formatSeconds(double total_seconds) {
	int minutes = total_seconds / 60;
	int seconds = round(fmod(total_seconds , 60));
	int hours = minutes / 60;
	minutes = minutes % 60;
	return std::format("{:02d}:{:02d}:{:02d}", hours, minutes, seconds);
}

QString utils::formatSecondsQt(double total_seconds) {
	return QString::fromStdString(formatSeconds(total_seconds));
}

bool utils::hiddenCheck(QStringList &settings) {
	return settings.length() > 1;
}

double utils::take_closest(QList<double> const& vec, int value) {
	auto const it = std::lower_bound(vec.begin(), vec.end(), value);
	return std::distance(vec.begin(), it);
}

std::string utils::convert_time_to_text(unsigned long int seconds)
{
	static const std::pair<int, const char*> time_table[] = {
		{31540000, "years"},{2628000, "months"}, {24 * 60 * 60, "days"}, {60 * 60, "hours"}, {60, "minutes"}, {1, "seconds"}
	};
	std::string text_result = "";
	std::vector<std::pair<unsigned long int, std::string>> result;
	for (const auto& e : time_table) {
		unsigned long int time = seconds / e.first;
		if (time != 0) result.emplace_back(time, e.second);
		seconds %= e.first;
	}
	for (auto it = result.begin(); it != result.end(); ++it)
	{
		if (std::next(it) == result.end() && result.size() > 1) // last element
		{
			text_result += " and ";
		}
		else if(it != result.begin()){
			text_result += ", ";
		}
		std::string value = std::to_string((*it).first);
		std::string type_text = (*it).second;
		if (value == "1" && !type_text.empty()) type_text.pop_back();
		text_result += value + " " + type_text;
	}

	return text_result;
}

bool utils::IsMyWindowCovered(QWidget* MyWindow)
{
	RECT MyRect = { 0 };
	GetWindowRect((HWND)MyWindow->winId(), &MyRect); // screen coordinates
	HRGN MyRgn = CreateRectRgnIndirect(&MyRect); // MyWindow not overlapped region
	HWND hwnd = GetTopWindow(nullptr); // currently examined topwindow
	int RType = SIMPLEREGION; // MyRgn type

	// From topmost window down to MyWindow, build the not overlapped portion of MyWindow
	while (hwnd != nullptr && hwnd != (HWND)MyWindow->winId() && RType != NULLREGION)
	{
		// nothing to do if hidden window
		if (IsWindowVisible(hwnd))
		{
			RECT TempRect = { 0 };
			GetWindowRect(hwnd, &TempRect);
			HRGN TempRgn = CreateRectRgnIndirect(&TempRect); // currently examined window region
			RType = CombineRgn(MyRgn, MyRgn, TempRgn, RGN_DIFF); // diff intersect
			DeleteObject(TempRgn);
		}
		if (RType != NULLREGION) // there's a remaining portion
			hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
	}

	DeleteObject(MyRgn);
	return RType == NULLREGION;
}

bool utils::IsMyWindowVisible(QWidget* MyWindow)
{
	return !MyWindow->isMinimized() && MyWindow->isVisible() && IsWindowVisible((HWND)MyWindow->winId()) && !IsMyWindowCovered(MyWindow);
}

QColor utils::blendColors(const QColor& color1, const QColor& color2, double ratio)
{
	int r = color1.red() * (1 - ratio) + color2.red() * ratio;
	int g = color1.green() * (1 - ratio) + color2.green() * ratio;
	int b = color1.blue() * (1 - ratio) + color2.blue() * ratio;

	return QColor(r, g, b, color1.alpha());
}

void utils::changeQImageHue(QImage& img, QColor &newcolor,double blend_ratio) {
	//for (int i = 0; i < img.width(); i++)
	//{
	//	for (int j = 0; j < img.height(); j++)
	//	{
	//		QColor color = img.pixelColor(i, j);
	//		if (color.alphaF() > 0) {
	//			QColor color_blend = blendColors(color, newcolor, 0.5);
	//			//color.setHsvF(newcolor.hueF(), color.saturationF(), color.valueF(), color.alphaF());
	//			img.setPixelColor(i, j, color_blend);
	//		}
	//	}
	//}
	for (int i = 0; i < img.height(); i++) {
		QRgb* pixel = reinterpret_cast<QRgb*>(img.scanLine(i));
		QRgb* end = pixel + img.width();
		for (; pixel != end; pixel++) {
			if (qAlpha(*pixel) > 0) {
				int gray = qGray(*pixel);
				QColor old_gray = QColor(gray, gray, gray, qAlpha(*pixel));
				QColor color_blend = blendColors(old_gray, newcolor, blend_ratio);
				*pixel = color_blend.rgba();
			}
		}
	}
}

std::string utils::getAppId(const char* VERSION)
{
	return std::format("mediamanager.qt.{}", VERSION);
}

bool utils::isSingleInstanceRunning(QString appid) {
	QLocalSocket socket;
	socket.connectToServer(appid);
	bool isOpen = socket.isOpen();
	socket.close();
	return isOpen;
}

void utils::openFileExplorer(QString path) {
	const QFileInfo fileInfo(path);
	if (fileInfo.exists()) {
		QStringList param;
		if (!fileInfo.isDir())
			param += QLatin1String("/select,");
		param += QDir::toNativeSeparators(fileInfo.canonicalFilePath());
		QProcess::startDetached("explorer", param);
	}
}

void utils::numlock_toggle_on() {
	if ((GetKeyState(VK_NUMLOCK) & 0x0001) != 0) {
		INPUT inputs[2] = {};
		ZeroMemory(inputs, sizeof(inputs));

		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = VK_NUMLOCK;
		inputs[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;

		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = VK_NUMLOCK;
		inputs[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;

		UINT uSent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
	}
}

std::chrono::microseconds utils::QueryUnbiasedInterruptTimeChrono()
{
	ULONGLONG interruptTime;
	if (!QueryUnbiasedInterruptTime(&interruptTime)) {
		interruptTime = -1;
	}
	interruptTime = interruptTime / 10;

	return std::chrono::microseconds(interruptTime);
}

double utils::get_luminance(QColor& color) {
	int R, G, B;
	color.getRgb(&R, &G, &B);
	const double rg = R <= 10 ? R / 3294.0 : std::pow((R / 269.0) + 0.0513, 2.4);
	const double gg = G <= 10 ? G / 3294.0 : std::pow((G / 269.0) + 0.0513, 2.4);
	const double bg = B <= 10 ? B / 3294.0 : std::pow((B / 269.0) + 0.0513, 2.4);
	return (0.2126 * rg) + (0.7152 * gg) + (0.0722 * bg);
}

QColor utils::complementary_color(QColor& color) {
	double Luminance = get_luminance(color);
	if(Luminance <= 0.35)
		return QColor(Qt::white);
	else
		return QColor(Qt::black);
}

color_area& utils::get_weighted_random_color(QList<color_area>& colors) {
	double sum_of_weight = 0;
	for (auto& c : colors) {
		sum_of_weight += c.area_percent;
	}
	double rnd = randfloat(0, sum_of_weight);
	for (auto& c : colors) {
		if (rnd < c.area_percent)
			return c;
		rnd -= c.area_percent;
	}
	assert(!"should never get here");
	return colors.first();
}

void utils::measureExecutionTime(std::function<void()> func, const QString& message) {
	auto start = std::chrono::high_resolution_clock::now();

	func();

	auto end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> elapsed = end - start;

	qDebug() << message << "Elapsed Time:" << elapsed.count() << "ms";
}

long double utils::normalize(long double x, long double maxVal)
{
	return (maxVal > 0) ? x / maxVal : 0.0;
}

QList<long double> utils::calculateWeights(const QList<VideoWeightedData>& items, double biasViews, double biasRating, double biasTags, double biasGeneral, long double maxViews, long double maxRating, long double maxTagsWeight, long double no_views_weight, long double no_rating_weight, long double no_tags_weight)
{
	QList<long double> weights;
	weights.reserve(items.size());

	for (const auto& item : items) {
		long double viewWeight = (item.views == 0) ? no_views_weight : normalize(item.views, maxViews);
		long double ratingWeight = (item.rating == 0) ? no_rating_weight : normalize(item.rating, maxRating);
		long double tagsWeight = (item.tagsWeight == 0) ? no_tags_weight : normalize(item.tagsWeight, maxTagsWeight);

		long double weight = (biasViews * viewWeight) + (biasRating * ratingWeight) + (biasTags * tagsWeight);

		// biasGeneral Value
		// 1.0	No change to weight(neutral)
		// > 1.0	Amplifies differences(larger values grow faster)
		// < 1.0 (but > 0)	Compresses differences(smaller values get closer together)
		// 0.0	Makes all weights equal to 1.0
		weight = powl(1+weight, biasGeneral); // Apply general bias
		weights.append(weight);
	}

	return weights;
}

QString utils::weightedRandomChoice(const QList<VideoWeightedData>& items, QRandomGenerator& generator, double biasViews, double biasRating, double biasTags, double biasGeneral, long double no_views_weight, long double no_rating_weight, long double no_tags_weight) {
	if (items.empty()) return "";
	// Calculate weights
	QList<long double> weights;
	if (biasGeneral == 0) {
		weights.fill(0, items.size());
	}
	else {
		long double maxViews = 0, maxRating = 0, maxTagWeight = 0;
		for (size_t i = 0; i < items.size(); ++i) {
			maxViews = std::max(maxViews, (long double)items[i].views);
			maxRating = std::max(maxRating, (long double)items[i].rating);
			maxTagWeight = std::max(maxTagWeight, (long double)items[i].tagsWeight);
		}
		weights = utils::calculateWeights(items, biasViews, biasRating, biasTags, biasGeneral, maxViews, maxRating, maxTagWeight, no_views_weight, no_rating_weight, no_tags_weight);
	}

	// Compute prefix sum for binary search
	QList<long double> prefixSum(weights.size());
	std::partial_sum(weights.begin(), weights.end(), prefixSum.begin());
	long double totalWeight = prefixSum.back();

	if (totalWeight == 0) return items[generator.bounded(int(items.size()))].path;

	// Generate a random number and perform binary search
	long double r = static_cast<long double>(generator.generateDouble()) * totalWeight;
	auto it = std::lower_bound(prefixSum.begin(), prefixSum.end(), r);
	size_t index = std::distance(prefixSum.begin(), it);
	//qDebug() << (double)weights[index] << items[index].path;
	return items[index].path;
}

quint32 utils::stringToSeed(const QString& textSeed) {
	QByteArray hash = QCryptographicHash::hash(textSeed.toUtf8(), QCryptographicHash::Md5);
	quint32 seed = 0;
	// Convert first 4 bytes of hash into a number
	for (int i = 0; i < 4; i++) {
		seed = (seed << 8) | (static_cast<quint32>(hash[i]) & 0xFF);
	}
	return seed;
}
