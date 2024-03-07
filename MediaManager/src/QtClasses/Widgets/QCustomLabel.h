#pragma once
#include <QLabel>
#include <QEvent>

class MainWindow;

class QCustomLabel :public QLabel
{
    Q_OBJECT

public:
    QCustomLabel(QWidget* parent = nullptr);
    void mousePressEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    ~QCustomLabel();
signals:
    void clicked();
    void rightClicked();
    void middleClicked();
    void scrollUp();
    void scrollDown();
};