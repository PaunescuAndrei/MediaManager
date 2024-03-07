#pragma once
#include <QObject>
#include <QMouseEvent>
#include <QDebug>

class MainApp;

class generalEventFilter : public QObject {
    Q_OBJECT
public:
    MainApp* mApp = nullptr;
    generalEventFilter() {}
    generalEventFilter(QObject* parent) : QObject(parent) {}
    generalEventFilter(QObject* parent,MainApp *mainApp) : QObject(parent) {
        this->mApp = mainApp;
    }
    ~generalEventFilter() {
    }
protected:
    bool eventFilter(QObject* object, QEvent* event); 
};