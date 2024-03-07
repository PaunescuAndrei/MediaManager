#pragma once
#include <QObject>
#include <QEvent>

class scrollAreaEventFilter : public QObject {
    Q_OBJECT
public:
    scrollAreaEventFilter(QWidget* parent = nullptr) : QObject(parent) {
    }
    bool eventFilter(QObject* obj, QEvent* event) override;
};