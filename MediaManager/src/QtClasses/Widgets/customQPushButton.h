#pragma once
#include <QPushButton>
#include <QWidget>

class customQPushButton : public QPushButton
{
    Q_OBJECT
public:
    customQPushButton(QWidget* parent = nullptr);
    customQPushButton(const QString& text, QWidget* parent = nullptr);
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    ~customQPushButton();
signals:
    void rightClicked();
    void middleClicked();
private:
    Qt::MouseButton m_pressedButton = Qt::NoButton;
};
