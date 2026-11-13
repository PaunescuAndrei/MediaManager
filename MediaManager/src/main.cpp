#include "stdafx.h"
#include "MainWindow.h"
#include "MainApp.h"
#include <SDKDDKVer.h>
#include "utils.h"
#include "version.h"
#include <QStandardPaths>
#include "crashhandler.h"

namespace {
    // Hides the windows under the crash notice. A QQuickWidget that is not visible returns from
    // renderSceneGraph() before it touches the graphics stack, which is the point: the Qt dialog that used to
    // sit here kept the frame timer running over a device that had already been removed. Widgets may only be
    // touched from the GUI thread, and a fault can arrive on a decoder thread instead.
    void quiesceForCrash()
    {
        if (!qApp || QThread::currentThread() != qApp->thread())
            return;
        const QWidgetList windows = QApplication::topLevelWidgets();
        for (QWidget* window : windows) {
            if (window)
                window->hide();
        }
    }
}

int main(int argc, char *argv[])
{
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

    // Installed once COM exists, because the folder is resolved with QStandardPaths (SHGetKnownFolderPath)
    // and it has to be resolved here: nothing Qt may run once a fault is being handled. The folder is the
    // same one the dump writer uses on every platform, so the notice and the .dmp stay next to each other.
    crashhandler::install(
        (QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/CrashDumps"))
            .toUtf8().constData(),
        &quiesceForCrash);

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
