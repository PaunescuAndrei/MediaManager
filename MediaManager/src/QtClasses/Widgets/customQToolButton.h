#pragma once
#include <QToolButton>
#include <QWidget>

class customQToolButton : public QToolButton
{
    Q_OBJECT
public:
    customQToolButton(QWidget* parent = nullptr);
    customQToolButton(const QString& text, QWidget* parent = nullptr);
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    ~customQToolButton();
signals:
    void rightClicked();
    void middleClicked();
private:
    Qt::MouseButton m_pressedButton = Qt::NoButton;
};
