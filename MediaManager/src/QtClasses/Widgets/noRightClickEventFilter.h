#pragma once
#include <QObject>
#include <QMouseEvent>
#include <QDebug>

class MainApp;

class noRightClickEventFilter : public QObject {
    Q_OBJECT
public:
    MainApp* mApp = nullptr;
    noRightClickEventFilter(QObject* parent) : QObject(parent) {}
    ~noRightClickEventFilter() {
    }
protected:
    bool eventFilter(QObject* object, QEvent* event);
};