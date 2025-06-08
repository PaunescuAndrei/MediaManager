#pragma once
#include <QObject>
#include <QEvent>
#include <QWidget>

class WheelStrongFocusEventFilter : public QObject {
    Q_OBJECT
public:
    WheelStrongFocusEventFilter(QObject* parent = nullptr);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
};
