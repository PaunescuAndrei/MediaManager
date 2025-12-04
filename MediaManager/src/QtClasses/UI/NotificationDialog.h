#pragma once
#include <QDialog>
#include "ui_NotificationDialog.h"
#include <chrono>
#include <QTimer>

class MainWindow;

class NotificationDialog :
    public QDialog
{
    Q_OBJECT
public:
    QTimer* timer;
    QTimer* timer2;
    MainWindow *MW = nullptr;
    int timerInterval = 1000;
    std::chrono::time_point<std::chrono::steady_clock> time_start = std::chrono::steady_clock::now();
    std::chrono::milliseconds time_duration = std::chrono::milliseconds(10000);
    NotificationDialog(QWidget* parent = nullptr);
    void setMainWindow(MainWindow* MW);
    void closeNotification();
    void showNotification();
    void showNotification(int duration, int interval);
    void pauseNotification();
    void resizeEvent(QResizeEvent*) override;
    ~NotificationDialog();
   void mousePressEvent(QMouseEvent* event) override;
    Ui::NotificationDialog ui;
signals:
    void showEventSignal();

private:
    bool paused = false;
};
