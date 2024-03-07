#pragma once
#include <QPushButton>
#include <QWidget>

class QCustomButton : public QPushButton
{
    Q_OBJECT //don't forget this macro, or your signals/slots won't work
public:
    QCustomButton(QWidget* parent = nullptr);
    QCustomButton(const QString& text, QWidget* parent = nullptr);
    void mouseReleaseEvent(QMouseEvent* e) override;
    ~QCustomButton();
signals:
    void rightClicked();
    void middleClicked();
};
