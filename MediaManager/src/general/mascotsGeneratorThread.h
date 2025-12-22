#pragma once
#include <QThread>
#include <QList>
#include <string>
#include <QPair>
#include <QPixmap>
#include <QColor>
#include <random>
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
    void initMascotsPaths(QString mascots_path);
    void initRun();
    ImageData getRandomImagePath();
    ImageData getRandomImagePath(bool allfiles_random);
    ImageData getImage();
    static QPair<QList<color_area>, QList<color_area>> extractColors(QPixmap& img);
    void loadImage();
    void clearCache();
    void run() override;
    mascotsGeneratorThread(MainApp* App,QString mascots_path, bool allfiles_random = false, QObject* parent = nullptr);
    ~mascotsGeneratorThread();
private:
    void refreshRngFromConfig();
    std::mt19937 rng;
    QString last_seed_string;
    bool last_use_seed = false;
    bool rng_initialized = false;
};

