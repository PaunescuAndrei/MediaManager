#include "stdafx.h"
#include "utils.h"
#include <QString>
#include <QDebug>
#include <algorithm>
#include <cctype>
#include <QProcess>
#include <math.h>
#include <cmath>
#include <QDir>
#include "MainApp.h"
#include <chrono>
#include <QDirIterator>
#include <QDir>
#include "tlhelp32.h"
#include <QLocalSocket>
#include "realtimeapiset.h"
#include <limits>

// Qt Multimedia for audio decoding
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QEventLoop>
#include <QUrl>
#include <QTimer>

// SIMD intrinsics
#if defined(__AVX2__)
#include <immintrin.h>
#define USE_AVX2
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#include <emmintrin.h>
#define USE_SSE2
#endif

// SIMD-optimized conversion functions
namespace AudioConversion {

#ifdef USE_AVX2
    // AVX2: Process 8 samples at once
    inline void convertInt16ToFloat_AVX2(const qint16* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 32768.0f;
        const __m256 vscale = _mm256_set1_ps(scale);

        size_t i = 0;
        // Process 8 samples at a time
        for (; i + 8 <= count; i += 8) {
            // Load 8 int16 values (128 bits)
            __m128i vi16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            // Convert to int32 (256 bits)
            __m256i vi32 = _mm256_cvtepi16_epi32(vi16);
            // Convert to float
            __m256 vf = _mm256_cvtepi32_ps(vi32);
            // Scale
            vf = _mm256_mul_ps(vf, vscale);
            // Store
            _mm256_storeu_ps(dst + i, vf);
        }

        // Handle remaining samples
        for (; i < count; ++i) {
            dst[i] = static_cast<float>(src[i]) * scale;
        }
    }

    inline void convertInt32ToFloat_AVX2(const qint32* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 2147483648.0f;
        const __m256 vscale = _mm256_set1_ps(scale);

        size_t i = 0;
        // Process 8 samples at a time
        for (; i + 8 <= count; i += 8) {
            // Load 8 int32 values
            __m256i vi32 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
            // Convert to float
            __m256 vf = _mm256_cvtepi32_ps(vi32);
            // Scale
            vf = _mm256_mul_ps(vf, vscale);
            // Store
            _mm256_storeu_ps(dst + i, vf);
        }

        // Handle remaining samples
        for (; i < count; ++i) {
            dst[i] = static_cast<float>(src[i]) * scale;
        }
    }

    inline void convertUInt8ToFloat_AVX2(const quint8* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 128.0f;
        const __m256 vscale = _mm256_set1_ps(scale);
        const __m256 voffset = _mm256_set1_ps(-128.0f);

        size_t i = 0;
        // Process 8 samples at a time
        for (; i + 8 <= count; i += 8) {
            // Load 8 uint8 values
            __m128i vi8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src + i));
            // Convert uint8 to int32
            __m256i vi32 = _mm256_cvtepu8_epi32(vi8);
            // Convert to float
            __m256 vf = _mm256_cvtepi32_ps(vi32);
            // Add offset and scale
            vf = _mm256_add_ps(vf, voffset);
            vf = _mm256_mul_ps(vf, vscale);
            // Store
            _mm256_storeu_ps(dst + i, vf);
        }

        // Handle remaining samples
        for (; i < count; ++i) {
            dst[i] = (static_cast<float>(src[i]) - 128.0f) * scale;
        }
    }

#elif defined(USE_SSE2)
    // SSE2: Process 4 samples at once
    inline void convertInt16ToFloat_SSE2(const qint16* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 32768.0f;
        const __m128 vscale = _mm_set1_ps(scale);

        size_t i = 0;
        // Process 4 samples at a time
        for (; i + 4 <= count; i += 4) {
            // Load 4 int16 values and convert to int32
            __m128i vi16 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src + i));
            __m128i vi32 = _mm_cvtepi16_epi32(vi16); // SSE4.1, fallback below if needed
            // Convert to float
            __m128 vf = _mm_cvtepi32_ps(vi32);
            // Scale
            vf = _mm_mul_ps(vf, vscale);
            // Store
            _mm_storeu_ps(dst + i, vf);
        }

        // Handle remaining samples
        for (; i < count; ++i) {
            dst[i] = static_cast<float>(src[i]) * scale;
        }
    }

    inline void convertInt32ToFloat_SSE2(const qint32* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 2147483648.0f;
        const __m128 vscale = _mm_set1_ps(scale);

        size_t i = 0;
        for (; i + 4 <= count; i += 4) {
            __m128i vi32 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            __m128 vf = _mm_cvtepi32_ps(vi32);
            vf = _mm_mul_ps(vf, vscale);
            _mm_storeu_ps(dst + i, vf);
        }

        for (; i < count; ++i) {
            dst[i] = static_cast<float>(src[i]) * scale;
        }
    }

    inline void convertUInt8ToFloat_SSE2(const quint8* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 128.0f;
        const __m128 vscale = _mm_set1_ps(scale);
        const __m128 voffset = _mm_set1_ps(-128.0f);

        size_t i = 0;
        for (; i + 4 <= count; i += 4) {
            // Manual conversion for uint8 to int32
            __m128i vi8 = _mm_cvtsi32_si128(*reinterpret_cast<const int*>(src + i));
            __m128i zero = _mm_setzero_si128();
            __m128i vi16 = _mm_unpacklo_epi8(vi8, zero);
            __m128i vi32 = _mm_unpacklo_epi16(vi16, zero);

            __m128 vf = _mm_cvtepi32_ps(vi32);
            vf = _mm_add_ps(vf, voffset);
            vf = _mm_mul_ps(vf, vscale);
            _mm_storeu_ps(dst + i, vf);
        }

        for (; i < count; ++i) {
            dst[i] = (static_cast<float>(src[i]) - 128.0f) * scale;
        }
    }
#endif

    // Fallback scalar implementations
    inline void convertInt16ToFloat_Scalar(const qint16* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 32768.0f;
        for (size_t i = 0; i < count; ++i) {
            dst[i] = static_cast<float>(src[i]) * scale;
        }
    }

    inline void convertInt32ToFloat_Scalar(const qint32* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 2147483648.0f;
        for (size_t i = 0; i < count; ++i) {
            dst[i] = static_cast<float>(src[i]) * scale;
        }
    }

    inline void convertUInt8ToFloat_Scalar(const quint8* src, float* dst, size_t count) {
        constexpr float scale = 1.0f / 128.0f;
        for (size_t i = 0; i < count; ++i) {
            dst[i] = (static_cast<float>(src[i]) - 128.0f) * scale;
        }
    }

    // Unified interface that selects best implementation
    inline void convertInt16ToFloat(const qint16* src, float* dst, size_t count) {
#ifdef USE_AVX2
        convertInt16ToFloat_AVX2(src, dst, count);
#elif defined(USE_SSE2)
        convertInt16ToFloat_SSE2(src, dst, count);
#else
        convertInt16ToFloat_Scalar(src, dst, count);
#endif
    }

    inline void convertInt32ToFloat(const qint32* src, float* dst, size_t count) {
#ifdef USE_AVX2
        convertInt32ToFloat_AVX2(src, dst, count);
#elif defined(USE_SSE2)
        convertInt32ToFloat_SSE2(src, dst, count);
#else
        convertInt32ToFloat_Scalar(src, dst, count);
#endif
    }

    inline void convertUInt8ToFloat(const quint8* src, float* dst, size_t count) {
#ifdef USE_AVX2
        convertUInt8ToFloat_AVX2(src, dst, count);
#elif defined(USE_SSE2)
        convertUInt8ToFloat_SSE2(src, dst, count);
#else
        convertUInt8ToFloat_Scalar(src, dst, count);
#endif
    }
}

// Optimized helper to read audio file using QAudioDecoder with SIMD conversion
bool utils::readAudioFile(const QString& path, std::vector<float>& audioData, int& sampleRate, int& channels) {
    QAudioDecoder decoder;
    decoder.setSource(QUrl::fromLocalFile(path));

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool success = true;
    audioData.clear();
    sampleRate = 0;
    channels = 0;

    // Batch buffers for fewer allocations and faster processing
    std::vector<QAudioBuffer> buffers;
    buffers.reserve(100);

    // Collect all buffers first
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&]() {
        timer.start(15000);
        QAudioBuffer buffer = decoder.read();
        if (buffer.isValid()) {
            buffers.push_back(buffer);
        }
        });

    QObject::connect(&decoder, &QAudioDecoder::finished, [&]() {
        timer.stop();
        if (buffers.empty()) {
            success = false;
            loop.quit();
            return;
        }

        // Get format from first buffer
        const auto& firstBuffer = buffers[0];
        sampleRate = firstBuffer.format().sampleRate();
        channels = firstBuffer.format().channelCount();
        QAudioFormat::SampleFormat format = firstBuffer.format().sampleFormat();

        // Calculate total size for single allocation
        size_t totalSamples = 0;
        for (const auto& buffer : buffers) {
            totalSamples += buffer.sampleCount();
        }
        audioData.reserve(totalSamples);

        // Process all buffers at once based on format with SIMD optimization
        if (format == QAudioFormat::Float) {
            // Fast path - direct copy
            for (const auto& buffer : buffers) {
                const float* data = buffer.constData<float>();
                int count = buffer.sampleCount();
                audioData.insert(audioData.end(), data, data + count);
            }
        }
        else if (format == QAudioFormat::Int16) {
            // SIMD-optimized Int16 conversion
            for (const auto& buffer : buffers) {
                const qint16* data = buffer.constData<qint16>();
                int count = buffer.sampleCount();
                size_t oldSize = audioData.size();
                audioData.resize(oldSize + count);
                float* dest = audioData.data() + oldSize;

                AudioConversion::convertInt16ToFloat(data, dest, count);
            }
        }
        else if (format == QAudioFormat::Int32) {
            // SIMD-optimized Int32 conversion
            for (const auto& buffer : buffers) {
                const qint32* data = buffer.constData<qint32>();
                int count = buffer.sampleCount();
                size_t oldSize = audioData.size();
                audioData.resize(oldSize + count);
                float* dest = audioData.data() + oldSize;

                AudioConversion::convertInt32ToFloat(data, dest, count);
            }
        }
        else if (format == QAudioFormat::UInt8) {
            // SIMD-optimized UInt8 conversion
            for (const auto& buffer : buffers) {
                const quint8* data = buffer.constData<quint8>();
                int count = buffer.sampleCount();
                size_t oldSize = audioData.size();
                audioData.resize(oldSize + count);
                float* dest = audioData.data() + oldSize;

                AudioConversion::convertUInt8ToFloat(data, dest, count);
            }
        }
        else {
            qWarning() << "Unsupported sample format:" << format;
            success = false;
        }

        loop.quit();
        });

    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error),
        [&](QAudioDecoder::Error error) {
            timer.stop();
            qWarning() << "Audio decode error:" << decoder.errorString();
            decoder.stop();
            success = false;
            loop.quit();
        });

    
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        qWarning() << "Audio decode timeout for:" << path;
        success = false;
        decoder.stop();
        loop.quit();
    });

    timer.start(15000); // 15 seconds timeout
    decoder.start();
    loop.exec();

    return success && !audioData.empty() && sampleRate > 0 && channels > 0;
}

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

void utils::bring_hwnd_to_foreground_all(HWND hwnd, IUIAutomation* uiAutomation)
{
	utils::bring_hwnd_to_foreground_uiautomation_method(hwnd, uiAutomation);
	if (GetForegroundWindow() != hwnd)
		utils::bring_hwnd_to_foreground_uiautomation_method(hwnd, uiAutomation);
	else if (GetForegroundWindow() != hwnd)
		utils::bring_hwnd_to_foreground(hwnd);
	else if (GetForegroundWindow() != hwnd)
		utils::bring_hwnd_to_foreground_console_method(hwnd);
	else if (GetForegroundWindow() != hwnd)
		utils::bring_hwnd_to_foreground_alt_method(hwnd);
	//if (GetForegroundWindow() != this->old_foreground_window)
	//	bring_hwnd_to_foreground_attach_method(this->old_foreground_window);
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

bool utils::bring_hwnd_to_foreground_uiautomation_method(HWND hwnd, IUIAutomation* uiAutomation)
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

QList<std::string> utils::getFiles(std::string directory,bool recursive, bool relative_path)
{
	QList<std::string> files = QList<std::string>();
	QDir dir(QString::fromStdString(directory));
	if (recursive) {
		QDirIterator it(QString::fromStdString(directory), QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext()) {
			files.append(QDir::toNativeSeparators(relative_path ? dir.relativeFilePath(it.next()) : it.next()).toStdString());
		}
		//for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(entry.path().string());
		//}
	}
	else {
		QDirIterator it(QString::fromStdString(directory), QDir::Files, QDirIterator::NoIteratorFlags);
		while (it.hasNext()) {
			files.append(QDir::toNativeSeparators(relative_path ? dir.relativeFilePath(it.next()) : it.next()).toStdString());
		}
		//for (const auto& entry : std::filesystem::directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(entry.path().string());
		//}
	}
	return files;
}

QStringList utils::getFilesQt(QString directory,bool recursive, bool relative_path) {
	QStringList files = QStringList();
	QDir dir(directory);
	if (recursive) {
		QDirIterator it(directory, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext()) {
			files.append(dir.toNativeSeparators(relative_path ? dir.relativeFilePath(it.next()) : it.next()));
		}
		//for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, std::filesystem::directory_options::skip_permission_denied)) {
		//	files.append(QString::fromStdString(entry.path().string()));
		//}
	}
	else {
		QDirIterator it(directory, QDir::Files, QDirIterator::NoIteratorFlags);
		while (it.hasNext()) {
			files.append(dir.toNativeSeparators(relative_path ? dir.relativeFilePath(it.next()) : it.next()));
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

QString utils::getRootPath(const QString &pathStr) {
	#ifdef Q_OS_WIN
		std::filesystem::path path(pathStr.toStdWString());  // Convert QString to wide string on Windows
	#else
		std::filesystem::path path(pathStr.toStdString());   // Use UTF-8 on Unix-like systems
	#endif
	return QString::fromStdString(path.root_path().string());
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

std::string utils::formatSeconds(double total_seconds, bool showHours) {
	int totalSec = static_cast<int>(std::round(total_seconds));
	int minutes = totalSec / 60;
	int seconds = totalSec % 60;
	int hours = minutes / 60;
	minutes = minutes % 60;
	if (showHours || hours > 0) {
		return std::format("{:02d}:{:02d}:{:02d}", hours, minutes, seconds);
	}
	return std::format("{:02d}:{:02d}", minutes + hours * 60, seconds);
}

QString utils::formatSecondsQt(double total_seconds, bool showHours) {
	return QString::fromStdString(formatSeconds(total_seconds, showHours));
}

std::string utils::formatSecondsCompact(double total_seconds)
{
	return formatSeconds(total_seconds, false);
}

QString utils::formatSecondsCompactQt(double total_seconds)
{
	return QString::fromStdString(formatSecondsCompact(total_seconds));
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

std::string utils::convert_time_to_text(double seconds) {
	return utils::convert_time_to_text(static_cast<unsigned long int>(seconds));
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

QSize utils::adjustSizeForAspect(const QSize& baseSize, double contentWidthFraction, int nonContentHeight, std::initializer_list<double> aspectGuesses)
{
	if (baseSize.width() <= 0 || baseSize.height() <= 0)
		return baseSize;

	const double fraction = std::clamp(contentWidthFraction, 0.0, 1.0);
	const int previewWidth = static_cast<int>(std::round(baseSize.width() * (fraction > 0.0 ? fraction : 1.0)));
	const int nonPreviewHeight = std::max(0, nonContentHeight);

	int bestHeight = baseSize.height();
	int smallestDelta = std::numeric_limits<int>::max();
	for (double aspect : aspectGuesses) {
		if (aspect <= 0.0 || previewWidth <= 0)
			continue;
		const int previewHeight = static_cast<int>(std::ceil(previewWidth / aspect));
		const int candidate = nonPreviewHeight + previewHeight;
		const int delta = std::max(0, candidate - baseSize.height());
		if (delta < smallestDelta) {
			smallestDelta = delta;
			bestHeight = std::max(baseSize.height(), candidate);
		}
	}

	return QSize(baseSize.width(), bestHeight);
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

QString utils::formatTimeAgo(qint64 seconds) {
	if (seconds < 1) {
		return "Just now";
	}
	if (seconds < 60) {
		return QStringLiteral("%1 second%2 ago").arg(seconds).arg(seconds == 1 ? "" : "s");
	}
	else if (seconds < 3600) {
		int minutes = seconds / 60;
		return QStringLiteral("%1 minute%2 ago").arg(minutes).arg(minutes == 1 ? "" : "s");
	}
	else if (seconds < 86400) {
		int hours = seconds / 3600;
		return QStringLiteral("%1 hour%2 ago").arg(hours).arg(hours == 1 ? "" : "s");
	}
	else if (seconds < 604800) {
		int days = seconds / 86400;
		return QStringLiteral("%1 day%2 ago").arg(days).arg(days == 1 ? "" : "s");
	}
	else if (seconds < 2592000) {
		int weeks = seconds / 604800;
		return QStringLiteral("%1 week%2 ago").arg(weeks).arg(weeks == 1 ? "" : "s");
	}
	else if (seconds < 31536000) {
		int months = seconds / 2592000;
		return QStringLiteral("%1 month%2 ago").arg(months).arg(months == 1 ? "" : "s");
	}
	else {
		int years = seconds / 31536000;
		return QStringLiteral("%1 year%2 ago").arg(years).arg(years == 1 ? "" : "s");
	}
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

double utils::get_luminance(const QColor& color) {
	int R, G, B;
	color.getRgb(&R, &G, &B);
	const double rg = R <= 10 ? R / 3294.0 : std::pow((R / 269.0) + 0.0513, 2.4);
	const double gg = G <= 10 ? G / 3294.0 : std::pow((G / 269.0) + 0.0513, 2.4);
	const double bg = B <= 10 ? B / 3294.0 : std::pow((B / 269.0) + 0.0513, 2.4);
	return (0.2126 * rg) + (0.7152 * gg) + (0.0722 * bg);
}

QColor utils::complementary_color(const QColor& color) {
	double Luminance = get_luminance(color);
	if(Luminance <= 0.35)
		return QColor(Qt::white);
	else
		return QColor(Qt::black);
}
QColor utils::get_vibrant_color(const QColor& color, int satIncrease, int lightAdjust) {
	float h, s, l, a;
	color.getHslF(&h, &s, &l, &a);

	s = qMin(1.0, s + s * (satIncrease / 100.0));
	l = qBound(0.0, l + l * (lightAdjust / 100.0), 1.0);

	return QColor::fromHslF(h, s, l, a);
}

QColor utils::get_vibrant_color_exponential(const QColor& color, float saturation_strength, float lightning_strength) {
	float h, s, l, a;
	color.getHslF(&h, &s, &l, &a);

	s = s + (1.0 - s) * saturation_strength;
	l = l + (1.0 - l) * lightning_strength;

	s = qMin(1.0, s);
	l = qMin(1.0, l);

	return QColor::fromHslF(h, s, l, a);
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

long double utils::normalize(long double x, long double maxVal)
{
	return (maxVal > 0) ? x / maxVal : 0.0;
}

QList<long double> utils::calculateWeights(const QList<VideoWeightedData>& items, double biasViews, double biasRating, double biasTags, double biasBpm, double biasGeneral, long double maxViews, long double maxRating, long double maxTagsWeight, long double maxBpm, long double no_views_weight, long double no_rating_weight, long double no_tags_weight)
{
	const int size = items.size();
	QList<long double> weights;
	weights.reserve(size);

	// Pre-calculate scaled bias factors (combines bias with reciprocal)
	const long double scaledBiasViews = (maxViews > 0 && biasViews != 0.0) ? (biasViews / maxViews) : 0.0L;
	const long double scaledBiasRating = (maxRating > 0 && biasRating != 0.0) ? (biasRating / maxRating) : 0.0L;
	const long double scaledBiasTags = (maxTagsWeight > 0 && biasTags != 0.0) ? (biasTags / maxTagsWeight) : 0.0L;
	const long double scaledBiasBpm = (maxBpm > 0 && biasBpm != 0.0) ? (biasBpm / maxBpm) : 0.0L;

	// Pre-calculate fallback values scaled by bias
	const long double viewFallback = biasViews * no_views_weight;
	const long double ratingFallback = biasRating * no_rating_weight;
	const long double tagsFallback = biasTags * no_tags_weight;

	for (const VideoWeightedData& item : items) {
		long double weight = 0.0L;

		// Combine normalization and bias multiplication in single operation
		weight += (item.views == 0) ? viewFallback : (item.views * scaledBiasViews);
		weight += (item.rating == 0) ? ratingFallback : (item.rating * scaledBiasRating);
		weight += (item.tagsWeight == 0) ? tagsFallback : (item.tagsWeight * scaledBiasTags);
		if (item.bpm > 0)
			weight += (item.bpm * scaledBiasBpm);

		// Apply general bias
		// biasGeneral Value:
		// 1.0   = No change to weight (neutral)
		// > 1.0 = Amplifies differences (larger values grow faster)
		// < 1.0 (but > 0) = Compresses differences (smaller values get closer together)
		// 0.0   = Makes all weights equal to 1.0
		weight = powl(1.0L + weight, biasGeneral);

		weights.append(weight);
	}

	return weights;
}

QString utils::weightedRandomChoice(const QList<VideoWeightedData>& items, QRandomGenerator& generator, double biasViews, double biasRating, double biasTags, double biasBpm, double biasGeneral, long double no_views_weight, long double no_rating_weight, long double no_tags_weight) {
	if (items.empty()) return "";

	// Fast path for uniform random (no bias)
	if (biasGeneral == 0) {
		return items[generator.bounded(items.size())].path;
	}

	const int size = items.size();

	// Find max values in a single pass
	long double maxViews = 0, maxRating = 0, maxTagWeight = 0, maxBpm = 0;
	for (const VideoWeightedData& item : items) {
		if (item.views > maxViews) maxViews = item.views;
		if (item.rating > maxRating) maxRating = item.rating;
		if (item.tagsWeight > maxTagWeight) maxTagWeight = item.tagsWeight;
		if (item.bpm > maxBpm) maxBpm = item.bpm;
	}

	// Calculate weights
	QList<long double> weights = utils::calculateWeights(items, biasViews, biasRating, biasTags, biasBpm, biasGeneral,
		maxViews, maxRating, maxTagWeight, maxBpm,
		no_views_weight, no_rating_weight, no_tags_weight);

	// Build prefix sum array for binary search
	QList<long double> prefixSum;
	prefixSum.reserve(size);

	long double sum = 0;
	for (int i = 0; i < size; ++i) {
		sum += weights[i];
		prefixSum.append(sum);
	}

	long double totalWeight = sum;

	// Fallback to uniform random if all weights are zero
	if (totalWeight == 0) {
		return items[generator.bounded(size)].path;
	}

	// Generate random number and find index via binary search
	long double r = static_cast<long double>(generator.generateDouble()) * totalWeight;
	auto it = std::lower_bound(prefixSum.constBegin(), prefixSum.constEnd(), r);
	int index = std::distance(prefixSum.constBegin(), it);

	return items[index].path;
}

QMap<int, long double> utils::calculateProbabilities(const QList<VideoWeightedData>& items, double biasViews, double biasRating, double biasTags, double biasBpm, double biasGeneral, long double no_views_weight, long double no_rating_weight, long double no_tags_weight) {
	QMap<int, long double> probabilities;
	if (items.empty()) {
		return probabilities;
	}

	long double maxViews = 0, maxRating = 0, maxTagsWeight = 0, maxBpm = 0;
	for (const auto& item : items) {
		maxViews = std::max(maxViews, (long double)item.views);
		maxRating = std::max(maxRating, (long double)item.rating);
		maxTagsWeight = std::max(maxTagsWeight, (long double)item.tagsWeight);
		maxBpm = std::max(maxBpm, (long double)item.bpm);
	}

	QList<long double> weights = utils::calculateWeights(items, biasViews, biasRating, biasTags, biasBpm, biasGeneral, maxViews, maxRating, maxTagsWeight, maxBpm, no_views_weight, no_rating_weight, no_tags_weight);
	long double totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0L);

	if (totalWeight == 0) {
		long double equalProbability = 100.0 / items.size();
		for (const auto& item : items) {
			probabilities.insert(item.id, equalProbability);
			//qDebug() << item.path << ":" << QString::number(equalProbability, 'f', 2) << "%";
		}
		return probabilities;
	}

	for (int i = 0; i < items.size(); ++i) {
		long double probability = (weights[i] / totalWeight) * 100.0;
		probabilities.insert(items[i].id, probability);
		//qDebug() << items[i].path << ":" << QString::number(probability, 'f', 2) << "%";
	}

	return probabilities;
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

BOOL utils::CallIsWindowArranged(HWND hwnd)
{
	static PFN_IsWindowArranged pIsWindowArranged = nullptr;
	static bool triedToLoad = false;

	if (!triedToLoad) {
		HMODULE hUser32 = GetModuleHandleW(L"user32.dll"); // Already loaded, no FreeLibrary needed
		if (hUser32) {
			pIsWindowArranged = (PFN_IsWindowArranged)GetProcAddress(hUser32, "IsWindowArranged");
		}
		triedToLoad = true;
	}

	if (pIsWindowArranged) {
		return pIsWindowArranged(hwnd);
	}
	else {
		qDebug() << "IsWindowArranged not available on this system.";
		if (qApp)
			qMainApp->logger->log("isWindowArranged not available on this system.","Warning");
		return FALSE;
	}
}
