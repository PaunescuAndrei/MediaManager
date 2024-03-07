#pragma once
#include <QObject>
#include <QEvent>

class QuitEater : public QObject
{
    Q_OBJECT
public:
    QuitEater(QObject* parent = nullptr);
protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
};