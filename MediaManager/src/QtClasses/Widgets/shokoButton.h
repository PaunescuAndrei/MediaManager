#pragma once
#include "customQToolButton.h"

class MainWindow;

class shokoButton : public customQToolButton
{
    Q_OBJECT

public:
    MainWindow* MW = nullptr;
    shokoButton(QWidget* parent = nullptr);
    void setMainWindow(MainWindow* window);
    void update_new_watching(QString ptw_playlist, bool random);
    void enterEvent(QEnterEvent* event);
    void leaveEvent(QEvent* event);
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void button_clicked();
    ~shokoButton();
};
