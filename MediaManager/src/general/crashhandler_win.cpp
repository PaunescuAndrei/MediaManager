#include "crashhandler.h"
#include <windows.h>
#include <shellapi.h>
#include <cwchar>
#include <iterator>

// Windows half of crashhandler.h. A port implements the same three detail functions in crashhandler_posix.cpp
// -- sigaction() for the capture, write(2) for the record, and a helper process for the notice, because no UI
// is legal in a signal context -- and nothing above this file changes.

namespace {
    wchar_t g_directoryW[MAX_PATH * 2] = {};
    // Interlocked, not a bool: the fault can arrive on any thread and re-entry would stack a second dialog.
    LONG g_entered = 0;

    LONG WINAPI onFault(EXCEPTION_POINTERS* info)
    {
        if (::InterlockedExchange(&g_entered, 1) != 0)
            return EXCEPTION_CONTINUE_SEARCH;

        DWORD code = 0;
        const void* address = nullptr;
        if (info && info->ExceptionRecord) {
            code = info->ExceptionRecord->ExceptionCode;
            address = info->ExceptionRecord->ExceptionAddress;
        }
        crashhandler::detail::recordFault(code, address);

        // Continuing the search is what lets Error Reporting write the dump the notice points at, and the
        // process then dies here instead of resuming on whatever state produced the fault.
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

bool crashhandler::detail::mayRunUiCode()
{
    // An exception filter runs on the faulting thread before any unwind, so even hiding widgets can itself
    // fault. That bounded risk is taken here because the alternative is not bounded: leaving them visible
    // under a modal box is what re-entered the renderer and produced the second fault. A POSIX build must
    // answer false instead -- a signal handler may not touch Qt at all.
    return true;
}

void crashhandler::detail::installPlatformCapture(const char* crashDirectoryUtf8)
{
    g_directoryW[0] = L'\0';
    if (crashDirectoryUtf8) {
        // The path arrives as UTF-8 because the layer above is neutral; a profile name outside the ANSI code
        // page would be mangled by the narrow APIs.
        ::MultiByteToWideChar(CP_UTF8, 0, crashDirectoryUtf8, -1, g_directoryW, int(std::size(g_directoryW)));
        g_directoryW[std::size(g_directoryW) - 1] = L'\0';
    }
    ::SetUnhandledExceptionFilter(&onFault);
}

void crashhandler::detail::appendMarker(const char* line)
{
    if (g_directoryW[0] == L'\0')
        return;
    ::CreateDirectoryW(g_directoryW, nullptr);
    wchar_t path[MAX_PATH * 2 + 32] = {};
    swprintf_s(path, L"%s\\crash.log", g_directoryW);
    HANDLE file = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;

    // The timestamp belongs to the write, not to the text: only the platform knows which clock and format to
    // use, and getting it neutral would mean a locale call that is not safe from here.
    SYSTEMTIME now = {};
    ::GetLocalTime(&now);
    wchar_t stamp[64] = {};
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth, now.wDay, now.wHour,
        now.wMinute, now.wSecond);
    wchar_t wide[512] = {};
    ::MultiByteToWideChar(CP_UTF8, 0, line, -1, wide, int(std::size(wide)));

    DWORD written = 0;
    ::WriteFile(file, stamp, DWORD(wcslen(stamp) * sizeof(wchar_t)), &written, nullptr);
    ::WriteFile(file, wide, DWORD(wcslen(wide) * sizeof(wchar_t)), &written, nullptr);
    ::CloseHandle(file);
}

void crashhandler::detail::notify(const char* text)
{
    // MessageBoxW, not a Qt dialog: this runs inside the fault, and a Qt modal loop is what let the
    // QQuickWidget update timer keep replaying into the device that had just been removed.
    wchar_t wide[1200] = {};
    ::MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, int(std::size(wide)));
    wchar_t message[1400] = {};
    swprintf_s(message, L"%s\r\n\r\nOpen that folder?", wide);
    const int answer = ::MessageBoxW(nullptr, message, L"Media Manager",
        MB_ICONERROR | MB_YESNO | MB_SETFOREGROUND);
    if (answer == IDYES && g_directoryW[0] != L'\0')
        ::ShellExecuteW(nullptr, L"open", g_directoryW, nullptr, nullptr, SW_SHOWNORMAL);
}
