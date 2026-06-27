#pragma once
#include <QObject>
#include <QPoint>
#include <QTimer>

class MainApp;
class QWidget;

class TooltipEventFilter : public QObject {
    Q_OBJECT
public:
    TooltipEventFilter(MainApp* app);
    void reloadConfig();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void startShowTimer();
    void stopShowTimer();

    MainApp* mApp = nullptr;
    QTimer* showTimer = nullptr;
    QWidget* pendingTarget = nullptr;
    QPoint pendingPos;
    bool enabled = true;
    int delayMs = 700;
};
