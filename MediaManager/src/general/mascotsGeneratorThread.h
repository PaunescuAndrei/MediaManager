#pragma once
#include <QThread>
#include <QList>
#include <string>
#include <QPair>
#include <QPixmap>
#include <QColor>
#include "colorPaletteExtractor.h"

class MainApp;
template <class T> class BlockingQueue;

struct ImageData {
    QString path;
    QPixmap pixmap;
    QList<color_area> accepted_colors;
    QList<color_area> rejected_colors;
};

class mascotsGeneratorThread :
    public QThread
{
    Q_OBJECT
public:
    MainApp* App;
    QString mascots_path = "";
    bool running = false;
    bool allfiles_random = false;
    QStringList mascots_paths = QStringList();
    QStringList mascots_allpaths = QStringList();
    BlockingQueue<ImageData> *mascots_pixmap;
    void init_mascots_paths(QString mascots_path);
    void init_run();
    QString get_random_imgpath();
    QString get_random_imgpath(bool allfiles_random);
    ImageData get_img();
    void load_img();
    void run() override;
    mascotsGeneratorThread(MainApp* App,QString mascots_path, bool allfiles_random = false, QObject* parent = nullptr);
    ~mascotsGeneratorThread();
};

