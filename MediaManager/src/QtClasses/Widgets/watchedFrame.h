#pragma once
#include <QFrame>

class MainWindow;

class watchedFrame :
    public QFrame
{
    Q_OBJECT

public:
    MainWindow* MW = nullptr;
    watchedFrame(QWidget* parent = nullptr);
    void setMainWindow(MainWindow* window);
    void mousePressEvent(QMouseEvent* e) override;
    ~watchedFrame();
};

