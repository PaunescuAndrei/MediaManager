#include "stdafx.h"
#include "MainWindow.h"
#include "MainApp.h"
#include <SDKDDKVer.h>
#include "utils.h"
#include "version.h"
#include <QMessageBox>
#include <signal.h>
#include <QStandardPaths>

void manageSegFailure(int signalCode)
{
    QString dump_name_win = QFileInfo(QCoreApplication::applicationFilePath()).fileName() + "." + QString::number(QCoreApplication::applicationPid()) + ".dmp";
    int userResult = QMessageBox::critical(nullptr, "Error", QStringLiteral("Unexpected error has occurred!\nCrash dump: %1").arg(dump_name_win), QMessageBox::Ok);

    if (userResult == QMessageBox::Ok) {
        utils::openFileExplorer(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/CrashDumps");
    }
    signal(signalCode, SIG_DFL);
    QApplication::exit(3);
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
    if (utils::isSingleInstanceRunning(QString::fromStdString(utils::getAppId(VERSION_TEXT)))) {
        //bring to front, for now its handled on the existing process on connection
        return 1;
    }
    MainApp App(argc, argv);
    signal(SIGSEGV, manageSegFailure);
    int exitCode = App.exec();
    if (qMainApp && qMainApp->logger) {
        qMainApp->logger->log(QStringLiteral("Application Exited with code: %1").arg(exitCode), "INFO");
    }
    return exitCode;
    //try
    //{
    //    return App.exec();
    //}
    //catch (const std::runtime_error& re)
    //{
    //    // speciffic handling for runtime_error
    //    std::cerr << "Runtime error: " << re.what() << std::endl;
    //    QMessageBox::critical(nullptr, "Runtime error", QString::fromStdString(re.what()));
    //    throw;
    //}
    //catch (const std::exception& ex)
    //{
    //    // speciffic handling for all exceptions extending std::exception
    //    std::cerr << "Error occurred: " << ex.what() << std::endl;
    //    QMessageBox::critical(nullptr, "Error occurred", QString::fromStdString(ex.what()));
    //    throw;
    //}
    //catch (...)
    //{
    //    // catch any other errors (that we have no information about)
    //    QMessageBox::critical(nullptr, "Error occurred", "Unknown failure occurred. Possible memory corruption.");
    //    std::cerr << "Unknown failure occurred. Possible memory corruption" << std::endl;
    //    throw;
    //}
}
