#pragma once

// Reports an unhandled fault from inside the fault, so the record survives a process that is about to die,
// and tells the user why the window closed. The split is by "what actually differs per platform", not by
// convenience:
//
//   - This header and crashhandler.cpp are platform-neutral. They own the wording, the order of the steps and
//     the fixed buffers, and they deliberately touch no Qt. Avoiding the event loop is only one of the
//     reasons: a fault handler also runs with the faulting frame still on the stack, possibly inside the
//     allocator or while a lock is held, and inside exception dispatch -- so an allocation can deadlock or,
//     on Windows, throw out of the filter and fail-fast the process. A POSIX handler is stricter still: only
//     async-signal-safe functions are legal there, which excludes every Qt type.
//   - crashhandler_win.cpp captures the fault (an unhandled-exception filter), writes the record with Win32,
//     and shows a native message box.
//   - A port is one new file, crashhandler_posix.cpp, implementing the same three detail functions: capture
//     with sigaction(), the record with write(2), and the notice through a helper process -- because no UI is
//     legal in a signal context on that side. Nothing above that file changes.
namespace crashhandler {
    // Hides the application's windows before anything is shown. Called from the fault path, so it must not
    // pump events or paint, and it runs on the thread it is called from: the callee is responsible for
    // ignoring threads that may not touch widgets.
    using QuiesceFn = void (*)();

    // Installs the fault capture and remembers where the record and the dumps live. `crashDirectoryUtf8` is
    // resolved by the caller while the application is healthy -- QStandardPaths, so the folder is the same
    // one the dump writer uses on every platform -- because no Qt may run once a fault is being handled.
    // Call once, before the application object exists.
    void install(const char* crashDirectoryUtf8, QuiesceFn quiesce);

    namespace detail {
        // Called by the platform's capture, which owns only the identification of the fault. `code` is the
        // platform's own fault identifier: an SEH exception code on Windows, a signal number on POSIX.
        void recordFault(unsigned long code, const void* address);

        // Implemented once per platform.
        void installPlatformCapture(const char* crashDirectoryUtf8);
        void appendMarker(const char* line);
        void notify(const char* text);
        // Whether the quiesce callback may run at all. Windows passes true: an exception filter runs on the
        // faulting thread with the faulting frame still on the stack, so hiding widgets is a bounded risk
        // that is still smaller than leaving them rendering. A POSIX build must pass false, because a signal
        // handler may not touch Qt at all.
        bool mayRunUiCode();
    }
}
