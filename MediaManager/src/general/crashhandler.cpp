#include "crashhandler.h"
#include <cstdio>
#include <cstring>

// Platform-neutral half of crashhandler.h. Nothing here may use Qt: a POSIX handler runs off a signal and may
// only call async-signal-safe functions, so every buffer is a fixed array and every string is C.
namespace {
    char g_directory[480] = {};
    crashhandler::QuiesceFn g_quiesce = nullptr;
}

void crashhandler::install(const char* crashDirectoryUtf8, QuiesceFn quiesce)
{
    g_quiesce = quiesce;
    g_directory[0] = '\0';
    if (crashDirectoryUtf8) {
        std::strncpy(g_directory, crashDirectoryUtf8, sizeof(g_directory) - 1);
        g_directory[sizeof(g_directory) - 1] = '\0';
    }
    detail::installPlatformCapture(g_directory);
}

void crashhandler::detail::recordFault(unsigned long code, const void* address)
{
    // The record comes first: hiding the windows is the one step that could fault again, and the code and the
    // address are the part that has to survive no matter what happens next.
    char line[256] = {};
    std::snprintf(line, sizeof(line), "fault 0x%08lX at %p\r\n", code, address);
    appendMarker(line);

    // Re-entering the renderer under a dialog is what turned the fault this handler was written for into a
    // second one, so the windows come down before anything is shown -- where the platform allows it at all.
    if (g_quiesce && detail::mayRunUiCode())
        g_quiesce();

    char text[1024] = {};
    std::snprintf(text, sizeof(text),
        "Media Manager hit an unexpected error and has to close.\r\n\r\n"
        "Fault 0x%08lX at %p.\r\n\r\n"
        "Crash dumps, when enabled in Settings, are written to:\r\n%s",
        code, address, g_directory[0] != '\0' ? g_directory : "(unknown)");
    notify(text);
}
