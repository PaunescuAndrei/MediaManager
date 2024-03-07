#pragma once
#include <QToolButton>

class MainWindow;

class shokoButton : public QToolButton
{
    Q_OBJECT //don't forget this macro, or your signals/slots won't work

public:
    MainWindow* MW = nullptr;
    shokoButton(QWidget* parent = nullptr);
    void setMainWindow(MainWindow* window);
    void update_new_watching(QString ptw_playlist, bool random);
    void enterEvent(QEnterEvent* event);
    void leaveEvent(QEvent* event);
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void button_clicked();
    ~shokoButton();
};
