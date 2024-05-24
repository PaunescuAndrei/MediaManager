#pragma once
#include <QPushButton>
#include <QWidget>

class customQButton : public QPushButton
{
    Q_OBJECT //don't forget this macro, or your signals/slots won't work
public:
    customQButton(QWidget* parent = nullptr);
    customQButton(const QString& text, QWidget* parent = nullptr);
    void mouseReleaseEvent(QMouseEvent* e) override;
    ~customQButton();
signals:
    void rightClicked();
    void middleClicked();
};
