#pragma once
#include <QThread>
#include <QMutex>
#include "sqliteDB.h"
#include "EventLock.h"
#include <QFuture>
#include <QStringList>
//#include <chrono>

class MainApp;

class IconChanger :
    public QThread
{
    Q_OBJECT
public:
    bool running = true;
    bool compression = false;
    bool random_icon = false;
    bool update_copies = false;
    double fps_modifier = 1.0;
    //std::chrono::steady_clock::time_point last_change = std::chrono::steady_clock::now();
    QFuture<void> future_seticon;
    QFuture<void> future_rebuildcache;
    QString currentIconPath = "";
    QMap<QString,QIcon> icon = QMap<QString,QIcon>();
    QMap<QString, QString> frame_dict = QMap<QString, QString>();
    QList<QPair<QString,int>> frame_pairlist = QList<QPair<QString, int>>();
    QList<QString> ordered_filenames = QList<QString>();
    QStringList archiveFiles = QStringList();
    double fps = 24;
    EventLock setIcon_lock = EventLock();
    EventLock animatedIconEvent = EventLock();
    QMutex data_lock = QMutex();
    MainApp* App = nullptr;
    sqliteDB* db = nullptr;
    IconChanger(MainApp* App,bool random_icon = false, QObject* parent = nullptr);
    void initIcon(bool instant= true);
    void setIcon(QString path,bool cache_check=true,bool instant = true);
    void setIconNonBlocking(QString path, bool cache_check = true,bool instant = true);
    void rebuildIconCacheNonBlocking();
    void setIconArchiveFiles(QString path);
    bool checkIconCache(QString path, QMap<QString, QIcon>* icon, QMap<QString, QString>* frame_dict, QList<QPair<QString, int>> *frame_pairlist, QList<QString> *ordered_filenames);
    void setRandomIcon(bool instant,bool updatedb = true);
    void rebuildIconCache();
    void showFirstIcon();
    void run() override;
    ~IconChanger();
signals:
    void animatedIconSignal(QIcon icon);
};
