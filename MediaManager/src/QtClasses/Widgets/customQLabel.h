#pragma once
#include <QLabel>
#include <QEvent>

class MainWindow;

class customQLabel :public QLabel
{
    Q_OBJECT

public:
    customQLabel(QWidget* parent = nullptr);
    void setText(const QString& text);
    void mousePressEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    ~customQLabel();
signals:
    void clicked();
    void rightClicked();
    void middleClicked();
    void scrollUp();
    void scrollDown();
    void textChanged(const QString& text);
};