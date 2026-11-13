#include "stdafx.h"
#include "MainWindow.h"
#include "MainApp.h"
#include <SDKDDKVer.h>
#include "utils.h"
#include "version.h"
#include <shellapi.h>
#include <atomic>
#include <cwchar>
#include <string>

namespace {
    // Every call below is Win32. The handler this replaces answered a fault by opening a QMessageBox,
    // whose nested event loop kept the QQuickWidget update timer running; the next tick replayed a
    // D3D11 command list into the device the failing driver update had already removed, and the
    // process died inside its own error dialog. No Qt event loop may run again from here.
    std::atomic<LONG> faultDepth{ 0 };

    std::wstring crashDumpDir()
    {
        wchar_t buffer[MAX_PATH * 4] = {};
        const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, DWORD(std::size(buffer)));
        if (length == 0 || length >= std::size(buffer))
            return std::wstring();
        // The folder Windows Error Reporting's LocalDumps setting writes the .dmp into.
        return std::wstring(buffer) + L"\\CrashDumps";
    }

    void appendCrashLine(const std::wstring& directory, const wchar_t* text)
    {
        if (directory.empty() || !text)
            return;
        ::CreateDirectoryW(directory.c_str(), nullptr);
        const std::wstring path = directory + L"\\crash.log";
        HANDLE file = ::CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        ::WriteFile(file, text, DWORD(wcslen(text) * sizeof(wchar_t)), &written, nullptr);
        ::CloseHandle(file);
    }

    // QQuickWidgetPrivate::renderSceneGraph() returns while the widget is not visible, so hiding the
    // top-levels is what stops a frame from being replayed into a device that may be gone. Widgets may only
    // be touched from the GUI thread, and the fault can arrive on a decoder thread instead.
    void hideTopLevels()
    {
        if (!qApp || QThread::currentThread() != qApp->thread())
            return;
        const QWidgetList windows = QApplication::topLevelWidgets();
        for (QWidget* window : windows) {
            if (window)
                window->hide();
        }
    }

    LONG WINAPI handleFatalFault(EXCEPTION_POINTERS* info)
    {
        // A second dialog stacked over the first would be worse than no dialog at all.
        if (faultDepth.fetch_add(1) != 0)
            return EXCEPTION_CONTINUE_SEARCH;

        const DWORD code = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionCode : 0;
        const void* address = (info && info->ExceptionRecord) ? info->ExceptionRecord->ExceptionAddress : nullptr;

        hideTopLevels();

        const std::wstring directory = crashDumpDir();

        SYSTEMTIME now = {};
        ::GetLocalTime(&now);
        wchar_t line[512] = {};
        swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u exception 0x%08lX at %p pid %lu\n",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
            code, address, ::GetCurrentProcessId());
        appendCrashLine(directory, line);

        wchar_t message[1024] = {};
        swprintf_s(message,
            L"Media Manager hit an unexpected error and has to close.\r\n\r\n"
            L"Exception 0x%08lX at %p.\r\n\r\n"
            L"Crash dumps, when enabled under Settings > Crash dumps, are written to:\r\n%s\r\n\r\n"
            L"Open that folder?",
            code, address, directory.empty() ? L"(LOCALAPPDATA is not set)" : directory.c_str());

        const int answer = ::MessageBoxW(nullptr, message, L"Media Manager",
            MB_ICONERROR | MB_YESNO | MB_SETFOREGROUND);
        if (answer == IDYES && !directory.empty())
            ::ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

        // Continuing the search is what lets Error Reporting write the dump this dialog promises, and
        // the process then dies here instead of resuming on a graphics stack that is already gone.
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

int main(int argc, char *argv[])
{
    // The CRT's SIGSEGV handler only receives a signal number and always re-raises, so it can neither
    // report the faulting address nor let the dump be written afterwards. An unhandled-exception
    // filter sees the exception record and can hand the process back to Error Reporting.
    ::SetUnhandledExceptionFilter(handleFatalFault);

    // Force QRhi-backed composition for every QWidget top-level from the very start.
    // The preview popups host QQuickWidgets; if the main window starts raster-backed,
    // adding such a child window makes Qt recreate the main window's native window
    // mid-life (a Hide/Show flash on the first preview). Starting every top-level on
    // RHI means the popup's QQuickWidget matches from the beginning, so no switch and
    // no recreation are needed. Must be set before the first widget window exists.
    qputenv("QT_WIDGETS_RHI", "1");

    if (!SUCCEEDED(CoInitialize(nullptr)))
    {
        return 1;
    }
    if (utils::isSingleInstanceRunning(QString::fromStdString(utils::getAppId(VERSION_TEXT)))) {
        //bring to front, for now its handled on the existing process on connection
        return 1;
    }
    MainApp App(argc, argv);
    int exitCode = App.exec();
    if (qMainApp && qMainApp->logger) {
        qMainApp->logger->log(QStringLiteral("Application Exited with code: %1").arg(exitCode), "INFO");
    }
    return exitCode;
}
